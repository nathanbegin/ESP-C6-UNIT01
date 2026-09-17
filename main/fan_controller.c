#include "fan_controller.h"

#include <stddef.h>

static int active_relay(fan_state_t state)
{
    if (state.exchange) return 2;          /* K3 : liaison directe J13-J14 */
    if (state.mode == FAN_LOW) return 0;   /* K1 : 10 kΩ */
    if (state.mode == FAN_HIGH) return 1;  /* K2 : 4 kΩ */
    if (state.mode == FAN_OFF) return 3;   /* K4 : 21 kΩ / unité OFF */
    return -1;
}

static bool state_valid(fan_state_t state)
{
    if (state.mode < FAN_OFF || state.mode > FAN_HIGH) return false;
    /* Exchange est un quatrième état distinct : K3 seul, sans Low ni High. */
    if (state.exchange && state.mode != FAN_OFF) return false;
    return true;
}

uint8_t fan_relay_mask(fan_state_t state)
{
    if (!state_valid(state)) return 0;
    int relay = active_relay(state);
    return relay < 0 ? 0u : (uint8_t)(1u << relay);
}

static bool recover_off(fan_controller_t *fan)
{
    bool released = true;

    /*
     * Remise en sécurité : ouvrir d'abord tous les relais. K4 ne doit être
     * fermé qu'après confirmation que K1/K2/K3 sont relâchés.
     */
    const unsigned order[] = {2, 0, 1, 3};
    for (unsigned i = 0; i < 4; ++i) {
        if (!fan->io.write_relay(fan->io.context, order[i], false)) released = false;
    }

    if (!released) {
        fan->healthy = false;
        return false;
    }

    fan->io.settle(fan->io.context);
    if (!fan->io.write_relay(fan->io.context, 3, true)) {
        fan->healthy = false;
        return false;
    }
    fan->io.settle(fan->io.context);

    fan->state = (fan_state_t){FAN_OFF, false};
    fan->healthy = true;
    return true;
}

bool fan_controller_init(fan_controller_t *fan, fan_io_t io)
{
    if (!fan || !io.write_relay || !io.settle) return false;
    *fan = (fan_controller_t){.state = {FAN_OFF, false}, .io = io, .healthy = false};
    return recover_off(fan);
}

bool fan_controller_apply(fan_controller_t *fan, fan_state_t target)
{
    if (!fan || !fan->healthy || !state_valid(target)) return false;

    if (target.mode == fan->state.mode && target.exchange == fan->state.exchange) {
        return true;
    }

    const int current = active_relay(fan->state);
    const int next = active_relay(target);

    /*
     * Les quatre états sont mutuellement exclusifs. Toute transition est
     * break-before-make : ouvrir l'état courant, attendre, puis fermer le
     * relais du nouvel état.
     *
     *   Off      -> K4 seul (21 kΩ)
     *   Low      -> K1 seul (10 kΩ)
     *   High     -> K2 seul (4 kΩ)
     *   Exchange -> K3 seul (≈ 0 Ω)
     */
    if (current >= 0 && current != next) {
        if (!fan->io.write_relay(fan->io.context, (unsigned)current, false)) goto failed;
        fan->io.settle(fan->io.context);
    }

    if (next >= 0 && next != current) {
        if (!fan->io.write_relay(fan->io.context, (unsigned)next, true)) goto failed;
        fan->io.settle(fan->io.context);
    }

    fan->state = target;
    return true;

failed:
    recover_off(fan);
    return false;
}

bool fan_controller_switch(fan_controller_t *fan, fan_switch_t control, bool on)
{
    if (!fan) return false;
    fan_state_t target = fan->state;

    switch (control) {
    case FAN_SWITCH_LOW:
        if (on) {
            target = (fan_state_t){FAN_LOW, false};
        } else if (target.mode == FAN_LOW && !target.exchange) {
            target = (fan_state_t){FAN_OFF, false};
        }
        break;

    case FAN_SWITCH_HIGH:
        if (on) {
            target = (fan_state_t){FAN_HIGH, false};
        } else if (target.mode == FAN_HIGH && !target.exchange) {
            target = (fan_state_t){FAN_OFF, false};
        }
        break;

    case FAN_SWITCH_EXCHANGE:
        if (on) {
            target = (fan_state_t){FAN_OFF, true};
        } else if (target.exchange) {
            target = (fan_state_t){FAN_OFF, false};
        }
        break;

    default:
        return false;
    }

    return fan_controller_apply(fan, target);
}
