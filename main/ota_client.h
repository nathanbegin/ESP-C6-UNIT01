#pragma once
#include "esp_zigbee_core.h"

/* Keep these IDs stable; increment FILE_VERSION for every released image. */
#define APP_OTA_MANUFACTURER 0x1234
#define APP_OTA_IMAGE_TYPE 0x0001
#define APP_OTA_FILE_VERSION 0x00000001

esp_err_t ota_client_add_cluster(esp_zb_cluster_list_t *clusters);
esp_err_t ota_client_action(esp_zb_core_action_callback_id_t id, const void *message);
