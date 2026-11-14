/* Internals shared between sim.c (threats, radar, emplacements) and air.c (your air wing). */
#ifndef SIM_INT_H
#define SIM_INT_H
#include "game.h"

#define FIRE_THRESHOLD 0.55f

static inline float dist2d(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline bool chance(float p) { return frand() < p; }
static inline float angle_off(float heading, float dir) { return fabsf(angle_delta(heading, dir)); }
float turn_toward(float angle, float target, float max_step);
int wpick(const float *w, int n);

Contact *con_at(int ci);
static inline Ref con_ref(const Contact *c) { return (Ref){c->ci, c->gen}; }
static inline bool site_is_airbase(int s) {
    return site_specs[s].kind == SK_AIRFIELD || site_specs[s].kind == SK_CARRIER;
}
static inline bool site_accepts(int s, const JetSpec *j) {
    return site_specs[s].kind == SK_AIRFIELD || (site_specs[s].kind == SK_CARRIER && j->carrier);
}

/* effects */
void fx_explosion(float x, float y, float alt, float r, u32 color);
void fx_shock(float x, float y, float r);
void fx_tracer(float x0, float y0, float a0, float x1, float y1, float a1, u32 color);
void fx_puff(float x, float y, float alt, u32 color);
void fx_cm_burst(float x, float y, float heading);
void fx_label(const char *text, float x, float y, float alt, u32 color);
void fx_update(float dt);

/* sim.c */
bool try_countermeasures(Enemy *e);
void damage_contact(Contact *c, Defense *by_def, Jet *by_jet);
int random_site(void);
void award_strike(int eb, const char *shooter);
void check_level_up(void);
void destroy_jet(Jet *j, const char *reason);
void damage_jet(Jet *j, const char *source, bool on_ground);
int free_parking_index(int site);

/* air.c */
void update_threatened_site(void);
void update_jets(float dt);
void update_air_missiles(float dt);
void update_enemy_bases(float dt);
void update_ordnance(float dt);
void launch_air_missile(float x, float y, bool friendly, int sensor, float pk, const char *shooter,
                        Ref target_con, Ref target_jet, Ref shooter_jet);
int enemy_missiles_on(const Jet *j);

#endif
