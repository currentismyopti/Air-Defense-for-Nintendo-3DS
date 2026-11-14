/* Your air wing: squadron management, patrols, strike runs on enemy installations, air-to-air missiles
   and the enemy's SAMs. A straight port of the Mac game's BattleScene+Air. */
#include "sim_int.h"

/* ---------- Squadron management ---------- */

int jets_at(int site) {
    int n = 0;
    for (int i = 0; i < MAX_JETS; i++)
        if (G.jets[i].alive && G.jets[i].home == site) n++;
    return n;
}
int hangars_total(void) {
    int n = 0;
    for (int s = 0; s < N_SITE; s++)
        if (G.sites[s].built && site_is_airbase(s)) n += site_specs[s].hangars;
    return n;
}
int hangars_used(void) {
    int n = 0;
    for (int i = 0; i < MAX_JETS; i++)
        if (G.jets[i].alive) n++;
    return n;
}

/* The emptiest airfield or carrier that can take this aircraft, or -1. */
int airbase_with_space(const JetSpec *spec) {
    int best = -1, best_n = 99;
    for (int s = 0; s < N_SITE; s++) {
        if (!G.sites[s].built || !site_is_airbase(s) || !site_accepts(s, spec)) continue;
        int n = jets_at(s);
        if (n < site_specs[s].hangars && n < best_n) { best_n = n; best = s; }
    }
    return best;
}

int free_parking_index(int site) {
    for (int p = 0; p < 4; p++) {
        bool used = false;
        for (int i = 0; i < MAX_JETS; i++)
            if (G.jets[i].alive && G.jets[i].home == site && G.jets[i].parking == p) used = true;
        if (!used) return p;
    }
    return 0;
}

bool buy_jet(int spec) {
    const JetSpec *s = &jet_specs[spec];
    int field = airbase_with_space(s);
    if (G.over || !unlocked(s->unlock) || !can_afford(s->cost) || field < 0) return false;
    for (int i = 0; i < MAX_JETS; i++) {
        Jet *j = &G.jets[i];
        if (j->alive) continue;
        u16 g = (u16)(j->gen + 1);
        memset(j, 0, sizeof *j);
        j->gen = g;
        j->alive = true;
        j->spec = (u8)spec;
        j->mission = MIS_AUTO;
        j->state = JS_PARKED;
        snprintf(j->title, sizeof j->title, "%s #%d", s->tag, ++G.jet_numbers[spec]);
        j->parking = (s16)free_parking_index(field);
        j->home = (s16)field;
        j->strike_target = -1;
        site_parking(field, j->parking, &j->x, &j->y);
        j->heading = PI / 2;
        j->hits = s->hits;
        j->missiles = s->missiles;
        j->fuel = s->endurance;
        j->cm = s->cm;
        j->rearm_left = 3;
        j->orbit = frr(0, 2 * PI);
        j->target = NOREF;
        if (!creative()) G.money -= s->cost;
        glog(LOG_INFO, "%s delivered to the air wing.", s->name);
        return true;
    }
    return false;
}

void set_mission(Jet *j, int mission) {
    const JetSpec *s = &jet_specs[j->spec];
    bool ok = mission == MIS_AUTO || (mission == MIS_PATROL && JET_CAN_PATROL(s)) || (mission == MIS_STRIKE && JET_CAN_STRIKE(s));
    if (!j->alive || !ok) return;
    j->mission = (u8)mission;
    if (j->state == JS_PATROL && mission == MIS_STRIKE) {
        j->state = JS_RETURNING;
        j->target = NOREF;
    }
}

void sell_jet(Jet *j) {
    if (G.over || !j->alive || j->state != JS_PARKED) return;
    j->alive = false;
    int refund = jet_specs[j->spec].cost / 2;
    if (creative()) glog(LOG_INFO, "%s removed.", j->title);
    else { G.money += refund; glog(LOG_INFO, "%s sold for $%d.", j->title, refund); }
}

/* Moves a jet to another airfield or carrier. A parked jet flies there. */
bool rebase_jet(Jet *j, int site) {
    if (G.over || !j->alive || site < 0 || site == j->home || !G.sites[site].built || !site_is_airbase(site)
        || !site_accepts(site, &jet_specs[j->spec]) || jets_at(site) >= site_specs[site].hangars) return false;
    j->parking = (s16)free_parking_index(site);
    j->home = (s16)site;
    if (j->state == JS_PARKED) { j->state = JS_RETURNING; j->air_t = 0; }
    glog(LOG_INFO, "%s transferring to %s.", j->title, site_specs[site].name);
    return true;
}

/* The next base after the jet's current one that has room for it, or -1. */
int next_rebase_option(const Jet *j) {
    for (int k = 1; k < N_SITE; k++) {
        int s = (j->home + k) % N_SITE;
        if (G.sites[s].built && site_is_airbase(s) && site_accepts(s, &jet_specs[j->spec])
            && jets_at(s) < site_specs[s].hangars) return s;
    }
    return -1;
}

float jet_endurance(const JetSpec *spec) { return spec->endurance * (1 + 0.3f * G.upg[U_TANKER]); }

/* ---------- Deciding what to do ---------- */

/* Called once a second: which base has the most hostiles bearing down on it. */
void update_threatened_site(void) {
    int best = 0;
    float best_score = 0;
    for (int s = 0; s < N_SITE; s++) {
        if (!G.sites[s].built) continue;
        float score = s == 0 ? 0.2f : 0;
        for (int i = 0; i < MAX_EN; i++) {
            const Enemy *e = &G.en[i];
            if (!e->c.alive || e->egress) continue;
            float d = dist2d(e->c.x, e->c.y, site_specs[s].x, site_specs[s].y);
            if (d < D(700)) score += 1 - d / D(700);
            if (e->target_site == s) score += 0.5f;
        }
        if (score > best_score) { best_score = score; best = s; }
    }
    G.threatened = best;
}

static void patrol_station(float *x, float *y) {
    int id = G.patrol_site < 0 ? G.threatened : G.patrol_site;
    if (id < 0 || id >= N_SITE || !G.sites[id].built) id = 0;
    float px = site_specs[id].x, py = site_specs[id].y + D(site_specs[id].kind == SK_CARRIER ? 170 : 230);
    *x = clampf(px, 80, WORLD_W - 80);
    *y = maxf(80, minf(front_y(*x) - D(140), py));
}

/* Hostile aircraft over your side of the front. */
static int air_threat(void) {
    int n = 0;
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &G.en[i];
        if (e->c.alive && !e->egress && !is_enemy_territory(e->c.x, e->c.y)) n++;
    }
    return n;
}

float effective_defense(int b) {
    int l = G.level > 1 ? G.level - 1 : 0;
    float d = minf(0.8f, eb_specs[b].defense * (1 + 0.03f * l));
    if (b != EB_SAMBELT && b != EB_CARRIER && eb_down(EB_SAMBELT)) d *= 0.5f;
    return d;
}

static bool strike_available(int b, const Jet *j) {
    const JetSpec *s = &jet_specs[j->spec];
    return unlocked(eb_specs[b].unlock) && !eb_down(b)
        && eb_specs[b].depth <= s->radius + G.upg[U_TANKER]
        && (!eb_specs[b].antiship || s->antiship);
}

/* Auto targeting: value of the target right now, minus the risk to this airframe. */
static float strike_score(int b, const Jet *j) {
    const JetSpec *js = &jet_specs[j->spec];
    float s = eb_specs[b].reward / 400.0f;
    switch (b) {
    case EB_ISKANDER:
        if (G.level >= settings()->msl_lvl) s += 1.5f;
        break;
    case EB_AIRSTRIP:
    case EB_AIRBASE:
        s += G.hostiles * 0.08f;
        break;
    case EB_SAMBELT:
        /* Suppress the SAMs first if other reachable targets are well defended. */
        for (int o = 0; o < N_EB; o++)
            if (o != b && strike_available(o, j) && eb_specs[o].defense > 0.25f) { s += 2; break; }
        break;
    case EB_CARRIER:
        s += 0.6f;
        break;
    }
    s += (1 - G.eb[b].health / eb_specs[b].health) * 2.5f;
    s -= effective_defense(b) * 4 * (1 - js->evasion) * (js->hits > 1 ? 0.6f : 1);
    float en_route = 0;
    for (int i = 0; i < MAX_JETS; i++) {
        const Jet *o = &G.jets[i];
        if (o->alive && o != j && o->state == JS_OUTBOUND && o->strike_target == b) en_route += jet_specs[o->spec].strike;
    }
    if (en_route >= G.eb[b].health) s -= 6;
    return s;
}

/* The target you picked if this jet can hit it, otherwise the best one it can reach. */
static int choose_strike_target(const Jet *j) {
    if (G.strike_target >= 0 && strike_available(G.strike_target, j)) return G.strike_target;
    int best = -1;
    float best_s = -1e9f;
    for (int b = 0; b < N_EB; b++) {
        if (!strike_available(b, j)) continue;
        float s = strike_score(b, j);
        if (best < 0 || s > best_s) { best_s = s; best = b; }
    }
    return best;
}

/* What this jet's next sortie will be, or -1 if it should stay on the ground. */
static int sortie_mission(const Jet *j) {
    const JetSpec *s = &jet_specs[j->spec];
    int target = JET_CAN_STRIKE(s) ? choose_strike_target(j) : -1;
    switch (j->mission) {
    case MIS_PATROL: return JET_CAN_PATROL(s) ? MIS_PATROL : -1;
    case MIS_STRIKE: return target >= 0 ? MIS_STRIKE : -1;
    default:
        if (!JET_CAN_STRIKE(s)) return JET_CAN_PATROL(s) ? MIS_PATROL : -1;
        if (!JET_CAN_PATROL(s)) return target >= 0 ? MIS_STRIKE : -1;
        /* Jets with real air-to-air missiles defend when the sky is busy. */
        if (s->missiles > 0 && air_threat() >= 2) return MIS_PATROL;
        return target >= 0 ? MIS_STRIKE : MIS_PATROL;
    }
}

/* ---------- Air-to-air missiles ---------- */

void launch_air_missile(float x, float y, bool friendly, int sensor, float pk, const char *shooter,
                        Ref target_con, Ref target_jet, Ref shooter_jet) {
    for (int i = 0; i < MAX_AM; i++) {
        AirMissile *m = &G.am[i];
        if (m->alive) continue;
        memset(m, 0, sizeof *m);
        m->alive = true;
        m->friendly = friendly;
        m->sensor = (u8)sensor;
        m->x = x; m->y = y;
        m->speed = D(friendly ? (sensor == SENS_RADAR ? 620 : 540) : (sensor == SENS_RADAR ? 560 : 500));
        m->pk = pk;
        snprintf(m->shooter, sizeof m->shooter, "%s", shooter);
        m->target_con = target_con;
        m->target_jet = target_jet;
        m->shooter_jet = shooter_jet;
        return;
    }
}

int enemy_missiles_on(const Jet *j) {
    int n = 0;
    for (int i = 0; i < MAX_AM; i++) {
        const AirMissile *m = &G.am[i];
        if (m->alive && !m->friendly && m->target_jet.i == j - G.jets && m->target_jet.g == j->gen) n++;
    }
    return n;
}

static int friendly_missiles_on(const Contact *c) {
    int n = 0;
    for (int i = 0; i < MAX_AM; i++) {
        const AirMissile *m = &G.am[i];
        if (m->alive && m->friendly && m->target_con.i == c->ci && m->target_con.g == c->gen) n++;
    }
    return n;
}

static void fire_aam(Jet *j, Contact *t) {
    const JetSpec *s = &jet_specs[j->spec];
    int ml = G.upg[U_MISSILES];
    j->missiles--;
    j->cooldown = 1.5f;
    float pk = s->msl_pk * (1 + 0.08f * ml);
    if (s->msl_sensor == SENS_IR && !t->is_mun) {
        /* Early Sidewinders want a tail shot; AIM-9L (missile level 2) is all-aspect. */
        const Enemy *e = (const Enemy *)t;
        float to_shooter = atan2f(j->y - t->y, j->x - t->x);
        float ca = cosf(angle_delta(e->heading + PI, to_shooter));
        pk *= ml >= 2 ? 0.75f + 0.125f * (1 + ca) : 0.5f + 0.25f * (1 + ca);
    }
    launch_air_missile(j->x, j->y, true, s->msl_sensor, pk, j->title, con_ref(t), NOREF, jet_ref(j));
}

/* Strike and returning jets with missiles left take shots at fighters that cross their nose. */
static void opportunistic_shot(Jet *j) {
    const JetSpec *s = &jet_specs[j->spec];
    if (j->missiles <= 0 || j->cooldown > 0) return;
    Enemy *pick = NULL;
    float best = 1e9f;
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &G.en[i];
        if (!e->c.alive) continue;
        float d = dist2d(e->c.x, e->c.y, j->x, j->y);
        if (d > s->msl_r || d >= best) continue;
        if (angle_off(j->heading, atan2f(e->c.y - j->y, e->c.x - j->x)) >= 1.0f) continue;
        if (friendly_missiles_on(&e->c) >= e->c.hits) continue;
        best = d;
        pick = e;
    }
    if (pick) fire_aam(j, &pick->c);
}

/* The target pops flares or chaff; returns true if the missile was fooled. */
static bool spoofed(AirMissile *m, int ml, int ecm) {
    float p, ax, ay, heading;
    Contact *c = con_get(m->target_con);
    Jet *j = jet_get(m->target_jet);
    if (m->friendly && c && !c->is_mun) {
        Enemy *e = (Enemy *)c;
        if (!try_countermeasures(e)) return false;
        p = m->sensor == SENS_IR ? minf(0.5f, c->flares) * (ml >= 2 ? 0.6f : 1) : 0.2f * (1 - 0.1f * ml);
        ax = c->x; ay = c->y; heading = e->heading;
    } else if (!m->friendly && j) {
        if (j->cm <= 0) return false;
        j->cm--;
        fx_cm_burst(j->x, j->y, j->heading);
        p = (m->sensor == SENS_IR ? 0.4f : 0.3f) * (1 + 0.15f * ecm);
        ax = j->x; ay = j->y; heading = j->heading;
    } else {
        return false;
    }
    if (frand() >= p) return false;
    m->decoyed = true;
    float back = heading + PI + frr(-0.6f, 0.6f);
    m->dx = ax + cosf(back) * 45;
    m->dy = ay + sinf(back) * 45;
    fx_label("DECOYED", ax, ay, 1, RGBF(1, 0.8f, 0.35f, 1));
    return true;
}

static void resolve_air_missile(AirMissile *m, int ecm) {
    Contact *c = con_get(m->target_con);
    Jet *j = jet_get(m->target_jet);
    if (m->friendly && c) {
        float pk = m->pk * (1 - 0.3f * c->maneuver);
        if (m->sensor == SENS_RADAR) pk *= 1 - 0.4f * c->ecm * 0.45f;
        if (c->is_mun) pk *= 0.7f;
        pk = clampf(pk, 0.03f, 0.95f);
        if (frand() < pk) damage_contact(c, NULL, jet_get(m->shooter_jet));
        else fx_label("MISS", c->x, c->y, c->alt, RGBF(0.75f, 0.75f, 0.75f, 1));
    } else if (!m->friendly && j && j->state != JS_PARKED) {
        float pk = m->pk * (1 - 0.8f * jet_specs[j->spec].evasion) * (1 - 0.1f * ecm);
        pk = clampf(pk, 0.03f, 0.9f);
        if (frand() < pk) damage_jet(j, m->shooter, false);
        else fx_label("MISS", j->x, j->y, 1, RGBF(0.75f, 0.75f, 0.75f, 1));
    }
}

void update_air_missiles(float dt) {
    int ml = G.upg[U_MISSILES], ecm = G.upg[U_ECM];
    for (int i = 0; i < MAX_AM; i++) {
        AirMissile *m = &G.am[i];
        if (!m->alive) continue;
        m->age += dt;
        float step = m->speed * dt;
        if (m->decoyed) {
            float tx = m->dx - m->x, ty = m->dy - m->y, dist = sqrtf(tx * tx + ty * ty);
            if (dist <= maxf(step, 6) || m->age > 5) {
                m->alive = false;
                fx_explosion(m->dx, m->dy, 1, 5, RGBF(0.85f, 0.85f, 0.85f, 0.6f));
            } else {
                m->x += tx / dist * step; m->y += ty / dist * step;
                m->ang = atan2f(ty, tx);
            }
            continue;
        }
        bool have = false;
        float px = 0, py = 0;
        Contact *c = con_get(m->target_con);
        Jet *j = jet_get(m->target_jet);
        if (c) { px = c->x; py = c->y; have = true; }
        if (j && j->state != JS_PARKED) { px = j->x; py = j->y; have = true; }
        if (!have || m->age >= 5) { m->alive = false; continue; }
        float tx = px - m->x, ty = py - m->y, dist = sqrtf(tx * tx + ty * ty);
        if (!m->cm_checked && dist < D(110)) {
            m->cm_checked = true;
            if (spoofed(m, ml, ecm)) continue;
        }
        if (dist <= maxf(step, 8)) {
            m->alive = false;
            resolve_air_missile(m, ecm);
        } else {
            m->x += tx / dist * step; m->y += ty / dist * step;
            m->ang = atan2f(ty, tx);
            m->trail_t -= dt;
            if (m->trail_t <= 0) {
                m->trail_t = 0.04f;
                fx_puff(m->x, m->y, 1, m->friendly ? RGBF(0.75f, 0.88f, 1, 0.4f) : RGBF(1, 0.7f, 0.65f, 0.4f));
            }
        }
    }
}

void damage_jet(Jet *j, const char *source, bool on_ground) {
    if (!j->alive) return;
    j->hits--;
    if (j->hits <= 0) {
        char why[60];
        snprintf(why, sizeof why, on_ground ? "destroyed on the ground by %s" : "shot down by %s", source);
        destroy_jet(j, why);
        return;
    }
    float alt = j->state == JS_PARKED ? 0 : 1;
    fx_explosion(j->x, j->y, alt, 7, RGBF(1, 0.8f, 0.5f, 0.8f));
    fx_label("DAMAGED", j->x, j->y, alt, RGBF(1, 0.6f, 0.4f, 1));
    glog(LOG_DANGER, "%s damaged by %s.", j->title, source);
    if (j->state == JS_PATROL) {
        j->state = JS_RETURNING;
        j->target = NOREF;
    }
}

void destroy_jet(Jet *j, const char *reason) {
    if (!j->alive) return;
    j->alive = false;
    float alt = j->state == JS_PARKED ? 0 : 1;
    fx_explosion(j->x, j->y, alt, 18, RGBF(0.6f, 0.8f, 1, 0.9f));
    char text[14];
    snprintf(text, sizeof text, "%s LOST", jet_specs[j->spec].tag);
    fx_label(text, j->x, j->y, alt, RGBF(0.6f, 0.82f, 1, 1));
    G.aircraft_lost++;
    for (int i = 7; i > 0; i--) G.lost[i] = G.lost[i - 1];
    snprintf(G.lost[0].title, sizeof G.lost[0].title, "%s", j->title);
    snprintf(G.lost[0].reason, sizeof G.lost[0].reason, "%s", reason);
    G.lost[0].spec = j->spec;
    if (G.n_lost < 8) G.n_lost++;
    glog(LOG_DANGER, "%s %s.", j->title, reason);
}

/* ---------- Flight ---------- */

static void steer(Jet *j, float px, float py, float dt, float turn_boost, float speed_factor) {
    const JetSpec *s = &jet_specs[j->spec];
    float desired = atan2f(py - j->y, px - j->x);
    j->heading = turn_toward(j->heading, desired, s->turn * turn_boost * dt);
    float step = s->speed * speed_factor * dt;
    j->x += cosf(j->heading) * step;
    j->y += sinf(j->heading) * step;
}

static void try_launch(Jet *j) {
    Site *home = &G.sites[j->home];
    if (!home->built || home->launch_cd > 0) return;
    int sortie = sortie_mission(j);
    if (sortie < 0) return;
    int t = sortie == MIS_STRIKE ? choose_strike_target(j) : -1;
    if (t >= 0) {
        j->strike_target = (s16)t;
        j->state = JS_OUTBOUND;
    } else {
        j->fuel = jet_endurance(&jet_specs[j->spec]);
        j->state = JS_PATROL;
    }
    j->sorties++;
    j->air_t = 0;
    home->launch_cd = 1.2f;
    j->heading = site_specs[j->home].kind == SK_CARRIER ? PI / 2 : 0;
}

static void land(Jet *j) {
    const JetSpec *s = &jet_specs[j->spec];
    j->state = JS_PARKED;
    j->target = NOREF;
    j->strike_target = -1;
    site_parking(j->home, j->parking, &j->x, &j->y);
    j->heading = PI / 2;
    float t = s->rearm * (1 - 0.18f * G.upg[U_GROUND]);
    if (j->hits < s->hits) t *= 1.5f;
    j->rearm_left = t;
    j->hits = s->hits;
    j->missiles = s->missiles;
    j->fuel = jet_endurance(s);
    j->cm = s->cm;
}

static void fly_home(Jet *j, float dt) {
    const JetSpec *s = &jet_specs[j->spec];
    if (j->homeless || !G.sites[j->home].built) {
        int field = airbase_with_space(s);
        if (field < 0) {
            for (int k = 0; k < N_SITE && field < 0; k++)
                if (G.sites[k].built && site_is_airbase(k) && site_accepts(k, s)) field = k;
        }
        if (field < 0) { destroy_jet(j, "ditched: no airfield left to land at"); return; }
        j->parking = (s16)free_parking_index(field);
        j->home = (s16)field;
        j->homeless = false;
    }
    float sx, sy;
    site_parking(j->home, j->parking, &sx, &sy);
    float d = dist2d(j->x, j->y, sx, sy);
    if (d < 26) { land(j); return; }
    steer(j, sx, sy, dt, d < D(300) ? 3 : 1, d < D(200) ? 0.6f : 1);
}

/* How high the jet is flying, 0 on the ground to 1 at altitude (climb-out and landing approach). */
float jet_alt(const Jet *j) {
    if (j->state == JS_PARKED) return 0;
    float a = minf(1, 0.1f + j->air_t / 1.4f);
    if (j->state == JS_RETURNING && G.sites[j->home].built) {
        float sx, sy;
        site_parking(j->home, j->parking, &sx, &sy);
        a = minf(a, 0.1f + dist2d(j->x, j->y, sx, sy) / D(220));
    }
    return a;
}

/* ---------- Strike runs (visible on the map) ---------- */

static void release_weapons(Jet *j, int b) {
    const JetSpec *s = &jet_specs[j->spec];
    const EBSpec *t = &eb_specs[b];
    float precision = (float)G.upg[U_PRECISION];
    float p_hit = minf(0.97f, 0.3f + 0.6f * s->precision + 0.06f * precision);
    bool hit = frand() < p_hit;
    bool missile = t->antiship;
    int count = missile ? 2 : 4;
    float total = hit ? s->strike * (1 + 0.1f * precision) : 0;
    float hit_r = b == EB_CARRIER ? 34 : 45;
    for (int i = 0; i < count; i++) {
        float tx, ty;
        if (hit) {
            float r = hit_r * 0.6f;
            tx = t->x + frr(-r, r);
            ty = t->y + frr(-r, r);
        } else {
            float a = frr(0, 2 * PI), r = hit_r + frr(10, 45);
            tx = t->x + cosf(a) * r;
            ty = t->y + sinf(a) * r;
        }
        for (int k = 0; k < MAX_ORD; k++) {
            Ordnance *o = &G.ord[k];
            if (o->alive) continue;
            memset(o, 0, sizeof *o);
            o->alive = true;
            o->missile = missile;
            o->fx = j->x; o->fy = j->y; o->tx = tx; o->ty = ty;
            o->dur = missile ? dist2d(j->x, j->y, tx, ty) / D(380) : 0.55f + i * 0.09f;
            o->damage = total / count;
            o->target = (s16)b;
            snprintf(o->shooter, sizeof o->shooter, "%s", j->title);
            break;
        }
    }
    if (hit) glog(LOG_KILL, "%s %s the %s.", j->title, missile ? "launched Harpoons at" : "bombed", t->name);
    else glog(LOG_INFO, "%s missed the %s.", j->title, t->name);
}

static void update_strike_run(Jet *j, float dt) {
    int t = j->strike_target;
    if (t >= 0 && !strike_available(t, j)) t = -1;
    if (t < 0) {
        /* The target was knocked out by someone else: pick another, or head home. */
        t = choose_strike_target(j);
        if (t < 0) {
            j->strike_target = -1;
            j->state = JS_RETURNING;
            return;
        }
        j->strike_target = (s16)t;
        glog(LOG_INFO, "%s retargeting to the %s.", j->title, eb_specs[t].name);
    }
    opportunistic_shot(j);
    float release = D(eb_specs[t].antiship ? 170 : 70);
    if (dist2d(j->x, j->y, eb_specs[t].x, eb_specs[t].y) <= release) {
        release_weapons(j, t);
        j->state = JS_RETURNING;
        return;
    }
    steer(j, eb_specs[t].x, eb_specs[t].y, dt, 1, 1);
}

void update_ordnance(float dt) {
    for (int i = 0; i < MAX_ORD; i++) {
        Ordnance *o = &G.ord[i];
        if (!o->alive) continue;
        o->t += dt;
        if (o->t < o->dur) continue;
        o->alive = false;
        fx_explosion(o->tx, o->ty, 0, o->damage > 0 ? 16 : 9,
                     o->damage > 0 ? RGBF(1, 0.6f, 0.25f, 0.9f) : RGBF(0.55f, 0.5f, 0.4f, 0.7f));
        EBase *b = &G.eb[o->target];
        if (o->damage <= 0 || b->down_left > 0) continue;
        b->health -= o->damage;
        if (b->health <= 0) {
            b->health = 0;
            b->down_left = eb_specs[o->target].down;
            fx_explosion(eb_specs[o->target].x, eb_specs[o->target].y, 0, 60, RGBF(1, 0.5f, 0.2f, 0.95f));
            fx_shock(eb_specs[o->target].x, eb_specs[o->target].y, 120);
            award_strike(o->target, o->shooter);
        }
    }
}

/* Enemy installations: repair timers and SAM fire at your aircraft. */
void update_enemy_bases(float dt) {
    for (int b = 0; b < N_EB; b++) {
        EBase *eb = &G.eb[b];
        const EBSpec *s = &eb_specs[b];
        if (eb->down_left > 0) {
            eb->down_left -= dt;
            if (eb->down_left <= 0) {
                eb->down_left = 0;
                eb->health = s->health;
                glog(LOG_INFO, "The enemy has repaired the %s.", s->name);
            }
            continue;
        }
        if (!unlocked(s->unlock)) continue;
        eb->sam_cd -= dt;
        if (eb->sam_cd > 0) continue;
        Jet *pick = NULL;
        float best = s->sam_r;
        for (int i = 0; i < MAX_JETS; i++) {
            Jet *j = &G.jets[i];
            if (!j->alive || j->state == JS_PARKED) continue;
            float d = dist2d(j->x, j->y, s->x, s->y);
            if (d <= best) { best = d; pick = j; }
        }
        if (!pick || enemy_missiles_on(pick) >= 2) { eb->sam_cd = 0.5f; continue; }
        eb->sam_cd = 2.4f;
        char who[28];
        snprintf(who, sizeof who, "%s SAMs", s->name);
        launch_air_missile(s->x, s->y, false, SENS_RADAR, effective_defense(b) * 0.75f, who, NOREF, jet_ref(pick), NOREF);
        fx_explosion(s->x, s->y, 0, 7, RGBF(1, 1, 1, 0.5f));
    }
}

/* ---------- Patrol and intercept ---------- */

/* Patrols defend your side of the front. On Auto they cover every base; otherwise the chosen one. */
static bool in_patrol_scope(float x, float y, float sx, float sy) {
    if (y > front_y(x) + D(60)) return false;
    if (G.patrol_site < 0) {
        if (dist2d(x, y, sx, sy) < D(750)) return true;
        for (int s = 0; s < N_SITE; s++)
            if (G.sites[s].built && dist2d(site_specs[s].x, site_specs[s].y, x, y) < D(750)) return true;
        return false;
    }
    return dist2d(x, y, sx, sy) < D(800);
}

/* The most urgent hostile this jet can see itself or that the radar network vectors it onto. */
static Contact *find_air_target(const Jet *j, float sx, float sy) {
    const JetSpec *s = &jet_specs[j->spec];
    Contact *best = NULL;
    float best_score = 1e30f;
    for (int ci = 0; ci < MAX_CON; ci++) {
        Contact *c = con_at(ci);
        if (!c->alive) continue;
        if (c->is_mun && !MUN_CRUISE(((Munition *)c)->kind)) continue;
        if (!in_patrol_scope(c->x, c->y, sx, sy)) continue;
        float dj = dist2d(j->x, j->y, c->x, c->y);
        float rcs_f = c->is_mun ? 0.6f : minf(1.3f, powf(c->rcs / 3, 0.25f));
        if (dj > s->sensor_r * rcs_f && G.net[ci] < FIRE_THRESHOLD) continue;
        float to_base = 1e9f;
        for (int k = 0; k < N_SITE; k++)
            if (G.sites[k].built) to_base = minf(to_base, dist2d(site_specs[k].x, site_specs[k].y, c->x, c->y));
        float score = dj + to_base * 0.6f + (c->is_mun ? 400 : 0);
        if (score < best_score) { best_score = score; best = c; }
    }
    return best;
}

static void engage(Jet *j, Contact *t) {
    const JetSpec *s = &jet_specs[j->spec];
    float dx = t->x - j->x, dy = t->y - j->y, dist = sqrtf(dx * dx + dy * dy);
    float off = angle_off(j->heading, atan2f(dy, dx));
    if (j->missiles > 0 && j->cooldown <= 0 && dist <= s->msl_r && dist > 40 && off < 0.9f) {
        if (friendly_missiles_on(t) < t->hits) { fire_aam(j, t); return; }
    }
    if (s->gun_pk > 0 && j->gun_cd <= 0 && dist <= D(80) && off < 0.5f) {
        j->gun_cd = 0.5f;
        float pk = s->gun_pk * (1 - 0.4f * t->maneuver);
        if (t->is_mun) pk *= 0.6f;
        bool hit = frand() < pk;
        float ex = t->x, ey = t->y;
        if (!hit) { ex += frr(-14, 14); ey += frr(-14, 14); }
        fx_tracer(j->x, j->y, 1, ex, ey, t->alt, RGBF(0.7f, 0.9f, 1, 1));
        if (hit) damage_contact(t, NULL, j);
    }
}

static void update_patrol(Jet *j, float dt) {
    const JetSpec *s = &jet_specs[j->spec];
    j->fuel -= dt;
    j->retarget -= dt;
    float sx, sy;
    patrol_station(&sx, &sy);
    bool winchester = s->missiles > 0 && j->missiles == 0;
    if (j->fuel <= 0 || winchester) {
        j->state = JS_RETURNING;
        j->target = NOREF;
        return;
    }
    Contact *t = con_get(j->target);
    if (t && !in_patrol_scope(t->x, t->y, sx, sy)) t = NULL;
    if (!t || j->retarget <= 0) {
        j->retarget = 1.0f;
        t = find_air_target(j, sx, sy);
    }
    j->target = t ? con_ref(t) : NOREF;
    if (!t) {
        j->orbit += dt * 0.7f;
        steer(j, sx + cosf(j->orbit) * D(120), sy + sinf(j->orbit) * D(120), dt, 1, 0.8f);
        return;
    }
    float lead = minf(1.0f, dist2d(j->x, j->y, t->x, t->y) / (s->speed + 1));
    float vx = 0, vy = 0;
    if (t->is_mun) {
        const Munition *m = (const Munition *)t;
        float ax = m->ax - t->x, ay = m->ay - t->y, d = maxf(1, sqrtf(ax * ax + ay * ay));
        vx = ax / d * t->speed; vy = ay / d * t->speed;
    } else {
        const Enemy *e = (const Enemy *)t;
        vx = cosf(e->heading) * t->speed; vy = sinf(e->heading) * t->speed;
    }
    steer(j, t->x + vx * lead, t->y + vy * lead, dt, 1, 1);
    engage(j, t);
}

void update_jets(float dt) {
    for (int s = 0; s < N_SITE; s++) G.sites[s].launch_cd -= dt;
    bool targeted[N_EB] = {false};
    for (int i = 0; i < MAX_JETS; i++) {
        Jet *j = &G.jets[i];
        if (!j->alive) continue;
        j->cooldown -= dt;
        j->gun_cd -= dt;
        if (j->state != JS_PARKED) j->air_t += dt;
        switch (j->state) {
        case JS_PARKED:
            if (j->rearm_left > 0) j->rearm_left -= dt;
            else try_launch(j);
            break;
        case JS_OUTBOUND: update_strike_run(j, dt); break;
        case JS_RETURNING: opportunistic_shot(j); fly_home(j, dt); break;
        case JS_PATROL: update_patrol(j, dt); break;
        }
        if (j->alive && j->state == JS_OUTBOUND && j->strike_target >= 0) targeted[j->strike_target] = true;
    }
    for (int b = 0; b < N_EB; b++) G.eb[b].targeted = targeted[b];
}

/* ---------- Status for the UI ---------- */

int jet_phase(const Jet *j) {
    switch (j->state) {
    case JS_PARKED: return j->rearm_left > 0 ? JP_REARMING : (sortie_mission(j) < 0 ? JP_STANDBY : JP_READY);
    case JS_OUTBOUND: return JP_STRIKING;
    case JS_PATROL: return JP_PATROLLING;
    default: return JP_RETURNING;
    }
}

void jet_status(const Jet *j, char *out, size_t n) {
    const JetSpec *s = &jet_specs[j->spec];
    switch (j->state) {
    case JS_PARKED:
        if (j->rearm_left > 0) snprintf(out, n, "Rearming, %ds", (int)ceilf(j->rearm_left));
        else if (sortie_mission(j) < 0) snprintf(out, n, JET_CAN_STRIKE(s) ? "Standing by: no strike target in range" : "Standing by");
        else snprintf(out, n, "Ready");
        break;
    case JS_OUTBOUND:
        snprintf(out, n, "Striking the %s", j->strike_target >= 0 ? eb_specs[j->strike_target].name : "target");
        break;
    case JS_RETURNING:
        if (j->homeless) snprintf(out, n, "Looking for a place to land");
        else snprintf(out, n, "Returning to %s", site_specs[j->home].name);
        break;
    default: {
        Contact *t = con_get(j->target);
        if (t && !t->is_mun) snprintf(out, n, "Engaging %s", enemy_specs[((Enemy *)t)->spec].name);
        else if (t) snprintf(out, n, "Chasing a cruise missile");
        else snprintf(out, n, "Patrolling, %ds fuel", (int)maxf(0, j->fuel));
    }
    }
}

int eb_assigned(int b) {
    int n = 0;
    for (int i = 0; i < MAX_JETS; i++)
        if (G.jets[i].alive && G.jets[i].state == JS_OUTBOUND && G.jets[i].strike_target == b) n++;
    return n;
}

void set_strike_target(int b) {
    G.strike_target = b;
    if (b >= 0) glog(LOG_INFO, "Strike target set: %s.", eb_specs[b].name);
}
