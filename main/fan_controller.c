#include "fan_controller.h"

#include <stddef.h>

uint8_t fan_relay_mask(fan_state_t state)
{
    uint8_t mask = 0;
    if (state.mode == FAN_LOW) mask |= 1u;       /* K1 */
    if (state.mode == FAN_HIGH) mask |= 2u;      /* K2 */
    if (state.mode == FAN_HIGH && state.exchange) mask |= 4u; /* K3 */
    return mask;
}

static bool release_all(fan_controller_t *fan)
{
    bool ok = true;
    /* Ouvrir d'abord l'échange, puis les deux branches de vitesse. */
    const unsigned order[] = {2, 0, 1};
    for (unsigned i = 0; i < 3; ++i) {
        if (!fan->io.write_relay(fan->io.context, order[i], false)) ok = false;
    }
    fan->healthy = ok;
    if (ok) fan->state = (fan_state_t){FAN_OFF, false};
    return ok;
}

bool fan_controller_init(fan_controller_t *fan, fan_io_t io)
{
    if (!fan || !io.write_relay || !io.settle) return false;
    *fan = (fan_controller_t){.state = {FAN_OFF, false}, .io = io};
    return release_all(fan);
}

bool fan_controller_apply(fan_controller_t *fan, fan_state_t target)
{
    if (!fan || !fan->healthy || target.mode < FAN_OFF || target.mode > FAN_HIGH) {
        return false;
    }
    /* L'échange extérieur n'est permis qu'en haute vitesse. */
    if (target.exchange && target.mode != FAN_HIGH) return false;

    if (target.mode == fan->state.mode && target.exchange == fan->state.exchange) {
        return true;
    }

    const bool mode_changed = target.mode != fan->state.mode;

    /* Break-before-make : retirer le bypass avant tout changement de vitesse. */
    if (fan->state.exchange && (!target.exchange || mode_changed)) {
        if (!fan->io.write_relay(fan->io.context, 2, false)) goto failed;
        fan->io.settle(fan->io.context);
    }

    if (mode_changed) {
        /* Ouvrir la branche actuellement sélectionnée avant d'en fermer une autre. */
        if (fan->state.mode == FAN_LOW) {
            if (!fan->io.write_relay(fan->io.context, 0, false)) goto failed;
            fan->io.settle(fan->io.context);
        } else if (fan->state.mode == FAN_HIGH) {
            if (!fan->io.write_relay(fan->io.context, 1, false)) goto failed;
            fan->io.settle(fan->io.context);
        }

        if (target.mode == FAN_LOW) {
            if (!fan->io.write_relay(fan->io.context, 0, true)) goto failed;
            fan->io.settle(fan->io.context);
        } else if (target.mode == FAN_HIGH) {
            if (!fan->io.write_relay(fan->io.context, 1, true)) goto failed;
            fan->io.settle(fan->io.context);
        }
    }

    /* Le bypass ne peut être fermé qu'après sélection de Fan High. */
    if (target.exchange && !fan->state.exchange) {
        if (!fan->io.write_relay(fan->io.context, 2, true)) goto failed;
        fan->io.settle(fan->io.context);
    }

    fan->state = target;
    return true;

failed:
    release_all(fan);
    return false;
}

bool fan_controller_switch(fan_controller_t *fan, fan_switch_t control, bool on)
{
    if (!fan) return false;
    fan_state_t target = fan->state;

    switch (control) {
    case FAN_SWITCH_LOW:
        if (on) {
            target.mode = FAN_LOW;
            target.exchange = false;
        } else if (target.mode == FAN_LOW) {
            target.mode = FAN_OFF;
            target.exchange = false;
        }
        break;

    case FAN_SWITCH_HIGH:
        if (on) {
            target.mode = FAN_HIGH;
        } else if (target.mode == FAN_HIGH) {
            target.mode = FAN_OFF;
            target.exchange = false;
        }
        break;

    case FAN_SWITCH_EXCHANGE:
        if (on) {
            /* La machine échange uniquement en haute vitesse. */
            target.mode = FAN_HIGH;
            target.exchange = true;
        } else {
            target.exchange = false;
        }
        break;

    default:
        return false;
    }

    return fan_controller_apply(fan, target);
}
