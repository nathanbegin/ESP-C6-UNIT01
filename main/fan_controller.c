#include "fan_controller.h"

#include <stddef.h>

static int active_relay(fan_state_t state)
{
    if (state.exchange) return 2;       /* K3 : liaison directe J13-J14 */
    if (state.mode == FAN_LOW) return 0;  /* K1 : 21 kΩ */
    if (state.mode == FAN_HIGH) return 1; /* K2 : 4 kΩ */
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

static bool release_all(fan_controller_t *fan)
{
    bool ok = true;
    /* Ouvrir les trois contacts. Tenter chaque sortie même si une écriture échoue. */
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
    if (!fan || !fan->healthy || !state_valid(target)) return false;

    if (target.mode == fan->state.mode && target.exchange == fan->state.exchange) {
        return true;
    }

    const int current = active_relay(fan->state);
    const int next = active_relay(target);

    /*
     * Les quatre états sont mutuellement exclusifs. Toute transition passe donc
     * par tous les contacts ouverts avant d'en fermer un autre :
     *   Off -> aucun relais
     *   Low -> K1 seul
     *   High -> K2 seul
     *   Exchange -> K3 seul
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
            /* Échange extérieur = K3 seul, liaison directe J13-J14. */
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
