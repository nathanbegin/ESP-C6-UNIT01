#include "fan_controller.h"

#include <stddef.h>

uint8_t fan_relay_mask(fan_state_t state)
{
    /* P1=000, P2=110, P3=111 in K1,K2,K3 order; K4 is independent. */
    return (state.mode == FAN_OFF ? 0u : 3u) |
           (state.mode == FAN_HIGH ? 4u : 0u) |
           (state.exchange ? 8u : 0u);
}

static bool release_all(fan_controller_t *fan)
{
    bool ok = true;
    /* Open exchange first. Attempt every output even if one write fails. */
    const unsigned order[] = {3, 0, 1, 2};
    for (unsigned i = 0; i < 4; ++i) {
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
    if (target.mode == fan->state.mode && target.exchange == fan->state.exchange) {
        return true;
    }

    if (target.mode != fan->state.mode) {
        /* Open the bypass before changing the selected resistance. */
        if (!fan->io.write_relay(fan->io.context, 3, false)) goto failed;
        fan->io.settle(fan->io.context);
        /* P1 is the transition position. Isolate K3 before moving it. */
        if (!fan->io.write_relay(fan->io.context, 0, false)) goto failed;
        if (!fan->io.write_relay(fan->io.context, 1, false)) goto failed;
        fan->io.settle(fan->io.context);
        if (!fan->io.write_relay(fan->io.context, 2, target.mode == FAN_HIGH)) goto failed;
        fan->io.settle(fan->io.context);
        if (!fan->io.write_relay(fan->io.context, 1, target.mode != FAN_OFF)) goto failed;
        fan->io.settle(fan->io.context);
        if (!fan->io.write_relay(fan->io.context, 0, target.mode != FAN_OFF)) goto failed;
        fan->io.settle(fan->io.context);
    }
    if (!fan->io.write_relay(fan->io.context, 3, target.exchange)) goto failed;
    fan->io.settle(fan->io.context);
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
        if (on) target.mode = FAN_LOW;
        else if (target.mode == FAN_LOW) target.mode = FAN_OFF;
        break;
    case FAN_SWITCH_HIGH:
        if (on) target.mode = FAN_HIGH;
        else if (target.mode == FAN_HIGH) target.mode = FAN_OFF;
        break;
    case FAN_SWITCH_EXCHANGE:
        target.exchange = on;
        break;
    default:
        return false;
    }
    return fan_controller_apply(fan, target);
}
