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
 *   0 = K1 Fan Low  (branche 10 kΩ)
 *   1 = K2 Fan High (branche 4 kΩ)
 *   2 = K3 échange extérieur (liaison directe J13-J14, ≈ 0 Ω)
 *   3 = K4 unité OFF (branche 21 kΩ)
 *
 * États physiques permis, strictement exclusifs :
 *   {FAN_OFF,  false} -> K4 seul
 *   {FAN_LOW,  false} -> K1 seul
 *   {FAN_HIGH, false} -> K2 seul
 *   {FAN_OFF,  true } -> K3 seul (échange extérieur)
 *
 * Un état normal a exactement un relais actif. Pendant une transition
 * break-before-make, tous les relais peuvent être momentanément ouverts.
 */
bool fan_controller_init(fan_controller_t *fan, fan_io_t io);
bool fan_controller_apply(fan_controller_t *fan, fan_state_t target);
bool fan_controller_switch(fan_controller_t *fan, fan_switch_t control, bool on);
uint8_t fan_relay_mask(fan_state_t state);
