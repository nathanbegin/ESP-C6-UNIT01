/*
 * zb_door_sensors.c
 * ESP32-C6-DEV-KIT-N8 — Zigbee Router/End Device
 *
 * Endpoint 1 : On/Off Light  -> LED RGB embarquée (GPIO8)
 * Endpoint 2 : IAS Zone       -> reed porte 1 (GPIO2)
 * Endpoint 3 : IAS Zone       -> reed porte 2 (GPIO3)
 * Endpoint 4 : IAS Zone       -> reed porte 3 (GPIO4)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "led_strip.h"

#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zb_door_sensors.h"
#include "app_config.h"

static const char *TAG = "ZB_DOOR";

static volatile bool s_zb_started = false;

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

/* ---------- Reed switches : table de correspondance ---------- */
typedef struct {
    uint8_t  endpoint;
    gpio_num_t gpio;
    bool     last_open;      /* dernier état rapporté : true = porte ouverte */
    TickType_t last_change;  /* pour l'anti-rebond */
} door_t;

static door_t s_doors[DOOR_COUNT] = {
    { HA_DOOR1_ENDPOINT, DOOR1_GPIO, false, 0 },
    { HA_DOOR2_ENDPOINT, DOOR2_GPIO, false, 0 },
    { HA_DOOR3_ENDPOINT, DOOR3_GPIO, false, 0 },
};

static QueueHandle_t s_gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t idx = (uint32_t)arg;
    xQueueSendFromISR(s_gpio_evt_queue, &idx, NULL);
}

/* Lecture brute : porte OUVERTE si niveau HAUT (contact ouvert, pull-up) */
static inline bool door_is_open(gpio_num_t g)
{
    return gpio_get_level(g) == 1;
}

/* Met à jour l'attribut Zone Status d'un endpoint IAS Zone.
 * bit0 (Alarm1) = 1 -> ouvert, 0 -> fermé. */

static void door_report(door_t *d, bool open)
{
    uint16_t zone_status = open ? 0x0001 : 0x0000;

    if (!s_zb_started) {
        d->last_open = open;
        ESP_LOGI(TAG, "Porte EP%d (init, pré-Zigbee) -> %s",
                 d->endpoint, open ? "OUVERTE" : "FERMEE");
        return;
    }

    esp_zb_lock_acquire(portMAX_DELAY);

    /* 1. mettre à jour l'attribut local ZoneStatus */
    esp_zb_zcl_set_attribute_val(
        d->endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_IAS_ZONE,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_IAS_ZONE_ZONESTATUS_ID,
        &zone_status,
        false);

    /* 2. envoyer la Zone Status Change Notification au coordinateur (addr 0x0000) */
    esp_zb_zcl_ias_zone_status_change_notif_cmd_t cmd = {0};
    cmd.zcl_basic_cmd.src_endpoint        = d->endpoint;
    cmd.zcl_basic_cmd.dst_endpoint        = 1;            /* endpoint du coordinateur */
    cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;     /* le coordinateur */
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd.zone_status  = zone_status;
    cmd.extend_status = 0;
    cmd.zone_id      = (uint8_t)(d->endpoint - HA_DOOR1_ENDPOINT); /* 0,1,2 */
    cmd.delay        = 0;
    esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);

    esp_zb_lock_release();

    ESP_LOGI(TAG, "Porte EP%d -> %s (notif envoyée)",
             d->endpoint, open ? "OUVERTE" : "FERMEE");
}

/* Tâche qui traite les interruptions GPIO avec anti-rebond */
static void door_task(void *arg)
{
    uint32_t idx;
    /* État initial au démarrage */
    for (int i = 0; i < DOOR_COUNT; i++) {
        s_doors[i].last_open = door_is_open(s_doors[i].gpio);
        door_report(&s_doors[i], s_doors[i].last_open);
    }

    while (1) {
        if (xQueueReceive(s_gpio_evt_queue, &idx, portMAX_DELAY)) {
            door_t *d = &s_doors[idx];
            TickType_t now = xTaskGetTickCount();
            if ((now - d->last_change) < pdMS_TO_TICKS(DOOR_DEBOUNCE_MS)) {
                continue; /* rebond, on ignore */
            }
            vTaskDelay(pdMS_TO_TICKS(DOOR_DEBOUNCE_MS)); /* laisse le signal se stabiliser */
            bool open = door_is_open(d->gpio);
            if (open != d->last_open) {
                d->last_open = open;
                d->last_change = now;
                door_report(d, open);
            }
        }
    }
}

static void doors_gpio_init(void)
{
    s_gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_config_t io = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = (1ULL << DOOR1_GPIO) |
                        (1ULL << DOOR2_GPIO) |
                        (1ULL << DOOR3_GPIO),
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (uint32_t i = 0; i < DOOR_COUNT; i++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_doors[i].gpio,
                                             gpio_isr_handler, (void *)i));
    }
    xTaskCreate(door_task, "door_task", 4096, NULL, 5, NULL);
}

/* ---------- Callback ZCL : commandes On/Off pour la LED ---------- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                   const void *message)
{
    if (cb_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        const esp_zb_zcl_set_attr_value_message_t *m = message;
        if (m->info.dst_endpoint == HA_LED_ENDPOINT &&
            m->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
            m->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
            m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
            bool on = *(bool *)m->attribute.data.value;
            led_set(on);
        }
    }
    return ESP_OK;
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
            .zone_status = 0,
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
	    s_zb_started = true;
            ESP_LOGI(TAG, "Démarrage OK (%s)",
                     sig_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START ?
                     "première fois" : "reboot");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Recherche d'un réseau à rejoindre...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Déjà sur un réseau.");
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
            esp_zb_ieee_addr_t ext_pan;
            esp_zb_get_extended_pan_id(ext_pan);
            ESP_LOGI(TAG, "Rejoint le réseau. Canal: %d",
                     esp_zb_get_current_channel());
        } else {
            ESP_LOGW(TAG, "Steering échoué (%s), réessai...",
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
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
    esp_zb_device_register(ep_list);

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    /* NVS doit être prête avant de lire le mode de démarrage */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

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

    /* Surveillance du bouton BOOT : appui long -> bascule en mode config */
    app_config_start_button_task();

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL);
}
