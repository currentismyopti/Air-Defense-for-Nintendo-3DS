/* The battle: economy, enemy waves and weapons, radar tracking, and your emplacements.
   A straight port of the Mac game's BattleScene, BattleScene+Threats, BattleScene+Defense and GameState. */
#include "sim_int.h"

Game G;

/* ---------- Random numbers and small helpers ---------- */

static u32 rng_state = 0x2545F491u;
void rng_seed(u32 s) { rng_state = s ? s : 0x2545F491u; }
u32 rng_u32(void) {
    u32 x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
float frand(void) { return (float)(rng_u32() >> 8) * (1.0f / 16777216.0f); }
float frr(float a, float b) { return a + (b - a) * frand(); }
int irr(int a, int b) { return b <= a ? a : a + (int)(rng_u32() % (u32)(b - a + 1)); }

float angle_delta(float a, float b) {
    float d = fmodf(b - a, 2 * PI);
    if (d > PI) d -= 2 * PI;
    if (d < -PI) d += 2 * PI;
    return d;
}
float turn_toward(float angle, float target, float max_step) {
    float d = angle_delta(angle, target);
    return angle_delta(0, angle + clampf(d, -max_step, max_step));
}
int wpick(const float *w, int n) {
    if (n <= 0) return -1;
    float total = 0;
    for (int i = 0; i < n; i++) total += w[i];
    if (total <= 0) return irr(0, n - 1);
    float roll = frand() * total;
    for (int i = 0; i < n; i++) {
        if (roll < w[i]) return i;
        roll -= w[i];
    }
    return n - 1;
}

void glog(int kind, const char *fmt, ...) {
    G.log_head = (G.log_head + MAX_LOG - 1) % MAX_LOG;
    LogEntry *e = &G.log[G.log_head];
    e->kind = (u8)kind;
    e->time = G.real_time;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->text, LOG_LEN, fmt, ap);
    va_end(ap);
    if (G.log_n < MAX_LOG) G.log_n++;
}
const LogEntry *log_at(int i) {
    if (i < 0 || i >= G.log_n) return NULL;
    return &G.log[(G.log_head + i) % MAX_LOG];
}

/* ---------- References ---------- */

Contact *con_at(int ci) { return ci < MAX_EN ? &G.en[ci].c : &G.mun[ci - MAX_EN].c; }
Contact *con_get(Ref r) {
    if (r.i < 0 || r.i >= MAX_CON) return NULL;
    Contact *c = con_at(r.i);
    return (c->alive && c->gen == r.g) ? c : NULL;
}
Enemy *en_get(Ref r) {
    if (r.i < 0 || r.i >= MAX_EN) return NULL;
    Enemy *e = &G.en[r.i];
    return (e->c.alive && e->c.gen == r.g) ? e : NULL;
}
Defense *def_get(Ref r) {
    if (r.i < 0 || r.i >= MAX_DEF) return NULL;
    Defense *d = &G.def[r.i];
    return (d->active && d->gen == r.g) ? d : NULL;
}
Jet *jet_get(Ref r) {
    if (r.i < 0 || r.i >= MAX_JETS) return NULL;
    Jet *j = &G.jets[r.i];
    return (j->alive && j->gen == r.g) ? j : NULL;
}
Ref def_ref(const Defense *d) { return d ? (Ref){(s16)(d - G.def), d->gen} : NOREF; }
Ref jet_ref(const Jet *j) { return j ? (Ref){(s16)(j - G.jets), j->gen} : NOREF; }

/* ---------- Geography ---------- */

float coast_x(float y) {
    float oy = y / GS;
    return GS * (1590 + 26 * sinf(oy / 140) + 14 * sinf(oy / 53 + 1));
}
float front_y(float x) {
    float ox = x / GS;
    return GS * (1085 + 22 * sinf(ox / 90) + 12 * sinf(ox / 37 + 2));
}
bool is_water(float x, float y) { return x > coast_x(y); }
bool is_enemy_territory(float x, float y) { return y > front_y(x); }

/* Areas that take damage when hit (relative to the site) and the no-build box around each kind of site. */
const Rect site_fp[4][2] = {
    {{-72, -47, 144, 94}, {-114, -72, 228, 20}},
    {{-55, -38, 110, 76}, {0, 0, 0, 0}},
    {{-110, -10, 220, 20}, {-48, 14, 96, 38}},
    {{-24, -95, 48, 200}, {0, 0, 0, 0}},
};
const int site_nfp[4] = {2, 1, 2, 1};
const Rect site_nb[4] = {{-122, -80, 244, 130}, {-65, -48, 130, 96}, {-120, -20, 240, 80}, {-50, -110, 100, 230}};

static bool rect_has(Rect r, float ox, float oy, float x, float y, float grow) {
    return x >= ox + r.x - grow && x <= ox + r.x + r.w + grow && y >= oy + r.y - grow && y <= oy + r.y + r.h + grow;
}
static bool site_contains(int s, float x, float y) {
    int k = site_specs[s].kind;
    for (int i = 0; i < site_nfp[k]; i++)
        if (rect_has(site_fp[k][i], site_specs[s].x, site_specs[s].y, x, y, 0)) return true;
    return false;
}
static bool site_blocks(int s, float x, float y) {
    return rect_has(site_nb[site_specs[s].kind], site_specs[s].x, site_specs[s].y, x, y, 0);
}
bool site_hit(int s, float x, float y) {
    return rect_has(site_nb[site_specs[s].kind], site_specs[s].x, site_specs[s].y, x, y, 8);
}
void site_parking(int s, int i, float *x, float *y) {
    static const float apron[4][2] = {{-33, 33}, {-11, 33}, {11, 33}, {33, 33}};
    static const float deck[4][2] = {{9, -52}, {9, -22}, {9, 8}, {9, 38}};
    const float (*p)[2] = site_specs[s].kind == SK_CARRIER ? deck : apron;
    i = ((i % 4) + 4) % 4;
    *x = site_specs[s].x + p[i][0];
    *y = site_specs[s].y + p[i][1];
}
/* Where an enemy weapon aimed at this site lands. Unguided weapons scatter around it. */
static void site_aim(int s, bool guided, bool rocket, float *x, float *y) {
    int k = site_specs[s].kind;
    if (guided) {
        Rect r = site_fp[k][irr(0, site_nfp[k] - 1)];
        *x = site_specs[s].x + frr(r.x + 8, r.x + r.w - 8);
        *y = site_specs[s].y + frr(r.y + 4, r.y + r.h - 4);
        return;
    }
    float x0 = site_fp[k][0].x, y0 = site_fp[k][0].y;
    float x1 = x0 + site_fp[k][0].w, y1 = y0 + site_fp[k][0].h;
    for (int i = 1; i < site_nfp[k]; i++) {
        Rect r = site_fp[k][i];
        x0 = minf(x0, r.x); y0 = minf(y0, r.y);
        x1 = maxf(x1, r.x + r.w); y1 = maxf(y1, r.y + r.h);
    }
    float extra = rocket ? 12 : 0;
    float sx = (x1 - x0) * 0.6f + extra, sy = (y1 - y0) * 0.7f + extra;
    *x = site_specs[s].x + (x0 + x1) / 2 + frr(-sx, sx);
    *y = site_specs[s].y + (y0 + y1) / 2 + frr(-sy, sy);
}

/* ---------- Derived values ---------- */

int xp_to_next(void) { return (int)(100 * pow((double)G.level, 1.45)); }
float armor_reduction(void) { return minf(0.6f, G.upg[U_ARMOR] * 0.1f); }
int income_per_second(void) {
    int base = 6 + G.upg[U_DEPOT] * 4 + G.level * 2;
    for (int s = 0; s < N_SITE; s++)
        if (G.sites[s].built) base += site_specs[s].income;
    return (int)floorf(base * settings()->income + 0.5f);
}
float repair_rate(void) { return G.upg[U_REPAIR] * 0.5f; }
float ew_range(void) { int l = G.upg[U_RADAR] - 1; return D(600 + 110.0f * (l > 0 ? l : 0)); }
float ew_eccm(void) { int l = G.upg[U_RADAR] - 1; return 0.45f + 0.1f * (l > 0 ? l : 0); }
bool has_airfield(void) {
    for (int s = 0; s < N_SITE; s++)
        if (G.sites[s].built && site_is_airbase(s)) return true;
    return false;
}
bool awacs_active(void) { return G.upg[U_HAWKEYE] > 0 && has_airfield(); }
int emplacement_cap(void) {
    if (creative()) return 99;
    int cap = 18;
    for (int s = 0; s < N_SITE; s++) {
        if (!G.sites[s].built) continue;
        if (site_specs[s].kind == SK_FOB) cap += 4;
        if (site_specs[s].kind == SK_AIRFIELD) cap += 2;
    }
    return cap;
}
int placed_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_DEF; i++)
        if (G.def[i].active && !G.def[i].fixed) n++;
    return n;
}

/* ---------- Effects ---------- */

static Fx *fx_new(int kind, float life) {
    Fx *f = NULL;
    for (int n = 0; n < MAX_FX; n++) {
        int i = (G.fx_next + n) % MAX_FX;
        if (G.fx[i].kind == FX_NONE) { f = &G.fx[i]; G.fx_next = (i + 1) % MAX_FX; break; }
    }
    if (!f) { f = &G.fx[G.fx_next]; G.fx_next = (G.fx_next + 1) % MAX_FX; }
    memset(f, 0, sizeof *f);
    f->kind = (u8)kind;
    f->life = life;
    return f;
}
void fx_explosion(float x, float y, float alt, float r, u32 color) {
    Fx *f = fx_new(FX_EXPLOSION, 0.35f);
    f->x = x; f->y = y; f->alt = alt; f->r = r; f->color = color;
}
void fx_shock(float x, float y, float r) {
    Fx *f = fx_new(FX_SHOCK, 0.5f);
    f->x = x; f->y = y; f->r = r; f->color = RGBF(1, 0.8f, 0.6f, 0.8f);
}
void fx_tracer(float x0, float y0, float a0, float x1, float y1, float a1, u32 color) {
    Fx *f = fx_new(FX_TRACER, 0.12f);
    f->x = x0; f->y = y0; f->alt = a0; f->x2 = x1; f->y2 = y1; f->r = a1; f->color = color;
}
void fx_puff(float x, float y, float alt, u32 color) {
    Fx *f = fx_new(FX_PUFF, 0.5f);
    f->x = x; f->y = y; f->alt = alt; f->color = color;
}
void fx_cm_burst(float x, float y, float heading) {
    for (int i = 0; i < 8; i++) {
        bool flare = i % 2 == 0;
        Fx *f = fx_new(FX_DOT, 1.0f);
        float back = heading + PI + frr(-1, 1), d = frr(20, 55);
        f->x = x; f->y = y; f->alt = 1;
        f->x2 = cosf(back) * d; f->y2 = sinf(back) * d - 8;
        f->r = flare ? 1.9f : 1.1f;
        f->color = flare ? RGBF(1, 0.85f, 0.45f, 1) : RGBF(0.78f, 0.78f, 0.78f, 0.85f);
    }
}
void fx_label(const char *text, float x, float y, float alt, u32 color) {
    Fx *f = fx_new(FX_LABEL, 0.7f);
    f->x = x; f->y = y + 10; f->alt = alt; f->color = color;
    snprintf(f->text, sizeof f->text, "%s", text);
}
void fx_update(float dt) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &G.fx[i];
        if (f->kind == FX_NONE) continue;
        f->t += dt;
        if (f->t >= f->life) f->kind = FX_NONE;
    }
}

/* ---------- Economy and progression ---------- */

void check_level_up(void) {
    if (creative()) return; /* In Creative the threat level is set by hand. */
    while (G.xp >= xp_to_next()) {
        G.xp -= xp_to_next();
        G.level++;
        int bonus = 100 * G.level;
        G.money += bonus;
        glog(LOG_INFO, "Threat level %d. Command sends $%d in funding.", G.level, bonus);
        for (int i = 0; i < N_SHOP_DEF; i++)
            if (def_specs[i].unlock == G.level) glog(LOG_INFO, "Unlocked: %s", def_specs[i].name);
        for (int i = 0; i < N_JET; i++)
            if (jet_specs[i].unlock == G.level) glog(LOG_INFO, "Unlocked aircraft: %s", jet_specs[i].name);
        for (int i = 1; i < N_SITE; i++)
            if (site_specs[i].unlock == G.level) glog(LOG_INFO, "New site available: %s", site_specs[i].name);
        for (int i = 0; i < N_UPG; i++)
            if (upg_specs[i].unlock == G.level) glog(LOG_INFO, "Unlocked: %s", upg_specs[i].title);
        for (int i = 0; i < N_ENEMY; i++)
            if (enemy_specs[i].min_level == G.level)
                glog(LOG_DANGER, "Intel: %s now operating in theater.", enemy_specs[i].name);
        if (G.level == settings()->raid_lvl) glog(LOG_DANGER, "Intel: expect coordinated raids from now on.");
        if (G.level == settings()->msl_lvl) glog(LOG_DANGER, "Intel: enemy Iskander batteries are in range.");
    }
}

static void award_kill(int bounty, int xp, const char *name, const char *shooter) {
    int pay = (int)(bounty * settings()->bounty);
    if (!creative()) G.money += pay;
    G.kills++;
    G.xp += xp;
    char payout[16] = "";
    if (!creative()) snprintf(payout, sizeof payout, " +$%d", pay);
    if (shooter) glog(LOG_KILL, "Splash one: %s downed by %s.%s", name, shooter, payout);
    else glog(LOG_KILL, "Splash one: %s down.%s", name, payout);
    check_level_up();
}
static void award_intercept(int bounty) {
    if (!creative()) G.money += (int)(bounty * settings()->bounty);
    G.intercepts++;
    G.xp += bounty / 5 > 2 ? bounty / 5 : 2;
    check_level_up();
}
void award_strike(int eb, const char *shooter) {
    const EBSpec *s = &eb_specs[eb];
    int reward = (int)(s->reward * (1 + 0.05f * (G.level - 1)) * settings()->bounty);
    if (!creative()) G.money += reward;
    G.xp += s->xp;
    char pay[16] = "";
    if (!creative()) snprintf(pay, sizeof pay, " +$%d", reward);
    glog(LOG_KILL, "%s destroyed the %s! %s for %ds.%s", shooter, s->name, s->effect, (int)s->down, pay);
    check_level_up();
}

/* ---------- Selection and placement ---------- */

void deselect_all(void) {
    G.sel_def = NOREF;
    G.sel_site = -1;
    G.sel_enemy = NOREF;
}
static void select_def(Defense *d) { deselect_all(); G.sel_def = def_ref(d); }
static void select_site(int s) { deselect_all(); G.sel_site = s; }
static void select_enemy(Enemy *e) { deselect_all(); if (e) G.sel_enemy = con_ref(&e->c); }

void cancel_placement(void) {
    G.placing = -1;
    G.relocating = NOREF;
}

static void clear_tracks(int ci) {
    G.net[ci] = 0;
    for (int i = 0; i < MAX_DEF; i++) G.def[i].trk[ci] = 0;
}

static Defense *add_defense(int spec, float x, float y) {
    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (d->active) continue;
        u16 g = (u16)(d->gen + 1);
        memset(d, 0, sizeof *d);
        d->gen = g;
        d->active = true;
        d->spec = (u8)spec;
        d->mount = -1;
        d->x = x; d->y = y;
        d->hp = def_specs[spec].hp;
        d->ammo = def_specs[spec].mag;
        d->scan_t = frr(0, def_specs[spec].scan);
        d->aim = PI / 2;
        d->born = G.real_time;
        return d;
    }
    return NULL;
}

const char *placement_problem(float x, float y, const Defense *moving) {
    if (x < 16 || y < 16 || x > WORLD_W - 16 || y > WORLD_H - 16) return "Off the map";
    if (is_water(x, y)) return "That's open water";
    if (is_enemy_territory(x, y)) return "Enemy territory";
    if (!creative()) {
        bool inside = false;
        for (int s = 0; s < N_SITE && !inside; s++)
            inside = G.sites[s].built && dist2d(site_specs[s].x, site_specs[s].y, x, y) <= site_specs[s].deploy_r;
        if (!inside) return "Outside your perimeter";
    }
    for (int s = 0; s < N_SITE; s++)
        if (site_blocks(s, x, y)) return "Too close to a base";
    for (int i = 0; i < MAX_DEF; i++) {
        const Defense *d = &G.def[i];
        if (d->active && d != moving && dist2d(d->x, d->y, x, y) < 34) return "Too close to another unit";
    }
    if (!moving && placed_count() >= emplacement_cap()) return "All emplacements used";
    return NULL;
}

const char *ghost_problem(void) {
    static char need[24];
    const Defense *moving = def_get(G.relocating);
    const char *p = placement_problem(G.ghost_x, G.ghost_y, moving);
    if (!p && G.placing >= 0 && !can_afford(def_specs[G.placing].cost)) {
        snprintf(need, sizeof need, "Need $%d", def_specs[G.placing].cost);
        return need;
    }
    return p;
}

bool begin_placement(int spec) {
    const DefSpec *s = &def_specs[spec];
    if (G.over || !unlocked(s->unlock) || !can_afford(s->cost) || emplacement_cap() - placed_count() <= 0) return false;
    G.sp_placing = false;
    deselect_all();
    G.relocating = NOREF;
    G.placing = spec;
    return true;
}

bool begin_relocate(void) {
    Defense *d = def_get(G.sel_def);
    if (!d || d->fixed || G.over) return false;
    G.placing = -1;
    G.relocating = def_ref(d);
    G.ghost_x = d->x;
    G.ghost_y = d->y;
    deselect_all();
    return true;
}

bool confirm_placement(void) {
    Defense *moving = def_get(G.relocating);
    if (G.relocating.i >= 0) {
        if (!moving) { cancel_placement(); return false; }
        if (placement_problem(G.ghost_x, G.ghost_y, moving)) { G.ghost_nudge = 0.2f; return false; }
        moving->x = G.ghost_x;
        moving->y = G.ghost_y;
        memset(moving->trk, 0, sizeof moving->trk);
        glog(LOG_INFO, "%s relocated.", def_specs[moving->spec].name);
        cancel_placement();
        select_def(moving);
        return true;
    }
    if (G.placing < 0) return false;
    const DefSpec *s = &def_specs[G.placing];
    if (ghost_problem()) { G.ghost_nudge = 0.2f; return false; }
    Defense *d = add_defense(G.placing, G.ghost_x, G.ghost_y);
    if (!d) { G.ghost_nudge = 0.2f; return false; }
    if (!creative()) G.money -= s->cost;
    glog(LOG_INFO, "%s emplaced.", s->name);
    cancel_placement();
    return true;
}

int sell_value(const Defense *d) { return (int)(def_specs[d->spec].cost * 0.6f / 10) * 10; }
int repair_cost(const Defense *d) { return (int)ceilf((def_specs[d->spec].hp - d->hp) * 4); }

void sell_selected(void) {
    Defense *d = def_get(G.sel_def);
    if (G.over || !d || d->fixed) return;
    int refund = sell_value(d);
    d->active = false;
    deselect_all();
    if (creative()) glog(LOG_INFO, "%s removed.", def_specs[d->spec].name);
    else { G.money += refund; glog(LOG_INFO, "%s sold for $%d.", def_specs[d->spec].name, refund); }
}

void repair_selected(void) {
    Defense *d = def_get(G.sel_def);
    if (!d || G.over) return;
    int cost = repair_cost(d);
    if (cost <= 0 || !can_afford(cost)) return;
    if (!creative()) G.money -= cost;
    d->hp = def_specs[d->spec].hp;
}

/* ---------- Bases ---------- */

int site_price(int s) {
    int cost = site_specs[s].cost;
    return G.sites[s].destroyed ? (int)(cost * 0.6f / 50) * 50 : cost;
}
int site_repair_cost(int s) {
    return G.sites[s].built ? (int)ceilf((G.sites[s].max_hp - G.sites[s].hp) * 5) : 0;
}
int total_repair_cost(void) {
    int t = 0;
    for (int s = 0; s < N_SITE; s++) t += site_repair_cost(s);
    return t;
}

bool build_site(int s) {
    Site *st = &G.sites[s];
    int price = site_price(s);
    if (G.over || st->built || !unlocked(site_specs[s].unlock) || !can_afford(price)) return false;
    st->built = true;
    st->destroyed = false;
    st->hp = st->max_hp;
    st->born = G.real_time;
    if (site_specs[s].kind == SK_CARRIER) {
        /* The carrier's own guns and missiles. */
        static const struct { int spec; float dx, dy; } mounts[3] = {
            {D_PHALANX, -34, 78}, {D_PHALANX, 34, -70}, {D_SEASPARROW, -36, -40}};
        for (int i = 0; i < 3; i++) {
            Defense *d = add_defense(mounts[i].spec, site_specs[s].x + mounts[i].dx, site_specs[s].y + mounts[i].dy);
            if (d) { d->fixed = true; d->mount = (s16)s; }
        }
    }
    if (!creative()) G.money -= price;
    glog(LOG_INFO, "%s is operational.", site_specs[s].name);
    return true;
}

void repair_site(int s) {
    int cost = site_repair_cost(s);
    if (G.over || cost <= 0 || !can_afford(cost)) return;
    if (!creative()) G.money -= cost;
    G.sites[s].hp = G.sites[s].max_hp;
}

void repair_all_sites(void) {
    int cost = total_repair_cost();
    if (G.over || cost <= 0 || !can_afford(cost)) return;
    if (!creative()) G.money -= cost;
    for (int s = 0; s < N_SITE; s++)
        if (G.sites[s].built) G.sites[s].hp = G.sites[s].max_hp;
    glog(LOG_INFO, "All bases repaired.");
}

static void apply_bunkers(int level) {
    for (int s = 0; s < N_SITE; s++) {
        Site *st = &G.sites[s];
        float nm = site_specs[s].hp * settings()->base_hp + (site_specs[s].kind == SK_HQ ? 25 : 15) * level;
        if (st->built) st->hp += nm - st->max_hp; else st->hp = nm;
        st->max_hp = nm;
    }
}

/* ---------- Upgrades ---------- */

int upgrade_cost(int u) {
    int l = G.upg[u];
    if (l >= upg_specs[u].max) return -1;
    return (int)floor(upg_specs[u].base * pow(upg_specs[u].growth, l) / 10 + 0.5) * 10;
}

bool buy_upgrade(int u) {
    int cost = upgrade_cost(u);
    if (G.over || !unlocked(upg_specs[u].unlock) || cost < 0 || !can_afford(cost)) return false;
    if (u >= N_BASE_UPG && !has_airfield()) return false;
    if (!creative()) G.money -= cost;
    G.upg[u]++;
    if (u == U_BUNKERS) apply_bunkers(G.upg[U_BUNKERS]);
    glog(LOG_INFO, "%s upgraded to level %d.", upg_specs[u].title, G.upg[u]);
    return true;
}

void upgrade_detail(int u, char *out, size_t n) {
    int l = G.upg[u];
    switch (u) {
    case U_RADAR:
        if (l == 0) snprintf(out, n, "Networks every launcher. Cues weapons onto targets their own sensors can't hold yet.");
        else snprintf(out, n, "Coverage %.1f km vs a 1 m2 target. ECCM %d%%.", ew_range() * 25 / GS / 1000, (int)(ew_eccm() * 100 + 0.5f));
        break;
    case U_ARMOR: snprintf(out, n, "Cuts damage to every base and emplacement. Now %d%%.", (int)(armor_reduction() * 100 + 0.5f)); break;
    case U_BUNKERS: snprintf(out, n, "+25 HP for HQ and +15 for other bases per level."); break;
    case U_DEPOT: snprintf(out, n, "+$4/s income per level. Now $%d/s in total.", income_per_second()); break;
    case U_REPAIR: snprintf(out, n, "Patches up bases and emplacements. Now %.1f HP/s each.", repair_rate()); break;
    case U_MISSILES: snprintf(out, n, "+8%% air-to-air hit chance per level. Level 2 makes Sidewinders all-aspect and harder to flare."); break;
    case U_PRECISION: snprintf(out, n, "Strikes hit more often and do 10%% more damage per level."); break;
    case U_ECM: snprintf(out, n, "Enemy missiles are less accurate against your jets, and chaff works better."); break;
    case U_GROUND: snprintf(out, n, "Rearm and repair 18%% faster per level."); break;
    case U_TANKER: snprintf(out, n, "+1 strike range and +30%% patrol time per level. Lets A-4s reach deeper targets."); break;
    case U_SHELTERS: snprintf(out, n, "Protects parked aircraft from bombs and missiles: %d%% now, 45%% per level.", 45 * l); break;
    case U_HAWKEYE:
        snprintf(out, n, l > 0 ? "On station behind the HQ. Feeds radar tracks to every SAM and fighter."
                               : "Airborne radar. Feeds tracks to every SAM and vectors fighters onto raids.");
        break;
    default: out[0] = 0;
    }
}

/* ---------- Spawning ---------- */

bool eb_down(int b) { return G.eb[b].down_left > 0; }

/* Multiplies the time between enemy waves after your retaliation strikes. */
static float sortie_multiplier(void) {
    float f = 1;
    if (eb_down(EB_AIRSTRIP)) f /= 0.6f;
    if (eb_down(EB_AIRBASE)) f *= 2.5f;
    if (eb_down(EB_CARRIER)) f /= 0.75f;
    return f;
}

/* 0 = from enemy airfields in the north, 1 = the western flank, 2 = from the enemy carrier in the east. */
static void spawn_point(int edge, float *x, float *y) {
    if (edge == 0) {
        int opts[2], n = 0;
        if (!eb_down(EB_AIRSTRIP)) opts[n++] = EB_AIRSTRIP;
        if (!eb_down(EB_AIRBASE)) opts[n++] = EB_AIRBASE;
        if (n) {
            int b = opts[irr(0, n - 1)];
            *x = eb_specs[b].x + frr(-40, 40);
            *y = eb_specs[b].y + frr(-20, 20);
        } else {
            *x = frr(D(200), D(1400));
            *y = WORLD_H + 40;
        }
    } else if (edge == 1) {
        *x = -40;
        *y = frr(D(450), D(1050));
    } else if (!eb_down(EB_CARRIER)) {
        *x = eb_specs[EB_CARRIER].x + frr(-20, 20);
        *y = eb_specs[EB_CARRIER].y + frr(-40, 40);
    } else {
        *x = WORLD_W + 40;
        *y = frr(D(800), D(1300));
    }
}

/* Where an enemy aircraft goes home to after its attack. */
static void egress_point(const Enemy *e, float *x, float *y) {
    static const int launch[3] = {EB_AIRSTRIP, EB_AIRBASE, EB_CARRIER};
    float best = 1e9f;
    int pick = -1;
    for (int i = 0; i < 3; i++) {
        int b = launch[i];
        if (eb_down(b)) continue;
        float d = dist2d(eb_specs[b].x, eb_specs[b].y, e->c.x, e->c.y);
        if (d < best) { best = d; pick = b; }
    }
    if (pick >= 0) { *x = eb_specs[pick].x; *y = eb_specs[pick].y; }
    else { *x = e->c.x; *y = WORLD_H + 250; }
}

/* Raiders usually come in from the side of the map nearest their target. */
static int edge_toward(int site) {
    float x = site_specs[site].x;
    if (site_specs[site].kind == SK_CARRIER) return chance(0.75f) ? 2 : 0;
    if (x < D(500)) return chance(0.5f) ? 0 : 1;
    if (x > D(1100)) return chance(0.5f) ? 0 : 2;
    static const int opts[5] = {0, 0, 0, 1, 2};
    return opts[irr(0, 4)];
}

int random_site(void) {
    static const float kind_w[4] = {1.0f, 0.8f, 1.1f, 1.0f};
    int pool[N_SITE], n = 0;
    float w[N_SITE];
    for (int s = 0; s < N_SITE; s++) {
        if (!G.sites[s].built) continue;
        pool[n] = s;
        w[n++] = kind_w[site_specs[s].kind];
    }
    int i = wpick(w, n);
    return i < 0 ? 0 : pool[i];
}

/* Picks one of your emplacements, favoring expensive ones near `site`, or NULL to hit a base instead. */
static Defense *random_emplacement(float p, bool radar_only, int site) {
    if (!chance(p)) return NULL;
    static int pool[MAX_DEF];
    static float w[MAX_DEF];
    int n = 0;
    for (int i = 0; i < MAX_DEF; i++) {
        const Defense *d = &G.def[i];
        if (!d->active || (radar_only && def_specs[d->spec].sensor != SENS_RADAR)) continue;
        float wt = powf((float)def_specs[d->spec].cost, 0.7f);
        if (site >= 0) wt /= 1 + dist2d(d->x, d->y, site_specs[site].x, site_specs[site].y) / D(350);
        pool[n] = i;
        w[n++] = wt;
    }
    int i = wpick(w, n);
    return i < 0 ? NULL : &G.def[pool[i]];
}

static bool any_jets(void) {
    for (int i = 0; i < MAX_JETS; i++)
        if (G.jets[i].alive) return true;
    return false;
}

/* Newer types peak in frequency a couple of levels after they first appear.
   mode 0: wave (no sweeps without jets to hunt), 1: raid escort, 2: raid striker. */
static int pick_enemy(int level, int mode) {
    bool hasj = any_jets();
    int pool[N_ENEMY], n = 0;
    float w[N_ENEMY];
    for (int i = 0; i < N_ENEMY; i++) {
        const EnemySpec *s = &enemy_specs[i];
        if (s->min_level > level) continue;
        bool sweep_ok = s->role != ROLE_SWEEP || hasj;
        bool ok = mode == 0 ? sweep_ok
                : mode == 1 ? (s->role != ROLE_STRIKER && sweep_ok)
                            : (s->role == ROLE_STRIKER || s->sead > 0.5f);
        if (!ok) continue;
        float d = (level - s->min_level - 2) / 4.0f;
        pool[n] = i;
        w[n++] = expf(-d * d) + 0.15f;
    }
    if (n == 0) return 0;
    return pool[wpick(w, n)];
}

static Enemy *spawn_enemy(int spec, float x, float y, int site, int level) {
    Enemy *e = NULL;
    for (int i = 0; i < MAX_EN; i++) {
        if (G.en[i].c.alive) continue;
        e = &G.en[i];
        u16 g = (u16)(e->c.gen + 1);
        memset(e, 0, sizeof *e);
        e->c.gen = g;
        e->c.ci = (s16)i;
        clear_tracks(i);
        break;
    }
    if (!e) return NULL;
    const EnemySpec *s = &enemy_specs[spec];
    level += settings()->lvl_bonus;
    float l = (float)(level > 1 ? level - 1 : 0);
    e->c.alive = true;
    e->c.x = x; e->c.y = y;
    e->c.rcs = s->rcs;
    e->c.ir = s->ir;
    e->c.ecm = s->ecm > 0 ? minf(1, s->ecm + 0.02f * l) : 0;
    e->c.flares = minf(0.75f, s->flares + 0.01f * l);
    e->c.maneuver = minf(0.95f, s->maneuver + 0.008f * l);
    e->c.speed = s->speed * minf(1.4f, 1 + 0.015f * l);
    e->c.hits = s->hits + (int)(l / 9);
    e->spec = (u8)spec;
    e->heading = atan2f(site_specs[site].y - y, site_specs[site].x - x);
    e->turn_rate = 1.2f + s->maneuver * 1.6f;
    e->mun_left = s->mun_count + (int)(l / 5);
    e->bounty = (int)(s->bounty * (1 + 0.05f * l));
    e->cm = s->cm + (int)(l / 6);
    e->aa_left = s->aa;
    e->aa_cd = frr(1.0f, 2.5f);
    e->target_site = (s16)site;
    e->release_t = frr(0, 0.3f);
    e->dogfight = NOREF;
    e->strike_def = def_ref(random_emplacement(s->sead, s->mun == M_ARM, site));
    return e;
}

static void spawn_wave(int level) {
    int spec = pick_enemy(level, 0);
    int half = level / 2;
    int count = 1 + irr(0, half < 4 ? half : 4) + (level >= 2 ? settings()->extra : 0);
    int target = random_site();
    float sx, sy;
    spawn_point(edge_toward(target), &sx, &sy);
    float side = chance(0.5f) ? 1 : -1;
    for (int i = 0; i < count; i++) spawn_enemy(spec, sx + i * 32 * side, sy + i * 32, target, level);
    float pace = sortie_multiplier() * settings()->spawn_iv;
    G.spawn_t = maxf(2.2f, 10.5f - level * 0.55f) * frr(0.75f, 1.25f) * pace;
}

/* A coordinated package: fighter escorts up front, strike aircraft behind, all aimed at one base. */
static void spawn_raid(int level, bool forced) {
    if (!forced) {
        G.raid_t = maxf(35, 80 - level * 3.0f) * frr(0.85f, 1.15f) * settings()->spawn_iv;
        if (level < settings()->raid_lvl || eb_down(EB_AIRBASE)) return;
    }
    int target = random_site();
    float sx, sy;
    spawn_point(edge_toward(target), &sx, &sy);
    int pool_level = creative() ? (level > 11 ? level : 11) : level;
    int escorts = level >= 7 ? 2 : 1;
    int strikers = 1 + level / 3;
    if (strikers > 7) strikers = 7;
    float toward = atan2f(site_specs[target].y - sy, site_specs[target].x - sx);
    float bx = -cosf(toward), by = -sinf(toward), lx = -sinf(toward), ly = cosf(toward);
    int n = 0;
    for (int k = 0; k < escorts + strikers; k++) {
        int spec = pick_enemy(pool_level, k < escorts ? 1 : 2);
        float row = (float)(n / 3), col = (float)(n % 3) - 1;
        spawn_enemy(spec, sx + bx * row * 45 + lx * col * 40, sy + by * row * 45 + ly * col * 40, target, level);
        n++;
    }
    glog(LOG_DANGER, "Raid inbound: %d aircraft heading for %s.", n, site_specs[target].name);
}

/* ---------- Enemy weapons ---------- */

static Munition *spawn_munition(int kind, float x, float y, float ax, float ay, int level, Defense *target) {
    Munition *m = NULL;
    for (int i = 0; i < MAX_MUN; i++) {
        if (G.mun[i].c.alive) continue;
        m = &G.mun[i];
        u16 g = (u16)(m->c.gen + 1);
        memset(m, 0, sizeof *m);
        m->c.gen = g;
        m->c.ci = (s16)(MAX_EN + i);
        clear_tracks(MAX_EN + i);
        break;
    }
    if (!m) return NULL;
    const MunSpec *s = &mun_specs[kind];
    float l = (float)(level > 1 ? level - 1 : 0);
    m->c.alive = true;
    m->c.is_mun = true;
    m->c.x = x; m->c.y = y;
    m->c.rcs = s->rcs; m->c.ir = s->ir; m->c.maneuver = s->maneuver;
    m->c.speed = s->speed * GS * minf(1.35f, 1 + 0.012f * l);
    m->c.hits = 1;
    m->c.alt = 1;
    m->kind = (u8)kind;
    m->ax = ax; m->ay = ay;
    m->damage = s->damage * (1 + 0.04f * l);
    m->target_def = def_ref(target);
    m->d0 = maxf(1, dist2d(x, y, ax, ay));
    return m;
}

static void launch_munition(Enemy *e) {
    int kind = enemy_specs[e->spec].mun;
    float ax, ay;
    Defense *homing = NULL, *d = def_get(e->strike_def);
    if (d) {
        float spread = MUN_GUIDED(kind) ? 0 : 30;
        ax = d->x + frr(-spread, spread);
        ay = d->y + frr(-spread, spread);
        if (MUN_GUIDED(kind)) homing = d;
    } else {
        site_aim(e->target_site, MUN_GUIDED(kind), kind == M_ROCKET, &ax, &ay);
    }
    spawn_munition(kind, e->c.x, e->c.y, ax, ay, G.level, homing);
}

static void hq_destroyed(void) {
    if (G.over) return;
    G.sites[0].hp = 0;
    cancel_placement();
    deselect_all();
    G.over = true;
    glog(LOG_DANGER, "FOB Anvil has fallen.");
}

static void destroy_site(int s, const char *source) {
    const SiteSpec *sp = &site_specs[s];
    fx_explosion(sp->x, sp->y, 0, 50, RGBF(1, 0.45f, 0.2f, 0.95f));
    fx_shock(sp->x, sp->y, 110);
    if (sp->kind == SK_HQ) { hq_destroyed(); return; }
    Site *st = &G.sites[s];
    st->built = false;
    st->destroyed = true;
    st->hp = 0;
    /* A sunk carrier takes its guns with it. */
    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (d->active && d->fixed && d->mount == s) {
            if (def_get(G.sel_def) == d) deselect_all();
            d->active = false;
        }
    }
    if (site_is_airbase(s)) {
        for (int i = 0; i < MAX_JETS; i++) {
            Jet *j = &G.jets[i];
            if (!j->alive || j->home != s) continue;
            if (j->state == JS_PARKED) {
                char why[60];
                if (sp->kind == SK_CARRIER) snprintf(why, sizeof why, "lost when %s went down", sp->name);
                else snprintf(why, sizeof why, "destroyed on the ground at %s", sp->name);
                destroy_jet(j, why);
            } else {
                int field = airbase_with_space(&jet_specs[j->spec]);
                if (field >= 0) { j->parking = (s16)free_parking_index(field); j->home = (s16)field; }
                else j->homeless = true;
            }
        }
    }
    glog(LOG_DANGER, "%s has been destroyed by %s! Rebuild it from the Bases tab.", sp->name, source);
}

static void damage_site(int s, float raw, const char *source) {
    Site *st = &G.sites[s];
    if (!st->built || G.over) return;
    float dmg = raw * (1 - armor_reduction());
    float floor_hp = (creative() && G.indestructible) ? 1 : 0;
    st->hp = maxf(floor_hp, st->hp - dmg);
    if (dmg >= 5) glog(LOG_DANGER, "%s hit by %s, -%d HP", site_specs[s].name, source, (int)(dmg + 0.5f));
    if (st->hp <= 0) destroy_site(s, source);
}

static void destroy_defense(Defense *d, const char *source) {
    d->active = false;
    fx_explosion(d->x, d->y, 0, 28, RGBF(1, 0.45f, 0.2f, 0.95f));
    fx_shock(d->x, d->y, 40);
    if (G.sel_def.i == d - G.def) deselect_all();
    if (G.relocating.i == d - G.def) cancel_placement();
    glog(LOG_DANGER, "%s destroyed by %s!", def_specs[d->spec].name, source);
}

static void damage_defense(Defense *d, float raw, const char *source) {
    if (!d->active) return;
    d->hp -= raw * (1 - armor_reduction()) * (def_specs[d->spec].pd ? 0.5f : 1);
    if (creative() && G.indestructible) d->hp = maxf(1, d->hp);
    if (d->hp <= 0) destroy_defense(d, source);
}

/* Damages any base hit, emplacements inside the blast, and aircraft parked nearby. */
static void resolve_impact(Munition *m) {
    const MunSpec *s = &mun_specs[m->kind];
    float px = m->ax, py = m->ay;
    float dmg = m->damage * settings()->dmg;
    bool hit = false;
    for (int i = 0; i < N_SITE; i++) {
        if (G.sites[i].built && site_contains(i, px, py)) {
            hit = true;
            damage_site(i, dmg, s->name);
        }
    }
    float reach = maxf(16, s->splash);
    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (!d->active) continue;
        float r = dist2d(d->x, d->y, px, py);
        if (r > reach) continue;
        hit = true;
        damage_defense(d, dmg * (r <= 16 ? 1 : 0.5f), s->name);
    }
    float jet_reach = maxf(20, s->splash);
    float protection = 0.45f * G.upg[U_SHELTERS];
    for (int i = 0; i < MAX_JETS; i++) {
        Jet *j = &G.jets[i];
        if (!j->alive || j->state != JS_PARKED || dist2d(j->x, j->y, px, py) > jet_reach) continue;
        hit = true;
        if (frand() >= protection) damage_jet(j, s->name, true);
    }
    if (hit) fx_explosion(px, py, 0, 8 + m->damage * 0.5f + s->splash * 0.3f, RGBF(1, 0.55f, 0.2f, 0.9f));
    else fx_explosion(px, py, 0, 7 + s->splash * 0.2f, RGBF(0.55f, 0.5f, 0.4f, 0.7f));
    if (MUN_BALLISTIC(m->kind)) fx_shock(px, py, s->splash * 1.4f);
}

static void update_munitions(float dt) {
    for (int i = 0; i < MAX_MUN; i++) {
        Munition *m = &G.mun[i];
        if (!m->c.alive) continue;
        Defense *d = def_get(m->target_def);
        /* Point-defense radar decoys can pull an anti-radiation missile off its target. */
        if (m->kind == M_ARM && !m->decoy_checked && d && def_specs[d->spec].pd
            && dist2d(m->c.x, m->c.y, d->x, d->y) < D(160)) {
            m->decoy_checked = true;
            if (chance(0.55f)) {
                float a = frr(0, 2 * PI);
                m->ax = d->x + cosf(a) * 80;
                m->ay = d->y + sinf(a) * 80;
                m->target_def = NOREF;
                fx_explosion(m->ax, m->ay, 0, 5, RGBF(0.5f, 0.85f, 1, 0.8f));
                fx_label("RADAR DECOY", d->x, d->y, 0, RGBF(0.55f, 0.85f, 1, 1));
                d = NULL;
            }
        }
        /* Guided weapons follow an emplacement if it gets moved. */
        if (d && !MUN_BALLISTIC(m->kind)) { m->ax = d->x; m->ay = d->y; }
        float tx = m->ax - m->c.x, ty = m->ay - m->c.y;
        float dist = sqrtf(tx * tx + ty * ty);
        float step = m->c.speed * dt;
        if (dist <= step) {
            m->c.alive = false;
            resolve_impact(m);
            continue;
        }
        m->c.x += tx / dist * step;
        m->c.y += ty / dist * step;
        /* Height above the ground, used for stereo depth and shadows. */
        float frac = clampf(dist / m->d0, 0, 1);
        if (MUN_BALLISTIC(m->kind)) m->c.alt = (m->kind == M_SRBM && m->marker && m->d0 < D(1500)) ? 0.3f + 2.9f * sinf(PI * frac) : 0.2f + 3.0f * frac;
        else if (m->kind == M_ALCM || m->kind == M_CRUISE || m->kind == M_KH59) m->c.alt = 0.35f * minf(1, frac * 6);
        else m->c.alt = frac;
        if (MUN_BALLISTIC(m->kind) || m->kind == M_ALCM || m->kind == M_CRUISE) {
            m->trail_t -= dt;
            if (m->trail_t <= 0) {
                bool b = MUN_BALLISTIC(m->kind);
                m->trail_t = b ? 0.03f : 0.07f;
                fx_puff(m->c.x, m->c.y, m->c.alt, b ? RGBF(1, 0.6f, 0.3f, 0.55f) : RGBF(0.7f, 0.7f, 0.7f, 0.35f));
            }
        }
    }
}

/* ---------- Hostile flight ---------- */

/* Fighters turn toward your jets that come close; sweep fighters hunt them across the map. */
static void update_dogfight_target(Enemy *e) {
    const EnemySpec *s = &enemy_specs[e->spec];
    if (s->role == ROLE_STRIKER || e->aa_left <= 0) { e->dogfight = NOREF; return; }
    float reach = D(s->role == ROLE_SWEEP ? 1100 : 380);
    Jet *cur = jet_get(e->dogfight);
    if (cur && cur->state != JS_PARKED && dist2d(cur->x, cur->y, e->c.x, e->c.y) < reach * 1.4f) return;
    float best = reach;
    Jet *pick = NULL;
    for (int i = 0; i < MAX_JETS; i++) {
        Jet *j = &G.jets[i];
        if (!j->alive || j->state == JS_PARKED) continue;
        float d = dist2d(j->x, j->y, e->c.x, e->c.y);
        if (d < best) { best = d; pick = j; }
    }
    e->dogfight = jet_ref(pick);
}

static void fire_at_jets(Enemy *e) {
    const EnemySpec *s = &enemy_specs[e->spec];
    if (e->aa_left <= 0 || e->aa_cd > 0) return;
    float arc = s->role == ROLE_STRIKER ? 1.8f : 1.0f;
    float best = 1e9f;
    Jet *pick = NULL;
    for (int i = 0; i < MAX_JETS; i++) {
        Jet *j = &G.jets[i];
        if (!j->alive || j->state == JS_PARKED) continue;
        float d = dist2d(j->x, j->y, e->c.x, e->c.y);
        if (d > s->aa_r || angle_off(e->heading, atan2f(j->y - e->c.y, j->x - e->c.x)) >= arc) continue;
        if (d < best) { best = d; pick = j; }
    }
    if (!pick || enemy_missiles_on(pick) >= 2) return;
    e->aa_left--;
    e->aa_cd = 2.5f;
    launch_air_missile(e->c.x, e->c.y, false, s->aa_r > 260 ? SENS_RADAR : SENS_IR, s->aa_pk, s->name,
                       NOREF, jet_ref(pick), NOREF);
}

static void update_enemies(float dt) {
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &G.en[i];
        if (!e->c.alive) continue;
        const EnemySpec *s = &enemy_specs[e->spec];
        e->age += dt;
        e->cm_window -= dt;
        e->cm_cd -= dt;
        e->aa_cd -= dt;
        e->c.alt = minf(1, 0.15f + e->age / 1.5f);
        Defense *st = def_get(e->strike_def);
        if (!st) e->strike_def = NOREF;
        if (!G.sites[e->target_site].built) e->target_site = (s16)random_site();
        update_dogfight_target(e);

        float mx = st ? st->x : site_specs[e->target_site].x;
        float my = st ? st->y : site_specs[e->target_site].y;
        float gx = e->egress ? e->ex : mx, gy = e->egress ? e->ey : my;
        Jet *dog = jet_get(e->dogfight);
        if (dog) {
            const JetSpec *js = &jet_specs[dog->spec];
            gx = dog->x + cosf(dog->heading) * js->speed * 0.4f;
            gy = dog->y + sinf(dog->heading) * js->speed * 0.4f;
        }
        float desired = atan2f(gy - e->c.y, gx - e->c.x);
        float dist_goal = dist2d(e->c.x, e->c.y, mx, my);
        /* Evasive weave once inside the defended zone */
        if (!e->egress && !dog && dist_goal < D(500)) desired += sinf(e->age * 3.1f) * e->c.maneuver * 0.5f;
        e->heading = turn_toward(e->heading, desired, e->turn_rate * dt);
        float step = e->c.speed * dt;
        e->c.x += cosf(e->heading) * step;
        e->c.y += sinf(e->heading) * step;

        fire_at_jets(e);

        bool on_run = !e->egress && !dog;
        if (on_run && dist_goal <= s->release_r && e->mun_left > 0) {
            e->release_t -= dt;
            if (e->release_t <= 0) {
                launch_munition(e);
                e->mun_left--;
                e->release_t = s->release_iv;
            }
        }
        if (on_run && e->mun_left == 0) {
            e->egress = true;
            egress_point(e, &e->ex, &e->ey);
        }
        if (e->egress && (dist2d(e->c.x, e->c.y, e->ex, e->ey) < 40
                          || e->c.x < -320 || e->c.y < -320 || e->c.x > WORLD_W + 320 || e->c.y > WORLD_H + 320)) {
            e->c.alive = false;
        }
    }
}

/* ---------- Off-map missile strikes ---------- */

static void spawn_ballistic(int kind, Defense *target, int site, int index, int level) {
    float ax, ay, sx, sy;
    if (target) { ax = target->x; ay = target->y; }
    else site_aim(site, true, false, &ax, &ay);
    if (kind == M_SRBM) {
        /* Iskanders rise visibly from their launch site. */
        sx = eb_specs[EB_ISKANDER].x + (index - 1) * 30.0f;
        sy = eb_specs[EB_ISKANDER].y + index * 40.0f;
    } else {
        sx = ax + frr(-220, 220) * GS;
        sy = WORLD_H + D(650 + index * 170.0f);
    }
    Munition *m = spawn_munition(kind, sx, sy, ax, ay, level, target);
    if (m) m->marker = true;
}

static void spawn_cruise_salvo(int count, int level) {
    bool from_left = chance(0.5f), from_top = chance(0.35f);
    for (int i = 0; i < count; i++) {
        float lag = i * 70.0f, sx, sy;
        if (from_top) { sx = frr(150, WORLD_W - 150); sy = WORLD_H + 40 + lag; }
        else { sx = from_left ? -40 - lag : WORLD_W + 40 + lag; sy = frr(D(300), WORLD_H - 100); }
        int site = random_site();
        Defense *target = random_emplacement(0.4f, false, site);
        float ax, ay;
        if (target) { ax = target->x; ay = target->y; }
        else site_aim(site, true, false, &ax, &ay);
        spawn_munition(M_ALCM, sx, sy, ax, ay, level, target);
    }
}

/* Fires one off-map strike: M_SRBM (Iskander salvo), M_ICBM, or anything else for a Kh-101 salvo. */
static void perform_enemy_strike(int kind, int level) {
    if (kind == M_SRBM) {
        int n = 1 + level / 6;
        if (n > 4) n = 4;
        for (int i = 0; i < n; i++) {
            int site = random_site();
            spawn_ballistic(M_SRBM, random_emplacement(0.4f, false, site), site, i, level);
        }
        glog(LOG_DANGER, "Iskander launch detected: %d ballistic missile%s inbound.", n, n > 1 ? "s" : "");
    } else if (kind == M_ICBM) {
        int site = random_site();
        spawn_ballistic(M_ICBM, NULL, site, 0, level);
        glog(LOG_DANGER, "ICBM warhead inbound on %s! Patriot and THAAD are the best shot at it.", site_specs[site].name);
    } else {
        int n = 2 + level / 4;
        if (n > 6) n = 6;
        spawn_cruise_salvo(n, level);
        glog(LOG_DANGER, "%d Kh-101 cruise missiles inbound from beyond the horizon.", n);
    }
}

static void launch_enemy_strike(int level) {
    G.strike_t = maxf(18, 50 - level * 2.0f) * frr(0.8f, 1.2f) * settings()->spawn_iv;
    int first = settings()->msl_lvl, kinds[3], n = 0;
    float w[3];
    if (level >= first && !eb_down(EB_ISKANDER)) { kinds[n] = M_SRBM; w[n++] = 1.0f; }
    if (level >= first + 1 && !eb_down(EB_AIRBASE)) { kinds[n] = M_ALCM; w[n++] = 1.2f; }
    if (level >= first + 6 && !eb_down(EB_ISKANDER)) { kinds[n] = M_ICBM; w[n++] = 0.35f; }
    int i = wpick(w, n);
    if (i >= 0) perform_enemy_strike(kinds[i], level);
}

/* ---------- Sensors ----------
   Radar: signal-to-noise falls with range^4 and scales with radar cross-section. A jammer adds noise that
   only falls with range^2, so radars burn through as the target closes. Detection per scan is the
   Swerling I formula Pd = Pfa^(1/(1+SNR)). Infrared falls with range^2 and ignores jamming. */

static float detection_probability(int sensor, float ref, float range, float rcs, float ir, float ecm, float eccm) {
    float r = maxf(range, 1), k = ref / r, k2 = k * k, snr;
    if (sensor == SENS_RADAR) snr = 19.0f * rcs * k2 * k2 / (1 + 4.0f * ecm * (1 - eccm) * k2);
    else snr = 19.0f * ir * k2;
    return expf(-13.815511f / (1 + snr)); /* ln(1e-6) */
}

static inline void scan_update(float *q, float pd) {
    if (frand() < pd) *q = minf(1, *q + 0.34f);
    else *q = maxf(0, *q - 0.2f);
}
static inline void scan_lost(float *q) {
    if (*q > 0) *q = maxf(0, *q - 0.3f);
}

static void update_radar(float dt) {
    bool ew = G.upg[U_RADAR] > 0, aw = awacs_active();
    if (ew || aw) {
        G.ew_t -= dt;
        if (G.ew_t <= 0) {
            G.ew_t = 1.0f;
            float ref = ew_range(), eccm = ew_eccm();
            /* Early-warning radar and AWACS share one network track picture. */
            for (int ci = 0; ci < MAX_CON; ci++) {
                const Contact *c = con_at(ci);
                if (!c->alive) { scan_lost(&G.net[ci]); continue; }
                float miss = 1;
                if (ew) miss *= 1 - detection_probability(SENS_RADAR, ref, dist2d(site_specs[0].x, site_specs[0].y, c->x, c->y),
                                                          c->rcs, c->ir, c->ecm, eccm);
                if (aw) miss *= 1 - detection_probability(SENS_RADAR, D(850), dist2d(G.awx, G.awy, c->x, c->y),
                                                          c->rcs, c->ir, c->ecm, 0.7f);
                scan_update(&G.net[ci], 1 - miss);
            }
        }
    } else {
        memset(G.net, 0, sizeof G.net);
    }

    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (!d->active) continue;
        d->scan_t -= dt;
        if (d->scan_t > 0) continue;
        const DefSpec *s = &def_specs[d->spec];
        d->scan_t = s->scan;
        float gain = s->pd ? 4 : 1, floor_rcs = s->pd ? 0.3f : 0, max_r = s->radar_r * 1.8f;
        for (int ci = 0; ci < MAX_CON; ci++) {
            const Contact *c = con_at(ci);
            if (!c->alive || (c->is_mun ? !s->hits_mun : !s->hits_air)) { scan_lost(&d->trk[ci]); continue; }
            float r = dist2d(d->x, d->y, c->x, c->y);
            if (r > max_r) { scan_lost(&d->trk[ci]); continue; }
            float rcs = c->is_mun ? maxf(c->rcs * gain, floor_rcs) : c->rcs;
            scan_update(&d->trk[ci], detection_probability(s->sensor, s->radar_r, r, rcs, c->ir, c->ecm, s->eccm));
        }
    }
}

static void update_awacs(float dt) {
    if (!awacs_active()) return;
    G.awacs_ang += dt * 0.35f;
    G.awx = site_specs[0].x + cosf(G.awacs_ang) * D(110);
    G.awy = site_specs[0].y + D(160) + sinf(G.awacs_ang) * D(55);
}

/* Own track, boosted by the network. A strong network track alone can cue a launch. */
static float track_quality(const Defense *d, int ci) {
    float own = d ? d->trk[ci] : 0, net = G.net[ci];
    if (own > 0) return minf(1, own + net * 0.4f);
    return net * 0.7f;
}

static float kill_probability(const DefSpec *s, const Contact *c, float q, float range) {
    float pk = s->pk;
    pk *= 0.45f + 0.55f * q;                                             /* track accuracy */
    float rf = minf(1, range / s->engage_r);
    pk *= 1 - s->falloff * rf * rf * rf;                                 /* energy lost at max range */
    if (s->sensor == SENS_RADAR) pk *= 1 - 0.4f * c->ecm * (1 - s->eccm); /* fire-control jamming */
    if (c->is_mun && MUN_BALLISTIC(((const Munition *)c)->kind)) pk *= s->ab;
    pk *= 1 - (s->weapon == WPN_GUN ? 0.45f : 0.3f) * c->maneuver;       /* target jinking */
    if (s->weapon == WPN_GUN) pk *= minf(1.15f, D(230) / c->speed);
    if (!c->is_mun && s->pri == PRI_MUNITIONS) pk *= 0.55f;
    return clampf(pk, 0.02f, 0.97f);
}

/* ---------- Engagements ---------- */

/* Flare/chaff burst. Returns true if a cloud is active (newly deployed or still lingering). */
bool try_countermeasures(Enemy *e) {
    if (e->cm_window > 0) return true;
    if (e->cm <= 0 || e->cm_cd > 0) return false;
    e->cm--;
    e->cm_window = 1.2f;
    e->cm_cd = 2.5f;
    fx_cm_burst(e->c.x, e->c.y, e->heading);
    return true;
}

/* Applies one hit to a hostile and credits whoever fired. */
void damage_contact(Contact *c, Defense *by_def, Jet *by_jet) {
    if (!c->alive) return;
    c->hits--;
    if (c->hits > 0) {
        fx_explosion(c->x, c->y, c->alt, 6, RGBF(1, 0.85f, 0.5f, 0.8f));
        return;
    }
    c->alive = false;
    if (by_def) by_def->kills++;
    if (by_jet) by_jet->kills++;
    if (!c->is_mun) {
        Enemy *e = (Enemy *)c;
        fx_explosion(c->x, c->y, c->alt, 22, RGBF(1, 0.6f, 0.25f, 0.9f));
        award_kill(e->bounty, enemy_specs[e->spec].xp, enemy_specs[e->spec].name, by_jet ? by_jet->title : NULL);
    } else {
        fx_explosion(c->x, c->y, c->alt, 9, RGBF(1, 0.9f, 0.6f, 0.85f));
        award_intercept(mun_specs[((Munition *)c)->kind].bounty);
    }
}

static int inflight[MAX_CON];

static int choose_target(const Defense *d, float *out_q) {
    const DefSpec *s = &def_specs[d->spec];
    int best = -1;
    float best_score = -1e30f;
    for (int ci = 0; ci < MAX_CON; ci++) {
        const Contact *c = con_at(ci);
        if (!c->alive || (c->is_mun ? !s->hits_mun : !s->hits_air)) continue;
        float r = dist2d(d->x, d->y, c->x, c->y);
        if (r > s->engage_r || r < s->min_r) continue;
        float q = track_quality(d, ci);
        if (q < FIRE_THRESHOLD) continue;
        if (s->weapon == WPN_MISSILE && inflight[ci] >= c->hits * s->salvo) continue;
        float score;
        if (c->is_mun) {
            const Munition *m = (const Munition *)c;
            if (s->guided_only && !MUN_GUIDED(m->kind)) continue;
            /* Closest to impact and most dangerous first. */
            score = -dist2d(c->x, c->y, m->ax, m->ay) + m->damage * 10;
            /* Ballistic missiles outrank aircraft for dedicated missile defense. */
            if (MUN_BALLISTIC(m->kind) && s->ab >= 1) score += 2600;
            /* Self-defense: shoot down whatever is homing on this emplacement first. */
            if (m->target_def.i == d - G.def && m->target_def.g == d->gen) score += s->pd ? 5000 : 2500;
        } else {
            score = -r;
        }
        score += q * 100;
        if (s->pri == PRI_MUNITIONS && c->is_mun) score += 2000;
        if (s->pri == PRI_AIRCRAFT && !c->is_mun) score += 2000;
        if (score > best_score) { best_score = score; best = ci; *out_q = q; }
    }
    return best;
}

static void fire(Defense *d, Contact *c, float q) {
    const DefSpec *s = &def_specs[d->spec];
    G.pend_shots++;
    d->aim = atan2f(c->y - d->y, c->x - d->x);
    if (s->weapon == WPN_GUN) {
        float pk = kill_probability(s, c, q, dist2d(d->x, d->y, c->x, c->y));
        bool hit = frand() < pk;
        float ex = c->x, ey = c->y;
        if (!hit) { ex += frr(-22, 22); ey += frr(-22, 22); }
        fx_tracer(d->x, d->y, 0, ex, ey, c->alt, s->color);
        if (hit) { G.pend_hits++; damage_contact(c, d, NULL); }
        return;
    }
    for (int i = 0; i < MAX_ICP; i++) {
        Interceptor *m = &G.icp[i];
        if (m->alive) continue;
        memset(m, 0, sizeof *m);
        m->alive = true;
        m->spec = d->spec;
        m->owner = def_ref(d);
        m->target = con_ref(c);
        m->x = d->x; m->y = d->y;
        m->ang = d->aim;
        m->speed = s->pspeed;
        m->launch_r = dist2d(d->x, d->y, c->x, c->y);
        m->launch_q = q;
        break;
    }
    fx_explosion(d->x, d->y, 0, 6, RGBF(1, 1, 1, 0.5f));
}

static void update_defenses(float dt) {
    memset(inflight, 0, sizeof inflight);
    for (int i = 0; i < MAX_ICP; i++) {
        const Interceptor *m = &G.icp[i];
        if (m->alive && !m->decoyed && con_get(m->target)) inflight[m->target.i]++;
    }
    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (!d->active) continue;
        const DefSpec *s = &def_specs[d->spec];
        d->cooldown -= dt;
        if (d->reload_left > 0) {
            d->reload_left -= dt;
            if (d->reload_left <= 0) { d->reload_left = 0; d->ammo = s->mag; }
            continue;
        }
        if (d->cooldown > 0) continue;
        float q = 0;
        int ci = choose_target(d, &q);
        if (ci < 0) continue;
        Contact *c = con_at(ci);
        int hits = c->hits; /* read before the shot: a gun may kill it */
        fire(d, c, q);
        d->cooldown = s->interval;
        if (s->weapon == WPN_MISSILE) {
            inflight[ci]++;
            /* Ripple the rest of a salvo quickly. */
            if (inflight[ci] < hits * s->salvo) d->cooldown = 0.35f;
        }
        d->ammo--;
        if (d->ammo <= 0) d->reload_left = s->reload;
    }
}

static void update_interceptors(float dt) {
    for (int i = 0; i < MAX_ICP; i++) {
        Interceptor *m = &G.icp[i];
        if (!m->alive) continue;
        const DefSpec *s = &def_specs[m->spec];
        m->age += dt;
        float step = m->speed * dt;
        if (m->decoyed) {
            float tx = m->dx - m->x, ty = m->dy - m->y, dist = sqrtf(tx * tx + ty * ty);
            if (dist <= maxf(step, 6) || m->age > 7) {
                m->alive = false;
                fx_explosion(m->dx, m->dy, m->alt, 5, RGBF(0.85f, 0.85f, 0.85f, 0.6f));
            } else {
                m->x += tx / dist * step; m->y += ty / dist * step;
                m->ang = atan2f(ty, tx);
            }
            continue;
        }
        Contact *c = con_get(m->target);
        if (!c || m->age > 7) { m->alive = false; continue; }
        float tx = c->x - m->x, ty = c->y - m->y, dist = sqrtf(tx * tx + ty * ty);
        m->alt = c->alt * clampf(1 - dist / maxf(1, m->launch_r), 0, 1);
        /* Target gets one chance to spoof each missile as it closes in. */
        if (!m->cm_checked && dist < D(140) && !c->is_mun) {
            Enemy *e = (Enemy *)c;
            m->cm_checked = true;
            if (try_countermeasures(e)) {
                float p = s->sensor == SENS_IR ? minf(0.5f, c->flares) : 0.45f * (1 - s->eccm);
                if (frand() < p) {
                    m->decoyed = true;
                    float back = e->heading + PI + frr(-0.6f, 0.6f);
                    m->dx = c->x + cosf(back) * 45;
                    m->dy = c->y + sinf(back) * 45;
                    fx_label("DECOYED", c->x, c->y, c->alt, RGBF(1, 0.8f, 0.35f, 1));
                    continue;
                }
            }
        }
        if (dist <= maxf(step, 8)) {
            m->alive = false;
            Defense *owner = def_get(m->owner);
            float q = maxf(track_quality(owner, c->ci), m->launch_q * 0.8f);
            float pk = kill_probability(s, c, q, m->launch_r);
            if (frand() < pk) { G.pend_hits++; damage_contact(c, owner, NULL); }
            else fx_label("MISS", c->x, c->y, c->alt, RGBF(0.75f, 0.75f, 0.75f, 1));
        } else {
            m->x += tx / dist * step; m->y += ty / dist * step;
            m->ang = atan2f(ty, tx);
            m->trail_t -= dt;
            if (m->trail_t <= 0) { m->trail_t = 0.035f; fx_puff(m->x, m->y, m->alt, RGBF(0.9f, 0.9f, 0.9f, 0.45f)); }
        }
    }
}

/* ---------- Map taps ---------- */

static void rearm_for_order(Enemy *e) {
    /* An aircraft sent on a new attack turns back in and gets at least one weapon to drop. */
    e->egress = false;
    e->dogfight = NOREF;
    if (e->mun_left == 0) {
        int n = enemy_specs[e->spec].mun_count / 2;
        e->mun_left = n > 1 ? n : 1;
    }
}

static Defense *nearest_defense(float x, float y, float *out_d) {
    Defense *best = NULL;
    float bd = 1e9f;
    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (!d->active) continue;
        float r = dist2d(d->x, d->y, x, y);
        if (r < bd) { bd = r; best = d; }
    }
    *out_d = bd;
    return best;
}

/* Creative: tap an enemy to select it, then tap a base or unit to send it there. */
static bool creative_click(float x, float y, float slop) {
    Enemy *best = NULL;
    float bd = 1e9f;
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &G.en[i];
        if (!e->c.alive) continue;
        float r = dist2d(e->c.x, e->c.y, x, y);
        if (r < bd) { bd = r; best = e; }
    }
    if (best && bd <= maxf(24, enemy_specs[best->spec].size * 1.3f) + slop) { select_enemy(best); return true; }
    Enemy *e = en_get(G.sel_enemy);
    if (!e) return false;
    float dd;
    Defense *d = nearest_defense(x, y, &dd);
    if (d && dd <= 20 + slop) {
        e->strike_def = def_ref(d);
        rearm_for_order(e);
        glog(LOG_INFO, "%s ordered to attack the %s.", enemy_specs[e->spec].name, def_specs[d->spec].name);
        return true;
    }
    for (int s = 0; s < N_SITE; s++) {
        if (G.sites[s].built && site_hit(s, x, y)) {
            G.sel_enemy = con_ref(&e->c);
            order_enemy_site(s);
            return true;
        }
    }
    return false;
}

void select_at(float x, float y, float slop) {
    if (G.over) return;
    if (creative() && creative_click(x, y, slop)) return;
    float dd;
    Defense *d = nearest_defense(x, y, &dd);
    if (d && dd <= 20 + slop) { select_def(d); return; }
    for (int s = 0; s < N_SITE; s++)
        if (site_hit(s, x, y)) { select_site(s); return; }
    /* Tapping an enemy installation makes it the strike target. */
    for (int b = 0; b < N_EB; b++)
        if (dist2d(eb_specs[b].x, eb_specs[b].y, x, y) < 70 + slop) { set_strike_target(b); return; }
    deselect_all();
}

/* ---------- Creative mode ---------- */

void creative_spawn(int type, int count, int target_site, int edge, bool at_point, float x, float y) {
    int target = (target_site >= 0 && target_site < N_SITE && G.sites[target_site].built) ? target_site : 0;
    float sx = x, sy = y;
    if (!at_point) spawn_point(edge, &sx, &sy);
    float toward = atan2f(site_specs[target].y - sy, site_specs[target].x - sx);
    float bx = -cosf(toward), by = -sinf(toward), lx = -sinf(toward), ly = cosf(toward);
    int n = count < 1 ? 1 : (count > 12 ? 12 : count);
    for (int i = 0; i < n; i++) {
        float row = (float)(i / 3), col = (float)(i % 3) - 1;
        spawn_enemy(type, sx + bx * row * 40 + lx * col * 36, sy + by * row * 40 + ly * col * 36, target, G.level);
    }
    glog(LOG_INFO, "Spawned %dx %s heading for %s.", n, enemy_specs[type].name, site_specs[target].name);
}
void creative_strike(int kind) { perform_enemy_strike(kind, G.level); }
void creative_raid(void) { spawn_raid(G.level, true); }
void clear_hostiles(void) {
    for (int i = 0; i < MAX_EN; i++) G.en[i].c.alive = false;
    for (int i = 0; i < MAX_MUN; i++) G.mun[i].c.alive = false;
    for (int i = 0; i < MAX_AM; i++)
        if (!G.am[i].friendly) G.am[i].alive = false;
    G.sel_enemy = NOREF;
    glog(LOG_INFO, "All hostiles cleared.");
}
void set_threat_level(int l) {
    G.level = l < 1 ? 1 : (l > 20 ? 20 : l);
    glog(LOG_INFO, "Threat level set to %d. New enemies fly with level %d skill.", G.level, G.level);
}
void order_enemy_site(int site) {
    Enemy *e = en_get(G.sel_enemy);
    if (!e || !G.sites[site].built) return;
    e->target_site = (s16)site;
    e->strike_def = NOREF;
    rearm_for_order(e);
    glog(LOG_INFO, "%s ordered to attack %s.", enemy_specs[e->spec].name, site_specs[site].name);
}
void retreat_enemy(void) {
    Enemy *e = en_get(G.sel_enemy);
    if (!e) return;
    e->egress = true;
    e->dogfight = NOREF;
    egress_point(e, &e->ex, &e->ey);
    glog(LOG_INFO, "%s ordered to retreat.", enemy_specs[e->spec].name);
}
void remove_enemy(void) {
    Enemy *e = en_get(G.sel_enemy);
    if (e) e->c.alive = false;
    G.sel_enemy = NOREF;
}

/* ---------- Game flow ---------- */

void game_start(int diff) {
    catalog_init();
    memset(&G, 0, sizeof G);
    G.diff = diff;
    G.phase = PH_PLAY;
    G.money = settings()->money;
    G.level = 1;
    G.patrol_site = -1;
    G.strike_target = -1;
    G.indestructible = true;
    G.sp_type = 2;
    G.sp_count = 2;
    G.placing = -1;
    G.relocating = NOREF;
    deselect_all();
    for (int s = 0; s < N_SITE; s++) {
        G.sites[s].max_hp = G.sites[s].hp = site_specs[s].hp * settings()->base_hp;
        G.sites[s].born = -10;
    }
    G.sites[0].built = true;
    for (int b = 0; b < N_EB; b++) {
        G.eb[b].health = eb_specs[b].health;
        G.eb[b].sam_cd = frr(1.5f, 3);
    }
    for (int j = 0; j < MAX_JETS; j++) G.jets[j].target = NOREF;
    float pace = settings()->spawn_iv;
    G.spawn_t = 10 * pace;
    G.raid_t = 75 * pace;
    G.strike_t = 40 * pace;
    G.awx = site_specs[0].x + D(110);
    G.awy = site_specs[0].y + D(160);
    Defense *d = add_defense(D_VULCAN, site_specs[0].x, site_specs[0].y + D(140));
    if (d) d->born = -10;
    if (creative()) glog(LOG_INFO, "Creative mode: everything is free. Use MORE > CREATIVE to send in enemy aircraft.");
    else glog(LOG_INFO, "%s difficulty. FOB Anvil online. Hostiles inbound.", settings()->title);
}

void game_restart(void) { game_start(G.diff); }

void game_menu(void) {
    G.paused = false;
    G.phase = PH_MENU;
}

static void repair_everything(float rate) {
    if (rate <= 0) return;
    for (int s = 0; s < N_SITE; s++) {
        Site *st = &G.sites[s];
        if (st->built && st->hp < st->max_hp) st->hp = minf(st->max_hp, st->hp + rate);
    }
    for (int i = 0; i < MAX_DEF; i++) {
        Defense *d = &G.def[i];
        if (d->active && d->hp < def_specs[d->spec].hp) d->hp = minf(def_specs[d->spec].hp, d->hp + rate);
    }
}

/* What the player can see of each hostile: the best track any sensor holds on it. */
static void update_contact_visuals(float dt) {
    float k = minf(1, dt * 6);
    int hostiles = 0;
    for (int ci = 0; ci < MAX_CON; ci++) {
        Contact *c = con_at(ci);
        if (!c->alive) continue;
        if (!c->is_mun) hostiles++;
        float q = G.net[ci];
        for (int i = 0; i < MAX_DEF; i++)
            if (G.def[i].active && G.def[i].trk[ci] > q) q = G.def[i].trk[ci];
        if (q < 0.8f) {
            for (int i = 0; i < MAX_JETS; i++) {
                const Jet *j = &G.jets[i];
                float sr = jet_specs[j->spec].sensor_r;
                if (j->alive && j->state != JS_PARKED && sr > 0 && dist2d(j->x, j->y, c->x, c->y) < sr * 0.8f) { q = 0.8f; break; }
            }
        }
        c->dispq += (q - c->dispq) * k;
        if (!c->is_mun) ((Enemy *)c)->locked = q >= FIRE_THRESHOLD;
    }
    G.hostiles = hostiles;
}

void sim_update(float real_dt) {
    if (G.phase != PH_PLAY) return;
    G.real_time += real_dt;
    if (G.ghost_nudge > 0) G.ghost_nudge -= real_dt;
    if (G.paused || G.over) return;
    float dt = minf(0.05f, real_dt) * TIME_SCALE * (G.fast ? 2 : 1);
    G.time += dt;

    G.income_t += dt;
    if (G.income_t >= 1) {
        G.income_t -= 1;
        if (!creative()) G.money += income_per_second();
        G.shots += G.pend_shots;
        G.shot_hits += G.pend_hits;
        G.pend_shots = G.pend_hits = 0;
        repair_everything(repair_rate());
        update_threatened_site();
    }

    /* In Creative the enemy only comes when you send it, unless automatic waves are on. */
    if (!creative() || G.auto_waves) {
        G.spawn_t -= dt;
        if (G.spawn_t <= 0) spawn_wave(G.level);
        G.raid_t -= dt;
        if (G.raid_t <= 0) spawn_raid(G.level, false);
        G.strike_t -= dt;
        if (G.strike_t <= 0) launch_enemy_strike(G.level);
    }

    update_awacs(dt);
    update_enemies(dt);
    update_munitions(dt);
    update_radar(dt);
    update_defenses(dt);
    update_interceptors(dt);
    update_jets(dt);
    update_air_missiles(dt);
    update_contact_visuals(dt);
    update_enemy_bases(dt);
    update_ordnance(dt);
    fx_update(dt);
}
