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

static void assert_safe(const simulator_t *sim)
{
    /* Au plus un des quatre relais peut être fermé à tout instant. */
    unsigned active = (unsigned)sim->coil[0] + (unsigned)sim->coil[1] +
                      (unsigned)sim->coil[2] + (unsigned)sim->coil[3];
    assert(active <= 1);
}

static bool write_coil(void *context, unsigned relay, bool on)
{
    simulator_t *sim = context;
    assert(relay < 4);
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
    return sim->coil[0] | sim->coil[1] << 1 | sim->coil[2] << 2 | sim->coil[3] << 3;
}

static fan_controller_t create(simulator_t *sim)
{
    memset(sim, 0, sizeof(*sim));
    fan_controller_t fan;
    assert(fan_controller_init(&fan, (fan_io_t){write_coil, settle, sim}));
    assert(mask(sim) == 8); /* OFF = K4 seul */
    assert(fan.state.mode == FAN_OFF && !fan.state.exchange);
    return fan;
}

int main(void)
{
    const fan_state_t states[] = {
        {FAN_OFF, false},
        {FAN_LOW, false},
        {FAN_HIGH, false},
        {FAN_OFF, true},
    };
    const unsigned expected[] = {8, 1, 2, 4};

    /* Toutes les 16 transitions gardent au plus un relais actif. */
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

    /* OFF -> Low : K4 s'ouvre puis K1 se ferme. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_LOW, true));
    assert(fan.state.mode == FAN_LOW && !fan.state.exchange);
    assert(mask(&sim) == 1);

    /* Low -> High : K1 s'ouvre puis K2 se ferme. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, true));
    assert(fan.state.mode == FAN_HIGH && !fan.state.exchange);
    assert(mask(&sim) == 2);

    /* High -> Exchange : K2 s'ouvre puis K3 se ferme. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, true));
    assert(fan.state.mode == FAN_OFF && fan.state.exchange);
    assert(mask(&sim) == 4);

    /* Un OFF périmé sur High n'affecte pas Exchange. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, false));
    assert(mask(&sim) == 4);

    /* Exchange OFF revient à l'état OFF physique : K4 seul. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_EXCHANGE, false));
    assert(fan.state.mode == FAN_OFF && !fan.state.exchange);
    assert(mask(&sim) == 8);

    /* OFF -> High puis High OFF revient également à K4. */
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, true));
    assert(mask(&sim) == 2);
    assert(fan_controller_switch(&fan, FAN_SWITCH_HIGH, false));
    assert(mask(&sim) == 8);

    /* Les états combinant Exchange avec une vitesse sont refusés. */
    unsigned before = sim.writes;
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_LOW, true}));
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_HIGH, true}));
    assert(!fan_controller_apply(&fan, (fan_state_t){99, false}));
    assert(!fan_controller_switch(&fan, (fan_switch_t)99, true));
    assert(sim.writes == before);

    /* Une erreur pendant Exchange -> Low récupère vers OFF/K4 si possible. */
    for (unsigned stage = 1; stage <= 2; ++stage) {
        fan = create(&sim);
        assert(fan_controller_apply(&fan, (fan_state_t){FAN_OFF, true}));
        sim.check_transition = true;
        sim.fail_at = sim.writes + stage;
        assert(!fan_controller_apply(&fan, (fan_state_t){FAN_LOW, false}));
        assert(fan.healthy && mask(&sim) == 8);
        assert(fan.state.mode == FAN_OFF && !fan.state.exchange);
    }

    /* Une erreur à la fermeture de K4 lors d'un retour OFF récupère aussi vers K4. */
    fan = create(&sim);
    assert(fan_controller_apply(&fan, (fan_state_t){FAN_HIGH, false}));
    sim.check_transition = true;
    sim.fail_at = sim.writes + 2; /* ouverture K2 réussit, première fermeture K4 échoue */
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_OFF, false}));
    assert(fan.healthy && mask(&sim) == 8);

    /* Si les GPIO échouent durablement, le contrôleur se bloque en défaut. */
    fan = create(&sim);
    sim.always_fail = true;
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_OFF, true}));
    assert(!fan.healthy);
    assert(!fan_controller_apply(&fan, (fan_state_t){FAN_LOW, false}));

    puts("PASS: 16 transitions, K1=10k K2=4k K3=Exchange K4=OFF21k, exclusivité, erreurs GPIO");
    return 0;
}
