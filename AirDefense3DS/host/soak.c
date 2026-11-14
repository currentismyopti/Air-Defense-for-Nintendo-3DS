/* Headless soak test: a simple bot plays the game for a long time under sanitizers. */
#include "../src/game.h"
#include <stdlib.h>

static void bot(void) {
    /* bases */
    for (int s = 1; s < N_SITE; s++)
        if (!G.sites[s].built && unlocked(site_specs[s].unlock) && G.money > site_price(s) + 800) build_site(s);
    if (total_repair_cost() > 0 && G.money > total_repair_cost() + 300) repair_all_sites();
    /* jets */
    for (int k = N_JET - 1; k >= 0; k--)
        if (G.money > jet_specs[k].cost + 1500 && (rng_u32() & 3) == 0) buy_jet(k);
    /* upgrades */
    for (int u = 0; u < N_UPG; u++) {
        int c = upgrade_cost(u);
        if (c > 0 && G.money > c * 3 && (rng_u32() & 7) == 0) buy_upgrade(u);
    }
    /* weapons: best affordable */
    for (int k = N_SHOP_DEF - 1; k >= 0; k--) {
        if (!unlocked(def_specs[k].unlock) || G.money < def_specs[k].cost + 200) continue;
        if (!begin_placement(k)) continue;
        bool ok = false;
        for (int tries = 0; tries < 60 && !ok; tries++) {
            int s = irr(0, N_SITE - 1);
            if (!G.sites[s].built || site_specs[s].deploy_r <= 0) continue;
            float a = frr(0, 6.28f), r = frr(60, site_specs[s].deploy_r);
            G.ghost_x = site_specs[s].x + cosf(a) * r;
            G.ghost_y = site_specs[s].y + sinf(a) * r;
            if (!ghost_problem()) ok = confirm_placement();
        }
        cancel_placement();
        break;
    }
}

int main(int argc, char **argv) {
    int diff = argc > 1 ? atoi(argv[1]) : DF_MEDIUM;
    int minutes = argc > 2 ? atoi(argv[2]) : 30;
    rng_seed(argc > 3 ? (u32)atoi(argv[3]) : 12345);
    catalog_init();
    for (int i = 0; i < N_SHOP_DEF; i++) printf("%-18s $%d\n", def_specs[i].name, def_specs[i].cost);
    game_start(diff);
    if (creative()) { G.auto_waves = true; G.indestructible = false; }
    int frames = minutes * 60 * 60, last_log = 0;
    for (int f = 0; f < frames && !G.over; f++) {
        sim_update(1.0f / 60);
        if (f % 60 == 0) bot();
        if (creative() && f % 3600 == 0) { set_threat_level(G.level + 1); creative_raid(); creative_strike(M_ICBM); }
        if (f % (60 * 60 * 5) == 0) {
            int nd = 0, nj = 0;
            for (int i = 0; i < MAX_DEF; i++) nd += G.def[i].active;
            for (int i = 0; i < MAX_JETS; i++) nj += G.jets[i].alive;
            printf("[%2d min] lv %d $%d kills %d icpt %d def %d jets %d lost %d hostiles %d hq %.0f/%.0f\n",
                   f / 3600, G.level, G.money, G.kills, G.intercepts, nd, nj, G.aircraft_lost, G.hostiles,
                   G.sites[0].hp, G.sites[0].max_hp);
        }
        (void)last_log;
    }
    printf("END over=%d lv %d kills %d intercepts %d shots %d hits %d lost %d time %.0fs\n", G.over, G.level, G.kills,
           G.intercepts, G.shots, G.shot_hits, G.aircraft_lost, G.time);
    for (int i = 0; i < 12; i++) { const LogEntry *e = log_at(i); if (e) printf("  %s\n", e->text); }
    return 0;
}
