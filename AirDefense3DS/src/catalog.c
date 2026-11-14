/* Every weapon, aircraft, base and upgrade in the game. Distances are in original-layout units and
   are scaled to the map by catalog_init(). */
#include "game.h"

#define C(r, g, b) RGBF(r, g, b, 1.0f)

DefSpec def_specs[N_DEFSPEC] = {
    {"M163 Vulcan", "VADS", "20 mm radar-ranged gun. Cheap and quick, short reach.",
     WPN_GUN, SENS_RADAR, 250, 190, 0, 0.14f, 0.22f, 40, 5, 0, true, true, PRI_AIRCRAFT,
     0.15f, 0.35f, 35, 0.2f, 1, C(0.78f, 0.80f, 0.62f), 1, 0.45f, false, 0, false},
    {"Stinger Team", "MPD", "Shoulder-fired IR missiles. Immune to jamming, fooled by flares.",
     WPN_MISSILE, SENS_IR, 300, 250, 30, 0.55f, 1.8f, 4, 9, 430, true, false, PRI_AIRCRAFT,
     0, 0.5f, 25, 0, 1, C(0.95f, 0.62f, 0.35f), 1, 0.45f, false, 0, false},
    {"C-RAM Centurion", "CRAM", "Phalanx-derived gatling. Shreds rockets, bombs and missiles near the base.",
     WPN_GUN, SENS_RADAR, 210, 150, 0, 0.20f, 0.08f, 120, 7, 0, true, true, PRI_MUNITIONS,
     0.4f, 0.25f, 75, 0.2f, 2, C(1.0f, 0.42f, 0.38f), 1, 0.45f, false, 0, false},
    {"Skynex 35 mm", "SKX", "Airburst cannon with its own fire-control radar. Good all-rounder.",
     WPN_GUN, SENS_RADAR, 330, 230, 0, 0.22f, 0.30f, 36, 6, 0, true, true, PRI_ANY,
     0.55f, 0.3f, 50, 0.3f, 3, C(0.55f, 0.85f, 0.95f), 1, 0.45f, false, 0, false},
    {"NASAMS", "NSM", "AMRAAM launcher on a Sentinel radar. Two-missile salvos at aircraft; ignores rockets and bombs.",
     WPN_MISSILE, SENS_RADAR, 600, 420, 30, 0.80f, 1.0f, 6, 10, 600, true, true, PRI_AIRCRAFT,
     0.65f, 0.4f, 60, 0.5f, 4, C(0.62f, 0.90f, 0.55f), 2, 0.2f, true, 0, false},
    {"Iron Dome Battery", "IDB", "Tamir interceptors built to kill incoming munitions, not aircraft.",
     WPN_MISSILE, SENS_RADAR, 560, 380, 50, 0.85f, 0.9f, 10, 16, 600, false, true, PRI_MUNITIONS,
     0.65f, 0.4f, 90, 0.55f, 6, C(0.98f, 0.85f, 0.40f), 1, 0.45f, false, 0, false},
    {"Patriot PAC-3", "PAC3", "Long-range area defense. Two-missile salvos at aircraft, drops everything for ballistic missiles, ignores rockets and bombs.",
     WPN_MISSILE, SENS_RADAR, 900, 620, 60, 0.88f, 1.3f, 8, 14, 760, true, true, PRI_AIRCRAFT,
     0.85f, 0.4f, 80, 1.0f, 8, C(0.78f, 0.62f, 1.0f), 2, 0.15f, true, 0, false},
    {"THAAD", "THD", "Kills ballistic missiles and ICBM warheads high and far out. Can't engage aircraft.",
     WPN_MISSILE, SENS_RADAR, 1000, 750, 120, 0.85f, 2.0f, 8, 22, 900, false, true, PRI_MUNITIONS,
     0.9f, 0.6f, 100, 1.25f, 10, C(1.0f, 0.55f, 0.75f), 1, 0.45f, false, 0, false},
    /* Built into the friendly carrier; not sold in the shop. */
    {"Mk 15 Phalanx", "CIWS", "Ship's close-in weapon system. Shreds missiles and bombs aimed at the carrier.",
     WPN_GUN, SENS_RADAR, 220, 140, 0, 0.2f, 0.06f, 150, 8, 0, true, true, PRI_MUNITIONS,
     0.5f, 0.25f, 70, 0.2f, 1, C(0.6f, 0.85f, 1.0f), 1, 0.45f, false, 0, false},
    {"RIM-7 Sea Sparrow", "NSSM", "Ship's missile defense against aircraft and guided missiles.",
     WPN_MISSILE, SENS_RADAR, 520, 340, 30, 0.65f, 1.4f, 8, 14, 560, true, true, PRI_ANY,
     0.55f, 0.4f, 70, 0.3f, 1, C(0.6f, 0.85f, 1.0f), 2, 0.25f, true, 0, false},
};

const MunSpec mun_specs[N_MUN] = {
    {"S-8 rockets", 300, 4, 0, 0.05f, 0.6f, 0.05f, 5},
    {"FAB-500 bomb", 140, 10, 24, 0.1f, 0.15f, 0.05f, 8},
    {"Kh-29 missile", 330, 14, 18, 0.15f, 0.4f, 0.05f, 15},
    {"Kh-31P anti-radar missile", 380, 30, 16, 0.12f, 0.5f, 0.15f, 25},
    {"Kh-59 cruise missile", 260, 22, 22, 0.1f, 0.4f, 0.2f, 25},
    {"Kh-22 cruise missile", 250, 35, 30, 0.5f, 0.5f, 0.25f, 35},
    {"Kh-101 cruise missile", 230, 30, 28, 0.08f, 0.3f, 0.3f, 40},
    {"Iskander ballistic missile", 270, 35, 45, 0.4f, 1.5f, 0.2f, 60},
    {"ICBM warhead", 330, 90, 70, 0.3f, 2.0f, 0.1f, 150},
};

EnemySpec enemy_specs[N_ENEMY] = {
    {"MiG-21", SH_FIGHTER, 14, 1, 150, 3, 1.0f, 0, 0.35f, 0.1f, M_ROCKET, 4, 210, 0.35f, 60, 20, 1, 2, 0, ROLE_FIGHTER, 2, 0.3f, 180},
    {"Su-25", SH_ATTACK, 16, 2, 115, 8, 1.1f, 0.1f, 0.3f, 0.45f, M_BOMB, 4, 70, 0.15f, 90, 30, 2, 4, 0.5f, ROLE_STRIKER, 2, 0.2f, 120},
    {"MiG-29", SH_FIGHTER, 16, 1, 205, 4, 1.2f, 0.3f, 0.6f, 0.3f, M_ASM, 2, 340, 0.8f, 130, 40, 3, 3, 0.3f, ROLE_FIGHTER, 2, 0.42f, 280},
    {"Su-30SM", SH_FIGHTER, 17, 1, 215, 4.5f, 1.2f, 0.5f, 0.6f, 0.35f, M_ARM, 2, 480, 0.9f, 180, 55, 4, 3, 1.0f, ROLE_FIGHTER, 2, 0.45f, 360},
    {"Su-24", SH_BOMBER, 20, 2, 195, 7, 1.4f, 0.5f, 0.35f, 0.35f, M_BOMB, 8, 80, 0.12f, 170, 55, 5, 3, 0.2f, ROLE_STRIKER, 0, 0, 0},
    {"Su-27", SH_FIGHTER, 18, 1, 225, 10, 1.3f, 0.5f, 0.75f, 0.4f, M_BOMB, 2, 80, 0.3f, 260, 80, 6, 4, 0, ROLE_SWEEP, 4, 0.45f, 320},
    {"Su-34", SH_BOMBER, 21, 2, 200, 5, 1.4f, 0.7f, 0.45f, 0.4f, M_KH59, 3, 560, 0.8f, 240, 75, 7, 4, 0.35f, ROLE_STRIKER, 2, 0.32f, 250},
    {"Tu-22M3", SH_BOMBER, 30, 3, 170, 15, 2.0f, 0.8f, 0.15f, 0.3f, M_CRUISE, 3, 620, 1.4f, 400, 120, 9, 3, 0, ROLE_STRIKER, 0, 0, 0},
    {"Su-57", SH_STEALTH, 17, 2, 230, 0.1f, 0.7f, 0.6f, 0.85f, 0.5f, M_ASM, 4, 360, 0.6f, 520, 150, 11, 2, 0.5f, ROLE_FIGHTER, 4, 0.5f, 380},
};

JetSpec jet_specs[N_JET] = {
    {"A-4E Skyhawk", "A-4", "LIGHT ATTACK",
     "Cheapest jet. Quick turnaround, nimble at low speed, flies from the carrier. Cannons for a pinch.",
     "Subsonic, no air-to-air missiles, small bomb load and short range.",
     1200, 3, SH_DELTA, 13, 185, 2.5f, 1, 0, "", SENS_IR, 0, 0, 0.13f, 220, 1.0f, 0.45f, 1, 0.3f, 2, 45, 12, true, false},
    {"F-104G Starfighter", "F-104", "INTERCEPTOR",
     "Mach 2: the fastest jet in the game. Outruns most missiles, carries plenty of flares and chaff, and can take a hit and make it home.",
     "Turns badly, short endurance, rear-aspect Sidewinders that flares can fool. Land bases only, no strike role.",
     1500, 3, SH_INTERCEPTOR, 15, 310, 1.3f, 2, 4, "AIM-9", SENS_IR, 0.55f, 220, 0.2f, 320, 0, 0, 0, 0.62f, 4, 32, 14, false, false},
    {"A-6E TRAM Intruder", "A-6E", "ALL-WEATHER ATTACK",
     "Big, accurate bomb load from the TRAM turret, long range, tough. Harpoons for sinking carriers.",
     "Slow, long rearm, and no air-to-air weapons at all. Easy prey without fighter cover.",
     3200, 5, SH_BOMBER, 19, 175, 1.5f, 2, 0, "", SENS_IR, 0, 0, 0, 0, 2.6f, 0.9f, 3, 0.2f, 4, 0, 24, true, true},
    {"F/A-18A Hornet", "F/A-18", "MULTIROLE",
     "Good at everything: Sparrows outrange most enemy fighters, solid bomb load, Harpoons, carrier-capable, survivable.",
     "Expensive, short legs, and Sparrows can be jammed or chaffed.",
     4200, 6, SH_FIGHTER, 16, 245, 2.6f, 2, 4, "AIM-7", SENS_RADAR, 0.6f, 340, 0.2f, 480, 1.6f, 0.75f, 2, 0.4f, 4, 40, 18, true, true},
    {"F-111F Aardvark", "F-111", "HEAVY STRIKE",
     "Biggest bomb load and longest range. Terrain-following radar and a supersonic dash make it hard for SAMs to hit.",
     "Most expensive jet, slow to rearm, turns like a truck, no air-to-air weapons. Land bases only.",
     5600, 8, SH_SWING, 21, 270, 1.1f, 2, 0, "", SENS_IR, 0, 0, 0, 0, 4.0f, 0.82f, 4, 0.45f, 4, 0, 30, false, false},
};

SiteSpec site_specs[N_SITE] = {
    {"FOB Anvil", SK_HQ, 800, 115, 0, 1, 150, 0, 470, 0, "Headquarters. If it falls, the game is over."},
    {"FOB Bastion", SK_FOB, 260, 370, 2000, 3, 110, 10, 300, 0, "Western outpost. +$10/s, +4 emplacements and a wider perimeter."},
    {"Hollis Air Base", SK_AIRFIELD, 390, 95, 3000, 3, 120, 0, 260, 4, "Airfield with 4 hangars. Unlocks the Air Wing. +2 emplacements."},
    {"FOB Citadel", SK_FOB, 1340, 370, 3500, 5, 120, 14, 300, 0, "Eastern outpost. +$14/s, +4 emplacements and a wider perimeter."},
    {"Kestrel Air Base", SK_AIRFIELD, 1210, 95, 6000, 7, 130, 0, 260, 4, "Second airfield with 4 more hangars. +2 emplacements."},
    {"FOB Dagger", SK_FOB, 800, 610, 5000, 8, 130, 22, 300, 0, "Forward outpost right in the enemy's path. +$22/s, +4 emplacements."},
    {"USS Midway", SK_CARRIER, 1800, 430, 7500, 6, 220, 0, 0, 4, "Carrier: 4 hangars for A-4, A-6E and F/A-18. Phalanx and Sea Sparrow built in. Closest base to the enemy fleet."},
};

EBSpec eb_specs[N_EB] = {
    {"Forward Airstrip", 430, 1250, "Enemy sorties cut 40%", 1, 0.12f, 230, 2, 1, 400, 40, 50, false},
    {"SAM Belt", 900, 1185, "Defenses at other land targets halved", 1, 0.35f, 330, 2.5f, 4, 500, 50, 80, false},
    {"Iskander Launch Site", 1250, 1370, "No ballistic missile strikes", 2, 0.3f, 250, 3, 4, 800, 80, 80, false},
    {"Main Air Base", 640, 1530, "Sorties cut 60%, no raids or cruise salvos", 3, 0.38f, 300, 5, 7, 1500, 150, 90, false},
    {"Enemy Carrier Group", 1830, 1460, "No sorties from the sea, all sorties cut 25%", 3, 0.45f, 340, 6, 6, 1800, 170, 120, true},
};

const UpgSpec upg_specs[N_UPG] = {
    {"Early-Warning Radar", 4, 1200, 1.7f, 3},
    {"Armor & HESCO Walls", 6, 350, 1.75f, 1},
    {"Hardened Bunkers", 8, 300, 1.65f, 1},
    {"Supply Depot", 10, 250, 1.6f, 1},
    {"Repair Crews", 4, 350, 1.9f, 1},
    {"AIM-9L / AIM-7M", 3, 900, 1.8f, 3},
    {"Laser-Guided Bombs", 3, 800, 1.8f, 3},
    {"ALQ-131 ECM Pods", 3, 1000, 1.8f, 4},
    {"Ground Crews", 3, 600, 1.7f, 3},
    {"KC-135 Tankers", 2, 2000, 2.0f, 5},
    {"Aircraft Shelters", 2, 1200, 1.9f, 4},
    {"E-2C Hawkeye", 1, 3500, 1.0f, 5},
};

const DiffSpec diff_specs[N_DIFF] = {
    {"Easy", "Learn the ropes. More money, tougher bases, fewer and weaker attacks.",
     {"Start with $1,200, +30% income", "Attacks 35% less often, 30% less damage", "Bases +30% HP, raids from level 6"},
     1200, 1.3f, 1.25f, 1.35f, 0, 0, 0.7f, 1.3f, 6, 6},
    {"Medium", "The intended experience.",
     {"Start with $700", "Raids and Iskander strikes from level 4", "Balanced income and enemy pressure"},
     700, 1.0f, 1.0f, 1.0f, 0, 0, 1.0f, 1.0f, 4, 4},
    {"Hard", "Thin budget, sharper enemies, earlier raids and missile strikes.",
     {"Start with $450, -15% income", "Bigger waves, enemies fly 3 levels sharper", "+30% damage, raids from level 3"},
     450, 0.85f, 0.9f, 0.8f, 1, 3, 1.3f, 0.85f, 3, 3},
    {"Creative", "Free build with everything unlocked. You decide what the enemy sends, and where.",
     {"Everything free and unlocked", "Build anywhere on the map", "Spawn and command enemy aircraft"},
     0, 1.0f, 1.0f, 1.0f, 0, 0, 1.0f, 1.0f, 4, 4},
};

const float shape_pts[N_SHAPE][10][2] = {
    {{0, 1}, {0.1f, 0.45f}, {0.75f, -0.25f}, {0.75f, -0.38f}, {0.14f, -0.3f}, {0.32f, -0.8f}, {0.3f, -0.9f}, {0, -0.75f}},
    {{0, 1}, {0.1f, 0.5f}, {0.9f, 0.15f}, {0.9f, 0}, {0.12f, -0.05f}, {0.35f, -0.75f}, {0.35f, -0.88f}, {0, -0.8f}},
    {{0, 1}, {0.09f, 0.6f}, {0.95f, -0.25f}, {0.95f, -0.35f}, {0.12f, -0.2f}, {0.4f, -0.85f}, {0.4f, -0.95f}, {0, -0.85f}},
    {{0, 1}, {0.22f, 0.35f}, {0.8f, -0.3f}, {0.55f, -0.55f}, {0.3f, -0.45f}, {0.36f, -0.9f}, {0, -0.7f}},
    {{0, 1}, {0.07f, 0.65f}, {0.09f, 0.0f}, {0.5f, -0.12f}, {0.5f, -0.22f}, {0.09f, -0.22f}, {0.09f, -0.7f}, {0.34f, -0.82f}, {0.34f, -0.92f}, {0, -0.88f}},
    {{0, 1}, {0.1f, 0.55f}, {0.13f, 0.25f}, {0.72f, -0.45f}, {0.72f, -0.56f}, {0.13f, -0.42f}, {0.3f, -0.85f}, {0.3f, -0.95f}, {0, -0.85f}},
    {{0, 1}, {0.1f, 0.6f}, {0.14f, 0.2f}, {0.85f, -0.1f}, {0.85f, -0.2f}, {0.16f, -0.12f}, {0.18f, -0.6f}, {0.5f, -0.85f}, {0.5f, -0.95f}, {0, -0.88f}},
};
const int shape_npts[N_SHAPE] = {8, 8, 8, 7, 10, 9, 10};

void catalog_init(void) {
    static bool done;
    if (done) return;
    done = true;
    for (int i = 0; i < N_DEFSPEC; i++) {
        DefSpec *s = &def_specs[i];
        s->radar_r *= GS; s->engage_r *= GS; s->min_r *= GS; s->pspeed *= GS;
        s->pd = s->hits_mun && s->pri == PRI_MUNITIONS;
        /* Price follows a single effectiveness score, exactly as in the Mac game. */
        double rate = s->mag / (s->mag * (double)s->interval + s->reload);
        double coverage = pow(s->engage_r / D(150.0), 1.8);
        double vers = (s->hits_air && s->hits_mun) ? (s->pri == PRI_MUNITIONS ? 1.15 : 1.35) : 1.0;
        double sensor = s->sensor == SENS_RADAR ? 1 + 0.6 * s->eccm : 1.3;
        double munv = s->hits_mun ? 0.85 + 0.3 * s->ab : 1.0;
        double eff = sqrt(rate) * s->pk * coverage * vers * sensor * munv * 100;
        int c = (int)floor(eff * 4.5 / 50 + 0.5) * 50;
        s->cost = c < 150 ? 150 : c;
    }
    for (int i = 0; i < N_ENEMY; i++) {
        EnemySpec *e = &enemy_specs[i];
        e->speed *= GS; e->release_r *= GS; e->aa_r *= GS;
    }
    for (int i = 0; i < N_JET; i++) {
        JetSpec *j = &jet_specs[i];
        j->speed *= GS; j->msl_r *= GS; j->sensor_r *= GS;
    }
    for (int i = 0; i < N_SITE; i++) {
        SiteSpec *s = &site_specs[i];
        s->x *= GS; s->y *= GS; s->deploy_r *= GS;
    }
    for (int i = 0; i < N_EB; i++) {
        EBSpec *b = &eb_specs[i];
        b->x *= GS; b->y *= GS; b->sam_r *= GS;
    }
}
