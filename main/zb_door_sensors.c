/*
 * zb_door_sensors.c
 * ESP32-C6-DEV-KIT-N8 — Zigbee Router/End Device
 *
 * Endpoint 1 : On/Off Light  -> LED RGB embarquée (GPIO8)
 * Endpoint 2 : IAS Zone       -> reed porte 1 (GPIO2)
 * Endpoint 3 : IAS Zone       -> reed porte 2 (GPIO3)
 * Endpoint 4 : IAS Zone       -> reed porte 3 (GPIO4)
 * Endpoint 5 : On/Off         -> Fan 1 (position 2, basse vitesse)
 * Endpoint 6 : On/Off         -> Fan 2 (position 3, haute vitesse)
 * Endpoint 7 : On/Off         -> échange extérieur
 */

#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "led_strip.h"

#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zb_door_sensors.h"
#include "app_config.h"
#include "fan_control.h"

static const char *TAG = "ZB_DOOR";

static atomic_bool s_zb_ready;
static atomic_uint s_sync_generation;

/* ---------- LED embarquée ---------- */
static led_strip_handle_t s_led;

static void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds       = LED_STRIP_LEDS,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led));
    led_strip_clear(s_led);
}

static void led_set(bool on)
{
    if (on) {
        led_strip_set_pixel(s_led, 0, 24, 24, 24); /* blanc doux */
        led_strip_refresh(s_led);
    } else {
        led_strip_clear(s_led);
    }
}

/* ---------- Read-only Zigbee state publication ---------- */
static void report_on_off_locked(uint8_t endpoint)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {0};
    cmd.zcl_basic_cmd.src_endpoint = endpoint;
    cmd.zcl_basic_cmd.dst_endpoint = 1;
    cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
    cmd.attributeID = ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
    cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    esp_err_t err = esp_zb_zcl_report_attr_cmd_req(&cmd);
    if (err != ESP_OK) ESP_LOGW(TAG, "Rapport EP%u: %s", endpoint, esp_err_to_name(err));
}

/* Called by the relay task AFTER the physical switching sequence. */
static void fan_state_report(fan_state_t state)
{
    if (!atomic_load(&s_zb_ready)) return;
    const uint8_t endpoints[] = {HA_FAN1_ENDPOINT, HA_FAN2_ENDPOINT, HA_EXCHANGE_ENDPOINT};
    bool values[] = {state.mode == FAN_LOW, state.mode == FAN_HIGH, state.exchange};
    esp_zb_lock_acquire(portMAX_DELAY);
    if (atomic_load(&s_zb_ready)) {
        for (unsigned i = 0; i < 3; ++i) {
            esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
                endpoints[i], ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                &values[i], false);
            if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Attribut EP%u: statut %u", endpoints[i], (unsigned)status);
            }
        }
        /* Report OFF before ON to avoid showing both speeds active in the UI. */
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (unsigned i = 0; i < 3; ++i) {
                if (values[i] == (bool)pass) report_on_off_locked(endpoints[i]);
            }
        }
    }
    esp_zb_lock_release();
}

/* ---------- Door inputs: periodic sampling also recovers missed edges ---------- */
typedef struct {
    uint8_t endpoint;
    gpio_num_t gpio;
    bool stable_open;
    bool candidate_open;
    bool dirty;
    TickType_t candidate_since;
} door_t;

static door_t s_doors[DOOR_COUNT] = {
    {.endpoint = HA_DOOR1_ENDPOINT, .gpio = DOOR1_GPIO},
    {.endpoint = HA_DOOR2_ENDPOINT, .gpio = DOOR2_GPIO},
    {.endpoint = HA_DOOR3_ENDPOINT, .gpio = DOOR3_GPIO},
};

static bool door_is_open(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 1;
}

static bool door_report(door_t *door)
{
    if (!atomic_load(&s_zb_ready)) return false;
    uint16_t zone_status = door->stable_open ? 1 : 0;
    bool requested = false;
    esp_zb_lock_acquire(portMAX_DELAY);
    if (atomic_load(&s_zb_ready)) {
        esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
            door->endpoint, ESP_ZB_ZCL_CLUSTER_ID_IAS_ZONE,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_IAS_ZONE_ZONESTATUS_ID,
            &zone_status, false);
        if (status == ESP_ZB_ZCL_STATUS_SUCCESS) {
            esp_zb_zcl_ias_zone_status_change_notif_cmd_t cmd = {0};
            cmd.zcl_basic_cmd.src_endpoint = door->endpoint;
            cmd.zcl_basic_cmd.dst_endpoint = 1;
            cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
            cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
            cmd.zone_status = zone_status;
            cmd.zone_id = door->endpoint - HA_DOOR1_ENDPOINT;
            /* This SDK returns a transaction number, NOT a delivery result. */
            esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
            requested = true;
        } else {
            ESP_LOGW(TAG, "Attribut porte EP%u: statut %u", door->endpoint, (unsigned)status);
        }
    }
    esp_zb_lock_release();
    if (requested) ESP_LOGI(TAG, "Porte EP%u: %s (notification demandée)",
                           door->endpoint, door->stable_open ? "OUVERTE" : "FERMEE");
    return requested;
}

static void door_task(void *arg)
{
    (void)arg;
    unsigned generation = 0;
    TickType_t last_refresh = xTaskGetTickCount();
    for (unsigned i = 0; i < DOOR_COUNT; ++i) {
        s_doors[i].stable_open = s_doors[i].candidate_open = door_is_open(s_doors[i].gpio);
        s_doors[i].candidate_since = last_refresh;
        s_doors[i].dirty = true;
    }
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        unsigned next_generation = atomic_load(&s_sync_generation);
        bool refresh = next_generation != generation || now - last_refresh >= pdMS_TO_TICKS(30000);
        if (refresh) {
            generation = next_generation;
            last_refresh = now;
        }
        for (unsigned i = 0; i < DOOR_COUNT; ++i) {
            door_t *door = &s_doors[i];
            bool open = door_is_open(door->gpio);
            if (open != door->candidate_open) {
                door->candidate_open = open;
                door->candidate_since = now;
            }
            if (refresh) door->dirty = true;
            if (now - door->candidate_since < pdMS_TO_TICKS(DOOR_DEBOUNCE_MS)) continue;
            if (door->stable_open != open) {
                door->stable_open = open;
                door->dirty = true;
            }
            if (door->dirty && door_report(door)) door->dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void doors_gpio_init(void)
{
    gpio_config_t io = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << DOOR1_GPIO) | (1ULL << DOOR2_GPIO) | (1ULL << DOOR3_GPIO),
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    if (xTaskCreate(door_task, "door_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

/* Incoming On/Off writes only enqueue work: no relay delay in the Zigbee task. */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id, const void *message)
{
    if (cb_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) return ESP_OK;
    const esp_zb_zcl_set_attr_value_message_t *m = message;
    if (!m || m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS || !m->attribute.data.value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (m->info.cluster != ESP_ZB_ZCL_CLUSTER_ID_ON_OFF ||
        m->attribute.id != ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID ||
        m->attribute.data.type != ESP_ZB_ZCL_ATTR_TYPE_BOOL) return ESP_OK;
    bool on = *(bool *)m->attribute.data.value;
    switch (m->info.dst_endpoint) {
    case HA_LED_ENDPOINT:
        led_set(on);
        report_on_off_locked(HA_LED_ENDPOINT);
        return ESP_OK;
    case HA_FAN1_ENDPOINT:
        return fan_control_set_switch(FAN_SWITCH_LOW, on);
    case HA_FAN2_ENDPOINT:
        return fan_control_set_switch(FAN_SWITCH_HIGH, on);
    case HA_EXCHANGE_ENDPOINT:
        return outdoor_exchange_set(on);
    default:
        return ESP_OK;
    }
}

/* ---------- Construction des endpoints ---------- */
static esp_zb_ep_list_t *build_endpoints(void)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    /* --- Endpoint 1 : On/Off Light (LED) --- */
    {
        esp_zb_on_off_light_cfg_t led_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
        esp_zb_cluster_list_t *cl = esp_zb_on_off_light_clusters_create(&led_cfg);

        /* Ajout des chaînes fabricant/modèle sur le cluster Basic */
        esp_zb_attribute_list_t *basic =
            esp_zb_cluster_list_get_cluster(cl, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        esp_zb_basic_cluster_add_attr(basic,
            ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, MANUFACTURER_NAME);
        esp_zb_basic_cluster_add_attr(basic,
            ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, MODEL_IDENTIFIER);

        esp_zb_endpoint_config_t ep_cfg = {
            .endpoint = HA_LED_ENDPOINT,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
    }

    /* --- Endpoints 2..4 : IAS Zone (contacts de porte) --- */
    for (int i = 0; i < DOOR_COUNT; i++) {
        esp_zb_ias_zone_cluster_cfg_t zone_cfg = {
            .zone_state = ESP_ZB_ZCL_IAS_ZONE_ZONESTATE_NOT_ENROLLED,
            /* 0x0015 = Contact Switch */
            .zone_type  = ESP_ZB_ZCL_IAS_ZONE_ZONETYPE_CONTACT_SWITCH,
            .zone_status = door_is_open(s_doors[i].gpio) ? 1 : 0,
            .zone_id     = (uint8_t)i,
            .ias_cie_addr = {0},
        };
        esp_zb_attribute_list_t *ias =
            esp_zb_ias_zone_cluster_create(&zone_cfg);

        esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
        esp_zb_cluster_list_add_ias_zone_cluster(cl, ias,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        /* Cluster Basic minimal sur chaque endpoint */
        esp_zb_basic_cluster_cfg_t basic_cfg = {
            .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
            .power_source = 0x01, /* mains */
        };
        esp_zb_cluster_list_add_basic_cluster(cl,
            esp_zb_basic_cluster_create(&basic_cfg),
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_endpoint_config_t ep_cfg = {
            .endpoint = s_doors[i].endpoint,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_IAS_ZONE_ID,
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, cl, ep_cfg);
    }

    /* Endpoints 5..7 are logical functions, never individual relay outputs. */
    const uint8_t controls[] = {HA_FAN1_ENDPOINT, HA_FAN2_ENDPOINT, HA_EXCHANGE_ENDPOINT};
    for (unsigned i = 0; i < 3; ++i) {
        esp_zb_on_off_light_cfg_t cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
        cfg.on_off_cfg.on_off = false;
        esp_zb_cluster_list_t *clusters = esp_zb_on_off_light_clusters_create(&cfg);
        esp_zb_endpoint_config_t endpoint = {
            .endpoint = controls[i],
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
            .app_device_version = 1,
        };
        ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, clusters, endpoint));
    }

    return ep_list;
}

/* ---------- Gestion du commissioning ---------- */
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialisé");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {

            ESP_LOGI(TAG, "Démarrage OK (%s)",
                     sig_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START ?
                     "première fois" : "reboot");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Recherche d'un réseau à rejoindre...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Déjà sur un réseau.");
                atomic_store(&s_zb_ready, true);
                atomic_fetch_add(&s_sync_generation, 1);
                fan_control_request_report();
            }
        } else {
            ESP_LOGW(TAG, "Échec init (%s), nouvel essai...",
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            atomic_store(&s_zb_ready, true);
            atomic_fetch_add(&s_sync_generation, 1);
            fan_control_request_report();
            esp_zb_ieee_addr_t ext_pan;
            esp_zb_get_extended_pan_id(ext_pan);
            ESP_LOGI(TAG, "Rejoint le réseau. Canal: %d",
                     esp_zb_get_current_channel());
        } else {
            atomic_store(&s_zb_ready, false);
            ESP_LOGW(TAG, "Steering échoué (%s), réessai...",
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        if (err_status == ESP_OK) {
            atomic_store(&s_zb_ready, false);
            ESP_LOGI(TAG, "Réseau quitté; redémarrage du commissioning");
            esp_restart();
        }
        break;
    default:
        ESP_LOGI(TAG, "Signal ZDO: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

/* ---------- Tâche principale Zigbee ---------- */
static void esp_zb_task(void *pv)
{
    esp_zb_cfg_t zb_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_cfg);

    esp_zb_ep_list_t *ep_list = build_endpoints();
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    /* Released coils in normal AND Wi-Fi configuration mode. */
    ESP_ERROR_CHECK(fan_control_init());
    /* NVS doit être prête avant de lire le mode de démarrage */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(nvs_ret);

    /* --- Aiguillage selon le mode de démarrage --- */
    boot_mode_t mode = app_config_get_boot_mode();

    if (mode == BOOT_MODE_CONFIG) {
        /* Mode configuration : portail Wi-Fi, Zigbee PAS démarré.
           app_config_run_portal() ne retourne pas (reboot via le portail). */
        app_config_run_portal();
        return;
    }

    /* --- Mode NORMAL : Zigbee --- */
    led_init();
    led_set(false);
    doors_gpio_init();
    ESP_ERROR_CHECK(fan_control_start(fan_state_report));

    /* Surveillance du bouton BOOT : appui long -> bascule en mode config */
    app_config_start_button_task();

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    if (xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
