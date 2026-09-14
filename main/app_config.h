/*
 * app_config.h
 * Machine à états de démarrage : NORMAL (Zigbee) vs CONFIG (portail Wi-Fi)
 * + bouton BOOT (GPIO9) pour basculer en mode config.
 */
#pragma once

#include <stdbool.h>

/* ---------- Bouton de configuration ---------- */
#define CONFIG_BUTTON_GPIO      9       /* bouton BOOT du DevKitC */
#define CONFIG_BUTTON_HOLD_MS   3000    /* appui long de 3 s */

/* ---------- Réseau de configuration (SoftAP) ---------- */
#define CONFIG_AP_SSID          "NathanSensors-Setup"
#define CONFIG_AP_CHANNEL       1
#define CONFIG_AP_MAX_CONN      4

/* ---------- Mode de démarrage stocké en NVS ---------- */
typedef enum {
    BOOT_MODE_NORMAL = 0,   /* démarre Zigbee */
    BOOT_MODE_CONFIG = 1,   /* démarre le portail Wi-Fi */
} boot_mode_t;

/* Lit le mode de démarrage depuis la NVS (défaut : NORMAL). */
boot_mode_t app_config_get_boot_mode(void);

/* Écrit le mode de démarrage en NVS. */
void app_config_set_boot_mode(boot_mode_t mode);

/* Démarre la surveillance du bouton (appui long -> bascule en CONFIG + reboot).
 * À appeler en mode NORMAL, après l'init Zigbee. */
void app_config_start_button_task(void);

/* Lance le portail Wi-Fi de configuration (mode CONFIG).
 * Ne retourne pas : l'appareil reste en mode config jusqu'au reboot. */
void app_config_run_portal(void);
