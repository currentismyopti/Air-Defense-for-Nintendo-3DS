/* Air Defense Command, Nintendo 3DS port. Shared types and the simulation API. */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;

#define PI 3.14159265358979f
/* The map is 80% of the original Mac layout and the game runs at 80% speed (same as the Mac build). */
#define GS 0.8f
#define D(v) ((v) * GS)
#define WORLD_W (2000.0f * GS)
#define WORLD_H (1700.0f * GS)
#define TIME_SCALE 0.8f

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define RGBF(r, g, b, a) RGBA((int)((r) * 255), (int)((g) * 255), (int)((b) * 255), (int)((a) * 255))

/* ---------- Catalog ---------- */

enum { SENS_RADAR, SENS_IR };
enum { WPN_GUN, WPN_MISSILE };
enum { PRI_AIRCRAFT, PRI_MUNITIONS, PRI_ANY };

enum { D_VULCAN, D_STINGER, D_CRAM, D_SKYNEX, D_NASAMS, D_IRONDOME, D_PATRIOT, D_THAAD,
       D_PHALANX, D_SEASPARROW, N_DEFSPEC };
#define N_SHOP_DEF 8

typedef struct {
    const char *name, *tag, *role;
    u8 weapon, sensor;
    float radar_r, engage_r, min_r;
    float pk, interval;
    int mag;
    float reload, pspeed;
    bool hits_air, hits_mun;
    u8 pri;
    float eccm, scan, hp, ab;
    int unlock;
    u32 color;
    int salvo;
    float falloff;
    bool guided_only;
    /* derived at start-up */
    int cost;
    bool pd;
} DefSpec;

enum { M_ROCKET, M_BOMB, M_ASM, M_ARM, M_KH59, M_CRUISE, M_ALCM, M_SRBM, M_ICBM, N_MUN };
typedef struct {
    const char *name;
    float speed, damage, splash, rcs, ir, maneuver;
    int bounty;
} MunSpec;
#define MUN_GUIDED(k) ((k) != M_ROCKET && (k) != M_BOMB)
#define MUN_BALLISTIC(k) ((k) == M_SRBM || (k) == M_ICBM)
#define MUN_CRUISE(k) ((k) == M_KH59 || (k) == M_CRUISE || (k) == M_ALCM)

enum { SH_FIGHTER, SH_ATTACK, SH_BOMBER, SH_STEALTH, SH_INTERCEPTOR, SH_DELTA, SH_SWING, N_SHAPE };
enum { ROLE_STRIKER, ROLE_FIGHTER, ROLE_SWEEP };
#define N_ENEMY 9
typedef struct {
    const char *name;
    u8 shape;
    float size;
    int hits;
    float speed, rcs, ir, ecm, maneuver, flares;
    u8 mun;
    int mun_count;
    float release_r, release_iv;
    int bounty, xp, min_level, cm;
    float sead;
    u8 role;
    int aa;
    float aa_pk, aa_r;
} EnemySpec;

#define N_JET 5
typedef struct {
    const char *name, *tag, *role, *strengths, *weaknesses;
    int cost, unlock;
    u8 shape;
    float size, speed, turn;
    int hits, missiles;
    const char *msl_name;
    u8 msl_sensor;
    float msl_pk, msl_r, gun_pk, sensor_r, strike, precision;
    int radius;
    float evasion;
    int cm;
    float endurance, rearm;
    bool carrier, antiship;
} JetSpec;
#define JET_CAN_PATROL(s) ((s)->missiles > 0 || (s)->gun_pk > 0)
#define JET_CAN_STRIKE(s) ((s)->strike > 0)

enum { SK_HQ, SK_FOB, SK_AIRFIELD, SK_CARRIER };
#define N_SITE 7
typedef struct {
    const char *name;
    u8 kind;
    float x, y;
    int cost, unlock;
    float hp;
    int income;
    float deploy_r;
    int hangars;
    const char *blurb;
} SiteSpec;

enum { EB_AIRSTRIP, EB_SAMBELT, EB_ISKANDER, EB_AIRBASE, EB_CARRIER, N_EB };
typedef struct {
    const char *name;
    float x, y;
    const char *effect;
    int depth;
    float defense, sam_r, health;
    int unlock, reward, xp;
    float down;
    bool antiship;
} EBSpec;

enum { U_RADAR, U_ARMOR, U_BUNKERS, U_DEPOT, U_REPAIR,
       U_MISSILES, U_PRECISION, U_ECM, U_GROUND, U_TANKER, U_SHELTERS, U_HAWKEYE, N_UPG };
#define N_BASE_UPG 5
typedef struct {
    const char *title;
    int max;
    float base, growth;
    int unlock;
} UpgSpec;

enum { DF_EASY, DF_MEDIUM, DF_HARD, DF_CREATIVE, N_DIFF };
typedef struct {
    const char *title, *tagline, *details[3];
    int money;
    float income, bounty, spawn_iv;
    int extra, lvl_bonus;
    float dmg, base_hp;
    int raid_lvl, msl_lvl;
} DiffSpec;

extern DefSpec def_specs[N_DEFSPEC];
extern const MunSpec mun_specs[N_MUN];
extern EnemySpec enemy_specs[N_ENEMY];
extern JetSpec jet_specs[N_JET];
extern SiteSpec site_specs[N_SITE];
extern EBSpec eb_specs[N_EB];
extern const UpgSpec upg_specs[N_UPG];
extern const DiffSpec diff_specs[N_DIFF];
/* Right half of each airframe outline, nose up, in units of the airframe size. */
extern const float shape_pts[N_SHAPE][10][2];
extern const int shape_npts[N_SHAPE];
void catalog_init(void);

/* ---------- Runtime entities ---------- */

#define MAX_DEF 80
#define MAX_EN 96
#define MAX_MUN 256
#define MAX_CON (MAX_EN + MAX_MUN)
#define MAX_ICP 128
#define MAX_JETS 12
#define MAX_AM 96
#define MAX_ORD 64
#define MAX_FX 320
#define MAX_LOG 50
#define LOG_LEN 100

/* A slot index plus the slot's generation, so stale references resolve to nothing. */
typedef struct { s16 i; u16 g; } Ref;
#define NOREF ((Ref){-1, 0})

/* Anything a sensor can track: enemy aircraft and their weapons. */
typedef struct {
    bool alive, is_mun;
    u16 gen;
    s16 ci;
    float x, y;
    float rcs, ir, ecm, flares, maneuver, speed;
    int hits;
    float dispq;
    float alt;
} Contact;

typedef struct {
    Contact c;
    u8 spec;
    int bounty;
    float heading, turn_rate;
    int mun_left;
    float release_t;
    bool egress;
    float ex, ey;
    float age;
    s16 target_site;
    Ref strike_def;
    Ref dogfight;
    int cm;
    float cm_window, cm_cd;
    int aa_left;
    float aa_cd;
    bool locked;
} Enemy;

typedef struct {
    Contact c;
    u8 kind;
    float ax, ay;
    float damage;
    Ref target_def;
    bool marker, decoy_checked;
    float trail_t, d0;
} Munition;

typedef struct {
    bool active, fixed;
    u16 gen;
    u8 spec;
    s16 mount;
    float x, y, hp;
    int kills, ammo;
    float reload_left, cooldown, scan_t, aim, born;
    float trk[MAX_CON];
} Defense;

typedef struct {
    bool alive, cm_checked, decoyed;
    u8 spec;
    Ref owner, target;
    float x, y, ang, speed, launch_r, launch_q, age, trail_t, dx, dy, alt;
} Interceptor;

enum { MIS_AUTO, MIS_PATROL, MIS_STRIKE };
enum { JS_PARKED, JS_OUTBOUND, JS_RETURNING, JS_PATROL };
enum { JP_READY, JP_REARMING, JP_STANDBY, JP_STRIKING, JP_PATROLLING, JP_RETURNING };

typedef struct {
    bool alive, homeless;
    u16 gen;
    u8 spec, mission, state;
    char title[12];
    s16 home, parking, strike_target;
    float x, y, heading;
    int hits, missiles, cm, kills, sorties;
    float fuel, rearm_left, cooldown, gun_cd, orbit, retarget, air_t, flash;
    Ref target;
} Jet;

typedef struct {
    bool alive, friendly, cm_checked, decoyed;
    u8 sensor;
    float x, y, ang, speed, pk, age, trail_t, dx, dy;
    char shooter[28];
    Ref shooter_jet, target_con, target_jet;
} AirMissile;

typedef struct {
    bool alive, missile;
    float fx, fy, tx, ty, dur, t, damage;
    s16 target;
    char shooter[12];
} Ordnance;

typedef struct {
    bool built, destroyed;
    float hp, max_hp, launch_cd, born;
} Site;

typedef struct {
    float health, down_left, sam_cd;
    bool targeted;
} EBase;

enum { FX_NONE, FX_EXPLOSION, FX_SHOCK, FX_TRACER, FX_PUFF, FX_DOT, FX_LABEL };
typedef struct {
    u8 kind;
    float x, y, x2, y2, r, t, life, alt;
    u32 color;
    char text[14];
} Fx;

enum { LOG_KILL, LOG_DANGER, LOG_INFO };
typedef struct { u8 kind; float time; char text[LOG_LEN]; } LogEntry;
typedef struct { char title[12]; u8 spec; char reason[60]; } LostJet;

enum { PH_MENU, PH_PLAY };

typedef struct {
    int phase, diff;
    int money, level, xp;
    int upg[N_UPG];
    int kills, intercepts, shots, shot_hits, aircraft_lost;
    bool over, paused, fast;
    int patrol_site;    /* -1 = auto */
    int strike_target;  /* -1 = auto */
    /* Creative mode */
    bool auto_waves, indestructible, sp_placing;
    int sp_type, sp_count, sp_target, sp_edge;
    /* timers */
    float spawn_t, raid_t, strike_t, income_t, ew_t;
    int pend_shots, pend_hits;
    float awacs_ang, awx, awy;
    float time;         /* game clock, seconds */
    float real_time;    /* wall clock while in a game, for UI animation */
    int threatened;
    int hostiles;

    Site sites[N_SITE];
    EBase eb[N_EB];
    Defense def[MAX_DEF];
    Enemy en[MAX_EN];
    Munition mun[MAX_MUN];
    Interceptor icp[MAX_ICP];
    Jet jets[MAX_JETS];
    AirMissile am[MAX_AM];
    Ordnance ord[MAX_ORD];
    Fx fx[MAX_FX];
    int fx_next;
    float net[MAX_CON];
    int jet_numbers[N_JET];
    LostJet lost[8];
    int n_lost;
    LogEntry log[MAX_LOG];
    int log_head, log_n;

    /* selection and placement */
    Ref sel_def;
    int sel_site;       /* -1 = none */
    Ref sel_enemy;
    int placing;        /* defense spec being placed, or -1 */
    Ref relocating;
    float ghost_x, ghost_y, ghost_nudge;
} Game;

extern Game G;

/* ---------- Simulation API ---------- */

u32 rng_u32(void);
void rng_seed(u32 s);
float frand(void);
float frr(float a, float b);
int irr(int a, int b);
float angle_delta(float a, float b);

void glog(int kind, const char *fmt, ...);
const LogEntry *log_at(int i); /* 0 = newest */

void game_start(int diff);
void game_restart(void);
void game_menu(void);
void sim_update(float real_dt);

static inline const DiffSpec *settings(void) { return &diff_specs[G.diff]; }
static inline bool creative(void) { return G.diff == DF_CREATIVE; }
static inline bool unlocked(int lvl) { return creative() || G.level >= lvl; }
static inline bool can_afford(int cost) { return creative() || G.money >= cost; }

int xp_to_next(void);
float armor_reduction(void);
int income_per_second(void);
float repair_rate(void);
float ew_range(void);
float ew_eccm(void);
int emplacement_cap(void);
int placed_count(void);
bool has_airfield(void);
bool awacs_active(void);
int hangars_total(void);
int hangars_used(void);
int jets_at(int site);

Defense *def_get(Ref r);
Enemy *en_get(Ref r);
Jet *jet_get(Ref r);
Contact *con_get(Ref r);
Ref def_ref(const Defense *d);
Ref jet_ref(const Jet *j);

/* geography */
float coast_x(float y);
float front_y(float x);
bool is_water(float x, float y);
bool is_enemy_territory(float x, float y);
typedef struct { float x, y, w, h; } Rect;
extern const Rect site_fp[4][2];
extern const int site_nfp[4];
extern const Rect site_nb[4];
void site_parking(int s, int i, float *x, float *y);
bool site_hit(int s, float x, float y);

/* placement and emplacements */
bool begin_placement(int spec);
bool begin_relocate(void);
void cancel_placement(void);
static inline bool is_placing(void) { return G.placing >= 0 || G.relocating.i >= 0; }
const char *placement_problem(float x, float y, const Defense *moving);
const char *ghost_problem(void);
bool confirm_placement(void);
int sell_value(const Defense *d);
int repair_cost(const Defense *d);
void sell_selected(void);
void repair_selected(void);
void deselect_all(void);
/* Tap on the map: select a unit, a base or an enemy target. `slop` widens hit areas (world units). */
void select_at(float x, float y, float slop);

/* bases */
int site_price(int s);
int site_repair_cost(int s);
int total_repair_cost(void);
bool build_site(int s);
void repair_site(int s);
void repair_all_sites(void);

/* upgrades */
int upgrade_cost(int u); /* -1 when maxed */
bool buy_upgrade(int u);
void upgrade_detail(int u, char *out, size_t n);

/* air wing */
int airbase_with_space(const JetSpec *spec);
bool buy_jet(int spec);
void set_mission(Jet *j, int mission);
void sell_jet(Jet *j);
bool rebase_jet(Jet *j, int site);
int next_rebase_option(const Jet *j);
int jet_phase(const Jet *j);
void jet_status(const Jet *j, char *out, size_t n);
float jet_endurance(const JetSpec *spec);
float jet_alt(const Jet *j);
float effective_defense(int eb);
bool eb_down(int eb);
int eb_assigned(int eb);
void set_strike_target(int eb);

/* creative */
void creative_spawn(int type, int count, int target_site, int edge, bool at_point, float x, float y);
void creative_strike(int kind);
void creative_raid(void);
void clear_hostiles(void);
void set_threat_level(int l);
void order_enemy_site(int site);
void retreat_enemy(void);
void remove_enemy(void);

#endif
