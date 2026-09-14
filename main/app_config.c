/*
 * app_config.c
 * Incrément 1 : bouton BOOT (appui long) -> bascule en mode CONFIG (reboot).
 * En mode CONFIG : SoftAP "NathanSensors-Setup" + serveur web minimal
 * avec un bouton "Retour Zigbee".
 *
 * Le passage Zigbee <-> Wi-Fi se fait TOUJOURS par reboot (drapeau NVS),
 * jamais à chaud, pour éviter les conflits de radio 2.4 GHz.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "app_config.h"

static const char *TAG = "APP_CFG";

#define NVS_NAMESPACE   "appcfg"
#define NVS_KEY_BOOT    "boot_mode"

/* ============================================================
 *  Mode de démarrage (NVS)
 * ============================================================ */
boot_mode_t app_config_get_boot_mode(void)
{
    nvs_handle_t h;
    uint8_t v = BOOT_MODE_NORMAL;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_BOOT, &v);
        nvs_close(h);
    }
    return (boot_mode_t)v;
}

void app_config_set_boot_mode(boot_mode_t mode)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_BOOT, (uint8_t)mode);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "boot_mode écrit en NVS: %d", mode);
    } else {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS pour écrire boot_mode");
    }
}

/* ============================================================
 *  Bouton BOOT : appui long -> mode CONFIG + reboot
 * ============================================================ */
static void config_button_task(void *arg)
{
    gpio_config_t io = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
    };
    gpio_config(&io);

    ESP_LOGI(TAG, "Surveillance bouton BOOT (GPIO%d) — appui long %d ms = mode config",
             CONFIG_BUTTON_GPIO, CONFIG_BUTTON_HOLD_MS);

    while (1) {
        /* bouton appuyé = niveau bas */
        if (gpio_get_level(CONFIG_BUTTON_GPIO) == 0) {
            TickType_t pressed_at = xTaskGetTickCount();
            bool triggered = false;
            while (gpio_get_level(CONFIG_BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                if (!triggered &&
                    (xTaskGetTickCount() - pressed_at) >= pdMS_TO_TICKS(CONFIG_BUTTON_HOLD_MS)) {
                    triggered = true;
                    ESP_LOGW(TAG, "Appui long détecté -> bascule en mode CONFIG");
                    app_config_set_boot_mode(BOOT_MODE_CONFIG);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    esp_restart();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_config_start_button_task(void)
{
    xTaskCreate(config_button_task, "cfg_btn", 3072, NULL, 4, NULL);
}

/* ============================================================
 *  Portail Wi-Fi (mode CONFIG)
 * ============================================================ */

/* Page HTML minimale (incrément 1). L'incrément 2 enrichira ce contenu
 * avec l'attribution des GPIO et le bouton factory reset. */
static const char *PAGE_HTML =
    "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>NathanSensors — Configuration</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}"
    ".wrap{max-width:520px;margin:0 auto;padding:24px}"
    "h1{font-size:1.3rem}"
    ".card{background:#1b1e26;border:1px solid #2a2f3a;border-radius:12px;padding:18px;margin:16px 0}"
    ".btn{display:block;width:100%;padding:14px;border:0;border-radius:10px;"
    "font-size:1rem;font-weight:600;cursor:pointer;margin-top:10px}"
    ".primary{background:#3b82f6;color:#fff}"
    ".muted{color:#9aa3b2;font-size:.9rem}"
    "</style></head><body><div class='wrap'>"
    "<h1>NathanSensors — Configuration</h1>"
    "<div class='card'>"
    "<p>Vous êtes en <b>mode configuration</b>. Le Zigbee est arrêté tant que "
    "vous êtes ici.</p>"
    "<p class='muted'>L'attribution des GPIO et le factory reset seront ajoutés "
    "à l'étape suivante.</p>"
    "</div>"
    "<div class='card'>"
    "<form action='/exit' method='post'>"
    "<button class='btn primary' type='submit'>Terminer et revenir au Zigbee</button>"
    "</form>"
    "</div>"
    "</div></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, PAGE_HTML);
    return ESP_OK;
}

/* Bouton "Terminer" : repasse en mode NORMAL et reboot vers Zigbee. */
static esp_err_t exit_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body style='font-family:sans-serif;background:#0f1115;color:#e6e6e6;"
        "text-align:center;padding-top:60px'>"
        "<h2>Retour au mode Zigbee…</h2><p>L'appareil redémarre.</p></body></html>");

    ESP_LOGI(TAG, "Demande de sortie du mode config -> NORMAL + reboot");
    app_config_set_boot_mode(BOOT_MODE_NORMAL);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK; /* jamais atteint */
}

/* Portail captif minimal : toute requête inconnue renvoie la page racine. */
static esp_err_t captive_redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t exit_uri = {
            .uri = "/exit", .method = HTTP_POST, .handler = exit_post_handler };
        httpd_register_uri_handler(server, &exit_uri);

        /* portail captif : 404 -> redirige vers / */
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_redirect_handler);

        ESP_LOGI(TAG, "Serveur web démarré");
    } else {
        ESP_LOGE(TAG, "Échec démarrage serveur web");
    }
    return server;
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap = {
        .ap = {
            .ssid = CONFIG_AP_SSID,
            .ssid_len = strlen(CONFIG_AP_SSID),
            .channel = CONFIG_AP_CHANNEL,
            .max_connection = CONFIG_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,   /* réseau ouvert pour la config */
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP actif : SSID='%s' — connecte-toi puis ouvre http://192.168.4.1",
             CONFIG_AP_SSID);
}

void app_config_run_portal(void)
{
    ESP_LOGI(TAG, "=== MODE CONFIGURATION (Wi-Fi) ===");
    wifi_init_softap();
    start_webserver();
    /* On ne lance PAS Zigbee dans ce mode. La tâche reste vivante,
       le portail tourne jusqu'au reboot déclenché par /exit. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
