#pragma once

#include <stdbool.h>
#include <stdint.h>

/* États logiques de ventilation. */
typedef enum { FAN_OFF = 0, FAN_LOW = 1, FAN_HIGH = 2 } fan_mode_t;
typedef enum { FAN_SWITCH_LOW, FAN_SWITCH_HIGH, FAN_SWITCH_EXCHANGE } fan_switch_t;
typedef struct {
    fan_mode_t mode;
    bool exchange;
} fan_state_t;

typedef struct {
    bool (*write_relay)(void *context, unsigned relay, bool energized);
    void (*settle)(void *context);
    void *context;
} fan_io_t;

typedef struct {
    fan_state_t state;
    fan_io_t io;
    bool healthy;
} fan_controller_t;

/*
 * Indexes relais :
 *   0 = K1 Fan Low  (branche 21 kΩ)
 *   1 = K2 Fan High (branche 4 kΩ)
 *   2 = K3 échange extérieur (liaison directe J13-J14)
 *
 * Invariant : exchange=true n'est valide qu'en FAN_HIGH.
 */
bool fan_controller_init(fan_controller_t *fan, fan_io_t io);
bool fan_controller_apply(fan_controller_t *fan, fan_state_t target);
bool fan_controller_switch(fan_controller_t *fan, fan_switch_t control, bool on);
uint8_t fan_relay_mask(fan_state_t state);
