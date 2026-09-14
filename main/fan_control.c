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
    if (err != ESP_OK) ESP_LOGE(TAG, "K%u: %s", relay + 1, esp_err_to_name(err));
    return err == ESP_OK;
}

static void settle(void *context)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_RELAY_SETTLE_MS));
}

esp_err_t fan_control_init(void)
{
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
    return fan_controller_init(&controller, io) ? ESP_OK : ESP_FAIL;
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
            if (command.set_mode) {
                fan_state_t target = controller.state;
                target.mode = command.mode;
                ok = fan_controller_apply(&controller, target);
            } else {
                ok = fan_controller_switch(&controller, command.control, command.on);
            }
            if (!ok) ESP_LOGE(TAG, "Commande échouée; remise au repos tentée");
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
    if (!commands) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(commands, &command, 0) != pdTRUE) {
        fan_control_request_report();
        return ESP_ERR_NO_MEM;
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
