#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fan_controller.h"

typedef struct {
    bool coil[4];
    unsigned writes;
    unsigned delays;
    unsigned fail_at;
    bool always_fail;
    bool check_transition;
} simulator_t;

static bool write_coil(void *context, unsigned relay, bool on)
{
    simulator_t *sim = context;
    assert(relay < 4);
    ++sim->writes;
    if (sim->always_fail || sim->writes == sim->fail_at) return false;
    if (sim->check_transition && relay == 2 && sim->coil[2] != on) {
        assert(!sim->coil[1]); /* K3 cannot switch while K2 selects its COM. */
        assert(!sim->coil[3]); /* No exchange bypass during speed selection. */
        assert(sim->delays >= 2);
    }
    sim->coil[relay] = on;
    return true;
}

static void settle(void *context)
{
    ++((simulator_t *)context)->delays;
}

static unsigned mask(const simulator_t *sim)
{
    return sim->coil[0] | sim->coil[1] << 1 | sim->coil[2] << 2 | sim->coil[3] << 3;
}

static fan_controller_t create(simulator_t *sim)
{
    memset(sim, 0, sizeof(*sim));
    fan_controller_t fan;
    assert(fan_controller_init(&fan, (fan_io_t){write_coil, settle, sim}));
    assert(mask(sim) == 0);
    return fan;
}

int main(void)
{
    const fan_state_t states[] = {
        {FAN_OFF, false}, {FAN_OFF, true}, {FAN_LOW, false},
        {FAN_LOW, true}, {FAN_HIGH, false}, {FAN_HIGH, true},
    };
    const unsigned expected[] = {0, 8, 3, 11, 7, 15};
    for (unsigned from = 0; from < 6; ++from) {
        for (unsigned to = 0; to < 6; ++to) {
            simulator_t sim;
            fan_controller_t fan = create(&sim);
            assert(fan_controller_apply(&fan, states[from]));
            sim.delays = 0;
            sim.check_transition = true;
            assert(fan_controller_apply(&fan, states[to]));
            assert(mask(&sim) == expected[to]);
            assert(fan_relay_mask(fan.state) == expected[to]);
            assert(fan.state.mode == states[to].mode && fan.state.exchange == states[to].exchange);
            if (states[from].mode != states[to].mode) assert(sim.delays == 6);
        }
    }

    simulator_t sim;
    fan_controller_t fan = create(&sim);
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, true));
    assert(fan_controller_switch(&fan, FAN_SWITCH_LOW, true));
    assert(mask(&sim) == 11);
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, true));
    assert(mask(&sim) == 15);
    assert(fan_controller_switch(&fan, FAN_SWITCH_LOW, false)); /* stale OFF */
    assert(mask(&sim) == 15);
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, false));
    assert(mask(&sim) == 8); /* OFF preserves the independent exchange request. */
    unsigned before = sim.writes;
    assert(!fan_controller_apply(&fan, (fan_state_t){99, true}));
    assert(!fan_controller_switch(&fan, (fan_switch_t)99, true));
    assert(sim.writes == before);

    /* Fail each stage of a transition: attempt to release every coil. */
    for (unsigned stage = 1; stage <= 7; ++stage) {
        fan = create(&sim);
        assert(fan_controller_apply(&fan, (fan_state_t){FAN_HIGH, true}));
        sim.fail_at = sim.writes + stage;
        assert(!fan_controller_apply(&fan, (fan_state_t){FAN_LOW, false}));
        assert(fan.healthy && mask(&sim) == 0);
        assert(fan.state.mode == FAN_OFF && !fan.state.exchange);
    }
    fan = create(&sim);
    sim.always_fail = true;
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_HIGH, true}));
    assert(!fan.healthy);
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_LOW, false}));
    puts("PASS: 36 transitions, interlocking order, exclusive modes, stale OFF, invalid input, GPIO failures");
    return 0;
}
