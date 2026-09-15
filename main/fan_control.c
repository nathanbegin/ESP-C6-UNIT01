#include "fan_control.h"

#include <stdatomic.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "FAN";
static const gpio_num_t pins[] = {
    FAN_RELAY1_GPIO, FAN_RELAY2_GPIO, FAN_RELAY3_GPIO, FAN_RELAY4_GPIO,
};
static fan_controller_t controller;
static QueueHandle_t commands;
static fan_state_callback_t state_callback;
static atomic_bool report_requested;

static const char *mode_name(fan_mode_t mode)
{
    switch (mode) {
    case FAN_OFF:  return "OFF";
    case FAN_LOW:  return "FAN 1 / BASSE";
    case FAN_HIGH: return "FAN 2 / HAUTE";
    default:       return "INCONNU";
    }
}

static const char *control_name(fan_switch_t control)
{
    switch (control) {
    case FAN_SWITCH_LOW:      return "FAN 1 / BASSE";
    case FAN_SWITCH_HIGH:     return "FAN 2 / HAUTE";
    case FAN_SWITCH_EXCHANGE: return "ECHANGE EXTERIEUR";
    default:                  return "INCONNU";
    }
}

static void log_state(const char *prefix, fan_state_t state)
{
    uint8_t mask = fan_relay_mask(state);
    ESP_LOGI(TAG, "%s: mode=%s, échange=%s, K1=%u K2=%u K3=%u K4=%u",
             prefix, mode_name(state.mode), state.exchange ? "ON" : "OFF",
             !!(mask & 1), !!(mask & 2), !!(mask & 4), !!(mask & 8));
}

typedef struct {
    bool set_mode;
    fan_mode_t mode;
    fan_switch_t control;
    bool on;
} fan_command_t;

static bool write_relay(void *context, unsigned relay, bool energized)
{
    (void)context;
#ifdef CONFIG_APP_RELAY_ACTIVE_LOW
    const int level = !energized;
#else
    const int level = energized;
#endif
    esp_err_t err = gpio_set_level(pins[relay], level);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "K%u GPIO%d -> bobine %s (niveau=%s)", relay + 1,
                 pins[relay], energized ? "ACTIVEE" : "RELACHEE",
                 level ? "HIGH" : "LOW");
    } else {
        ESP_LOGE(TAG, "K%u GPIO%d: %s", relay + 1, pins[relay], esp_err_to_name(err));
    }
    return err == ESP_OK;
}

static void settle(void *context)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_RELAY_SETTLE_MS));
}

esp_err_t fan_control_init(void)
{
#ifdef CONFIG_APP_RELAY_ACTIVE_LOW
    ESP_LOGI(TAG, "Initialisation relais: entrées actives LOW, délai=%d ms",
             CONFIG_APP_RELAY_SETTLE_MS);
#else
    ESP_LOGI(TAG, "Initialisation relais: entrées actives HIGH, délai=%d ms",
             CONFIG_APP_RELAY_SETTLE_MS);
#endif
    /* Preload the inactive output latch before enabling each output. */
    for (unsigned i = 0; i < 4; ++i) {
        if (!write_relay(NULL, i, false)) return ESP_FAIL;
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) return err;
    }
    fan_io_t io = {.write_relay = write_relay, .settle = settle};
    if (!fan_controller_init(&controller, io)) return ESP_FAIL;
    log_state("Etat initial applique", controller.state);
    return ESP_OK;
}

static void fan_task(void *arg)
{
    (void)arg;
    TickType_t last_report = xTaskGetTickCount();
    for (;;) {
        fan_command_t command;
        bool changed = xQueueReceive(commands, &command, pdMS_TO_TICKS(100)) == pdTRUE;
        if (changed) {
            bool ok;
            fan_state_t before = controller.state;
            if (command.set_mode) {
                ESP_LOGI(TAG, "Exécution commande interne: mode %s", mode_name(command.mode));
            } else {
                ESP_LOGI(TAG, "Exécution commande Zigbee: %s -> %s",
                         control_name(command.control), command.on ? "ON" : "OFF");
            }
            log_state("Etat avant", before);
            if (command.set_mode) {
                fan_state_t target = controller.state;
                target.mode = command.mode;
                ok = fan_controller_apply(&controller, target);
            } else {
                ok = fan_controller_switch(&controller, command.control, command.on);
            }
            if (ok) {
                if (before.mode == controller.state.mode &&
                    before.exchange == controller.state.exchange) {
                    ESP_LOGI(TAG, "Commande sans changement physique");
                }
                log_state("Etat applique", controller.state);
            } else {
                ESP_LOGE(TAG, "Commande échouée; remise au repos tentée");
                if (controller.healthy) log_state("Etat de récupération", controller.state);
                else ESP_LOGE(TAG, "Contrôleur relais en défaut; commandes bloquées");
            }
        }
        TickType_t now = xTaskGetTickCount();
        bool requested = atomic_exchange(&report_requested, false);
        if (controller.healthy && (changed || requested ||
            now - last_report >= pdMS_TO_TICKS(30000))) {
            state_callback(controller.state);
            last_report = now;
        }
    }
}

esp_err_t fan_control_start(fan_state_callback_t callback)
{
    if (commands || !callback || !controller.healthy) return ESP_ERR_INVALID_STATE;
    commands = xQueueCreate(16, sizeof(fan_command_t));
    if (!commands) return ESP_ERR_NO_MEM;
    state_callback = callback;
    if (xTaskCreate(fan_task, "fan_control", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(commands);
        commands = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t enqueue(fan_command_t command)
{
    if (!commands) {
        ESP_LOGE(TAG, "Commande refusée: contrôleur non démarré");
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(commands, &command, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Commande refusée: file pleine");
        fan_control_request_report();
        return ESP_ERR_NO_MEM;
    }
    if (command.set_mode) {
        ESP_LOGI(TAG, "Commande interne acceptée: mode %s", mode_name(command.mode));
    } else {
        ESP_LOGI(TAG, "Commande Zigbee acceptée: %s -> %s",
                 control_name(command.control), command.on ? "ON" : "OFF");
    }
    return ESP_OK;
}

esp_err_t fan_set_mode(fan_mode_t mode)
{
    if (mode < FAN_OFF || mode > FAN_HIGH) return ESP_ERR_INVALID_ARG;
    return enqueue((fan_command_t){.set_mode = true, .mode = mode});
}

esp_err_t outdoor_exchange_set(bool enabled)
{
    return fan_control_set_switch(FAN_SWITCH_EXCHANGE, enabled);
}

esp_err_t fan_control_set_switch(fan_switch_t control, bool on)
{
    if (control < FAN_SWITCH_LOW || control > FAN_SWITCH_EXCHANGE) return ESP_ERR_INVALID_ARG;
    return enqueue((fan_command_t){.control = control, .on = on});
}

void fan_control_request_report(void)
{
    atomic_store(&report_requested, true);
}
