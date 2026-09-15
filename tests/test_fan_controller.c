#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fan_controller.h"

typedef struct {
    bool coil[3];
    unsigned writes;
    unsigned delays;
    unsigned fail_at;
    bool always_fail;
    bool check_transition;
} simulator_t;

static void assert_safe(const simulator_t *sim)
{
    /* Low et High ne doivent jamais être fermés ensemble. */
    assert(!(sim->coil[0] && sim->coil[1]));
    /* L'échange n'est permis que lorsque High est déjà fermé. */
    if (sim->coil[2]) {
        assert(!sim->coil[0]);
        assert(sim->coil[1]);
    }
}

static bool write_coil(void *context, unsigned relay, bool on)
{
    simulator_t *sim = context;
    assert(relay < 3);
    ++sim->writes;
    if (sim->always_fail || sim->writes == sim->fail_at) return false;
    sim->coil[relay] = on;
    if (sim->check_transition) assert_safe(sim);
    return true;
}

static void settle(void *context)
{
    ++((simulator_t *)context)->delays;
}

static unsigned mask(const simulator_t *sim)
{
    return sim->coil[0] | sim->coil[1] << 1 | sim->coil[2] << 2;
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
        {FAN_OFF, false},
        {FAN_LOW, false},
        {FAN_HIGH, false},
        {FAN_HIGH, true},
    };
    const unsigned expected[] = {0, 1, 2, 6};

    /* Toutes les 16 transitions entre états valides respectent l'interverrouillage. */
    for (unsigned from = 0; from < 4; ++from) {
        for (unsigned to = 0; to < 4; ++to) {
            simulator_t sim;
            fan_controller_t fan = create(&sim);
            assert(fan_controller_apply(&fan, states[from]));
            sim.delays = 0;
            sim.check_transition = true;
            assert(fan_controller_apply(&fan, states[to]));
            assert_safe(&sim);
            assert(mask(&sim) == expected[to]);
            assert(fan_relay_mask(fan.state) == expected[to]);
            assert(fan.state.mode == states[to].mode);
            assert(fan.state.exchange == states[to].exchange);
        }
    }

    simulator_t sim;
    fan_controller_t fan = create(&sim);
    sim.check_transition = true;

    /* Exchange ON force d'abord Fan High puis ferme K3. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, true));
    assert(fan.state.mode == FAN_HIGH && fan.state.exchange);
    assert(mask(&sim) == 6);

    /* Passer à Low coupe l'échange avant de changer de branche. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_LOW, true));
    assert(fan.state.mode == FAN_LOW && !fan.state.exchange);
    assert(mask(&sim) == 1);

    /* Fan High reste exclusif; OFF périmé sur Low ne l'affecte pas. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, true));
    assert(mask(&sim) == 2);
    assert(fan_controller_switch(&fan, FAN_SWITCH_LOW, false));
    assert(mask(&sim) == 2);

    /* Exchange ON puis OFF conserve High; High OFF arrête aussi l'échange. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, true));
    assert(mask(&sim) == 6);
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, false));
    assert(mask(&sim) == 2);
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, true));
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, false));
    assert(mask(&sim) == 0);
    assert(fan.state.mode == FAN_OFF && !fan.state.exchange);

    /* Les états directs incohérents sont refusés sans toucher aux sorties. */
    unsigned before = sim.writes;
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_LOW, true}));
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_OFF, true}));
    assert(!fan_controller_apply(&fan, (fan_state_t){99, false}));
    assert(!fan_controller_switch(&fan, (fan_switch_t)99, true));
    assert(sim.writes == before);

    /* Une erreur pendant High+Exchange -> Low entraîne une remise au repos. */
    for (unsigned stage = 1; stage <= 3; ++stage) {
        fan = create(&sim);
        assert(fan_controller_apply(&fan, (fan_state_t){FAN_HIGH, true}));
        sim.check_transition = true;
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

    puts("PASS: 16 transitions, 3 relais SPST, interverrouillage High/Exchange, erreurs GPIO");
    return 0;
}
