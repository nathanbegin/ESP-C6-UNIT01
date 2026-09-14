#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Logical coil states, independent of the electrical HIGH/LOW polarity. */
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

/* Call from one owner task. Relay indexes 0..3 correspond to K1..K4. */
bool fan_controller_init(fan_controller_t *fan, fan_io_t io);
bool fan_controller_apply(fan_controller_t *fan, fan_state_t target);
bool fan_controller_switch(fan_controller_t *fan, fan_switch_t control, bool on);
uint8_t fan_relay_mask(fan_state_t state);
