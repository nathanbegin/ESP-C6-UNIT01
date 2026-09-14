/*
 * zb_door_sensors.h
 * ESP32-C6-DEV-KIT-N8 — Zigbee End Device
 * 1 LED On/Off (WS2812 embarquée, GPIO8) + 3 contacts de porte (IAS Zone)
 */
#pragma once

#include "esp_zigbee_core.h"

/* ---------- Rôle réseau ---------- */
/* Alimenté secteur -> on peut être ROUTER (meilleur pour le maillage).
 * Si tu préfères End Device, mets false. */
#define DOOR_DEVICE_AS_ROUTER   true

#if DOOR_DEVICE_AS_ROUTER
#define ESP_ZB_ZED_CONFIG()                                     \
    {                                                           \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,               \
        .install_code_policy = false,                           \
        .nwk_cfg.zczr_cfg = {                                   \
            .max_children = 10,                                 \
        },                                                      \
    }
#else
#define ESP_ZB_ZED_CONFIG()                                     \
    {                                                           \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                   \
        .install_code_policy = false,                           \
        .nwk_cfg.zed_cfg = {                                    \
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,        \
            .keep_alive = 3000,                                 \
        },                                                      \
    }
#endif

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }

/* ---------- GPIO ---------- */
#define LED_STRIP_GPIO          8       /* WS2812 RGB embarquée du DevKitC */
#define LED_STRIP_LEDS          1

/* Reed switches : un fil au GPIO, l'autre à GND. Pull-up interne activé.
 * Niveau BAS  = contact fermé (aimant present) = porte FERMÉE.
 * Niveau HAUT = contact ouvert                  = porte OUVERTE. */
#define DOOR1_GPIO              2
#define DOOR2_GPIO              3
#define DOOR3_GPIO              4
#define DOOR_COUNT             3

/* ---------- Endpoints ---------- */
#define HA_LED_ENDPOINT         1
#define HA_DOOR1_ENDPOINT       2
#define HA_DOOR2_ENDPOINT       3
#define HA_DOOR3_ENDPOINT       4

/* ---------- Divers Zigbee ---------- */
#define INSTALLCODE_POLICY_ENABLE       false
#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define ED_KEEP_ALIVE                   3000        /* ms */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

/* Anti-rebond logiciel pour les reed switches (ms) */
#define DOOR_DEBOUNCE_MS                50

/* Identification fabricant / modèle (affiché dans Z2MQTT) */
#define MANUFACTURER_NAME       "\x0D""NathanSensors"   /* len = 0x0D = 13 chars */
#define MODEL_IDENTIFIER        "\x0D""ESP-C6-UNIT01"   /* len = 0x0D = 13 chars */