#pragma once

#include "esp_err.h"
#include "fan_controller.h"

#define FAN_RELAY_LOW_GPIO       6
#define FAN_RELAY_HIGH_GPIO      7
#define FAN_RELAY_EXCHANGE_GPIO 10
#define FAN_RELAY_OFF_GPIO      11
#define FAN_RELAY_COUNT          4

typedef void (*fan_state_callback_t)(fan_state_t state);

/* Initialize relay outputs in the safe OFF state: K4 active, K1/K2/K3 released. */
esp_err_t fan_control_init(void);
esp_err_t fan_control_start(fan_state_callback_t callback);

/* Nonblocking, serialized requests. No relay is directly exposed to Zigbee. */
esp_err_t fan_set_mode(fan_mode_t mode);
esp_err_t outdoor_exchange_set(bool enabled);
esp_err_t fan_control_set_switch(fan_switch_t control, bool on);
void fan_control_request_report(void);
