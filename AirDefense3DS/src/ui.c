/* The touch screen: tactical map, the weapons shop, bases, the air wing and everything else you buy
   or manage. Immediate-mode: each control is drawn and hit-tested in the same call. */
#include <stdlib.h>
#include "app.h"
#include "sim_int.h"

#define C_BG RGBA(14, 18, 16, 255)
#define C_PANEL RGBA(26, 32, 28, 255)
#define C_PANEL2 RGBA(34, 42, 36, 255)
#define C_EDGE RGBA(74, 88, 72, 255)
#define C_TEXT RGBA(232, 238, 220, 255)
#define C_DIM RGBA(150, 162, 142, 255)
#define C_GOLD RGBA(245, 200, 100, 255)
#define C_GREEN RGBA(120, 220, 130, 255)
#define C_RED RGBA(255, 110, 95, 255)
#define C_BLUE RGBA(140, 200, 255, 255)

#define STATUS_H 18
#define TAB_H 34
#define CY0 STATUS_H
#define CY1 (240 - TAB_H)
#define SEG_H 24

/* ---------- Touch ---------- */

static struct {
    bool down, pressed, released, drag, tap;
    int x, y, sx, sy, dx, dy;
} T;
static bool was_down;
static int last_x, last_y;
static float clip0, clip1 = 240;

static void touch_update(void) {
    bool down = A.pad.touch;
    T.pressed = down && !was_down;
    T.released = !down && was_down;
    T.dx = T.dy = 0;
    if (down) {
        if (T.pressed) { T.sx = A.pad.tx; T.sy = A.pad.ty; T.drag = false; last_x = T.sx; last_y = T.sy; }
        T.x = A.pad.tx; T.y = A.pad.ty;
        T.dx = T.x - last_x; T.dy = T.y - last_y;
        last_x = T.x; last_y = T.y;
        if (abs(T.x - T.sx) > 7 || abs(T.y - T.sy) > 7) T.drag = true;
    }
    T.down = down;
    T.tap = T.released && !T.drag;
    was_down = down;
}
static bool inside(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
static bool area_tap(float x, float y, float w, float h) {
    return T.tap && inside(T.sx, T.sy, x, y, w, h) && inside(T.x, T.y, x, y, w, h) && T.y >= clip0 && T.y < clip1;
}
static bool area_held(float x, float y, float w, float h) {
    return T.down && !T.drag && inside(T.sx, T.sy, x, y, w, h) && T.sy >= clip0 && T.sy < clip1;
}

/* ---------- Widgets ---------- */

enum { B_NORMAL, B_PRIMARY, B_DANGER, B_ON };

static void label_fit(float cx, float y, float h, float w, u32 c, const char *s) {
    int font = gx_textw(F_M, s) <= w - 6 ? F_M : F_S;
    gx_text_c(font, cx, y + (h - gx_lineh(font)) / 2, c, s);
}

static bool button(float x, float y, float w, float h, const char *label, int style, bool enabled) {
    static const u32 fills[4] = {RGBA(44, 54, 46, 255), RGBA(40, 96, 56, 255), RGBA(110, 44, 38, 255), RGBA(52, 84, 110, 255)};
    static const u32 edges[4] = {RGBA(104, 122, 100, 255), RGBA(110, 210, 130, 255), RGBA(240, 120, 100, 255), RGBA(140, 200, 255, 255)};
    bool held = enabled && area_held(x, y, w, h);
    u32 fill = fills[style], edge = edges[style], text = C_TEXT;
    if (!enabled) { fill = RGBA(28, 32, 30, 255); edge = RGBA(56, 62, 56, 255); text = RGBA(104, 112, 100, 255); }
    if (held) fill = gx_mix(fill, RGBA(255, 255, 255, 255), 0.25f);
    gx_rect(x, y, w, h, fill);
    gx_rect(x, y, w, 1, gx_mix(fill, RGBA(255, 255, 255, 255), 0.18f));
    gx_frame(x, y, w, h, 1, edge);
    label_fit(x + w / 2, y + (held ? 1 : 0), h, w, text, label);
    return enabled && area_tap(x, y, w, h);
}

static void text_fit(int font, float x, float y, float w, u32 c, const char *s) {
    char buf[96];
    snprintf(buf, sizeof buf, "%s", s);
    size_t n = strlen(buf);
    if (gx_textw(font, buf) > w) {
        while (n > 2 && gx_textw(font, buf) > w - 6) buf[--n] = 0;
        if (n + 2 < sizeof buf) { buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = 0; }
    }
    gx_text(font, x, y, c, buf);
}

/* Segmented control under the status bar. */
static int segments(const char *const *labels, int n, int cur) {
    float w = 320.0f / n;
    gx_rect(0, CY0, 320, SEG_H, C_BG);
    for (int i = 0; i < n; i++) {
        float x = i * w;
        bool on = i == cur, held = area_held(x, CY0, w, SEG_H);
        gx_rect(x + 1, CY0 + 1, w - 2, SEG_H - 3, on ? RGBA(52, 70, 56, 255) : (held ? C_PANEL2 : C_PANEL));
        if (on) gx_rect(x + 1, CY0 + SEG_H - 3, w - 2, 2, C_GREEN);
        label_fit(x + w / 2, CY0, SEG_H - 2, w, on ? C_TEXT : C_DIM, labels[i]);
        if (area_tap(x, CY0, w, SEG_H)) cur = i;
    }
    gx_rect(0, CY0 + SEG_H - 1, 320, 1, C_EDGE);
    return cur;
}

/* Scrolling lists: drag with the stylus, with a little inertia. */
enum { L_ARMS, L_SITES, L_UPG, L_WING, L_HANGAR, L_JETINFO, L_AIRUPG, L_TARGETS, L_LOG, L_OPTIONS, L_CREATIVE, L_STATS, N_LIST };
static float sc_off[N_LIST], sc_h[N_LIST], sc_v[N_LIST];
static float list_y0, list_y1;

static float list_begin(int id, float y0, float y1) {
    float max = sc_h[id] - (y1 - y0);
    if (max < 0) max = 0;
    if (T.down && T.drag && T.sy >= y0 && T.sy < y1) { sc_off[id] -= T.dy; sc_v[id] = (float)-T.dy; }
    else { sc_off[id] += sc_v[id]; sc_v[id] *= 0.88f; if (fabsf(sc_v[id]) < 0.3f) sc_v[id] = 0; }
    sc_off[id] = clampf(sc_off[id], 0, max);
    list_y0 = y0; list_y1 = y1;
    clip0 = y0; clip1 = y1;
    return y0 - floorf(sc_off[id]);
}
static void list_end(int id, float content_bottom, float content_top) {
    sc_h[id] = content_bottom - content_top;
    float view = list_y1 - list_y0;
    if (sc_h[id] > view + 1) {
        float th = maxf(16, view * view / sc_h[id]);
        float ty = list_y0 + (view - th) * sc_off[id] / (sc_h[id] - view);
        gx_rect(317, list_y0, 3, view, RGBA(30, 36, 32, 255));
        gx_rect(317, ty, 3, th, RGBA(120, 140, 116, 255));
    }
    clip0 = 0; clip1 = 240;
}
static bool row_visible(float y, float h) { return y + h > list_y0 && y < list_y1; }

static void hp_bar(float x, float y, float w, float h, float f, u32 c) {
    gx_rect(x, y, w, h, RGBA(44, 48, 44, 255));
    gx_rect(x, y, w * clampf(f, 0, 1), h, c);
}
static u32 hp_col(float f) { return f > 0.6f ? C_GREEN : (f > 0.3f ? RGBA(255, 190, 80, 255) : C_RED); }
static const char *price(int cost) {
    static char buf[4][20];
    static int k;
    k = (k + 1) & 3;
    if (creative()) snprintf(buf[k], sizeof buf[k], "Free");
    else fmt_money(buf[k], sizeof buf[k], cost);
    return buf[k];
}
static float km(float units) { return units * 25 / GS / 1000; }

/* ---------- UI state ---------- */

static int arms_detail = -1, hangar_detail = -1;
static Ref jet_detail = {-1, 0};
static int confirm_what;
static float confirm_until;

void ui_reset(void) {
    arms_detail = hangar_detail = -1;
    jet_detail = NOREF;
    confirm_what = 0;
    memset(sc_off, 0, sizeof sc_off);
    memset(sc_v, 0, sizeof sc_v);
    A.tab = TAB_MAP;
    memset(A.sub, 0, sizeof A.sub);
    A.map_local = false;
}

/* A button that must be tapped twice. */
static bool confirm_button(float x, float y, float w, float h, const char *label, int id) {
    bool armed = confirm_what == id && A.t < confirm_until;
    if (button(x, y, w, h, armed ? "TAP AGAIN TO CONFIRM" : label, B_DANGER, true)) {
        if (armed) { confirm_what = 0; return true; }
        confirm_what = id;
        confirm_until = A.t + 3;
    }
    return false;
}

static void go_place(void) {
    G.ghost_x = A.cam_x;
    G.ghost_y = A.cam_y;
    /* Start somewhere legal if the camera is looking at enemy land. */
    if (placement_problem(G.ghost_x, G.ghost_y, NULL) && !creative()) {
        G.ghost_x = site_specs[0].x;
        G.ghost_y = site_specs[0].y + D(160);
        cam_focus(G.ghost_x, G.ghost_y);
    }
    A.tab = TAB_MAP;
    A.map_local = true;
}

/* ---------- MAP tab ---------- */

static View map_view(void) {
    View v = {0, CY0, 320, CY1 - CY0, A.cam_x, A.cam_y, 0.30f, 0, true};
    if (!A.map_local) {
        v.s = (CY1 - CY0) / WORLD_H;
        v.cx = WORLD_W / 2;
        v.cy = WORLD_H / 2;
    }
    return v;
}

static float map_panel(void) {
    const float ph = 62, py = CY1 - ph, by = py + 30, bh = 28;
    char t[96];
    Defense *d = def_get(G.sel_def);
    Enemy *e = en_get(G.sel_enemy);
    bool any = is_placing() || G.sp_placing || d || G.sel_site >= 0 || e;
    if (!any) return CY1;
    gx_rect(0, py, 320, ph, RGBA(16, 22, 18, 244));
    gx_rect(0, py, 320, 1, C_EDGE);
    if (G.sp_placing) {
        snprintf(t, sizeof t, "Drop %dx %s", G.sp_count, enemy_specs[G.sp_type].name);
        gx_text(F_M, 6, py + 4, C_TEXT, t);
        gx_text(F_S, 6, py + 18, C_DIM, "Tap the map where the formation should appear.");
        if (button(214, by, 100, bh, "CANCEL", B_NORMAL, true)) G.sp_placing = false;
    } else if (is_placing()) {
        Defense *mv = def_get(G.relocating);
        const DefSpec *s = &def_specs[mv ? mv->spec : G.placing];
        const char *problem = ghost_problem();
        if (mv) snprintf(t, sizeof t, "Moving %s", s->name);
        else snprintf(t, sizeof t, "Placing %s   %s", s->name, price(s->cost));
        gx_text(F_M, 6, py + 4, C_TEXT, t);
        gx_text(F_S, 6, py + 18, problem ? C_RED : C_GREEN, problem ? problem : "Clear to build. Tap or drag to aim, D-Pad to nudge.");
        if (button(6, by, 100, bh, "CANCEL", B_NORMAL, true)) cancel_placement();
        if (button(194, by, 120, bh, mv ? "MOVE HERE" : "CONFIRM", B_PRIMARY, problem == NULL)) confirm_placement();
    } else if (d) {
        const DefSpec *s = &def_specs[d->spec];
        gx_rect(6, py + 6, 3, 20, s->color);
        gx_text(F_M, 13, py + 4, C_TEXT, s->name);
        snprintf(t, sizeof t, "HP %d/%d   Kills %d   %s", (int)ceilf(d->hp), (int)s->hp, d->kills,
                 d->reload_left > 0 ? "Reloading" : (d->fixed ? "Ship-mounted" : "Ready"));
        gx_text(F_S, 13, py + 18, C_DIM, t);
        int rc = repair_cost(d);
        snprintf(t, sizeof t, rc > 0 ? "REPAIR %s" : "REPAIR", price(rc));
        if (!d->fixed && button(6, by, 70, bh, "MOVE", B_NORMAL, true)) { begin_relocate(); A.map_local = true; }
        if (button(80, by, 98, bh, t, B_PRIMARY, rc > 0 && can_afford(rc))) repair_selected();
        if (!d->fixed) {
            snprintf(t, sizeof t, creative() ? "REMOVE" : "SELL %s", price(sell_value(d)));
            if (button(182, by, 94, bh, t, B_DANGER, true)) sell_selected();
        }
        if (button(280, by, 34, bh, "X", B_NORMAL, true)) deselect_all();
    } else if (G.sel_site >= 0) {
        int s = G.sel_site;
        const SiteSpec *sp = &site_specs[s];
        const Site *st = &G.sites[s];
        gx_text(F_M, 6, py + 4, C_TEXT, sp->name);
        if (st->built) {
            snprintf(t, sizeof t, "HP %d/%d", (int)ceilf(st->hp), (int)st->max_hp);
            gx_text(F_S, 6, py + 18, C_DIM, t);
            hp_bar(74, py + 21, 90, 5, st->hp / st->max_hp, hp_col(st->hp / st->max_hp));
            int rc = site_repair_cost(s);
            snprintf(t, sizeof t, rc > 0 ? "REPAIR %s" : "NO DAMAGE", price(rc));
            if (button(6, by, 130, bh, t, B_PRIMARY, rc > 0 && can_afford(rc))) repair_site(s);
        } else {
            text_fit(F_S, 6, py + 18, 308, C_DIM, sp->blurb);
            if (!unlocked(sp->unlock)) {
                snprintf(t, sizeof t, "UNLOCKS AT LV %d", sp->unlock);
                button(6, by, 150, bh, t, B_NORMAL, false);
            } else {
                snprintf(t, sizeof t, "%s %s", st->destroyed ? "REBUILD" : "BUILD", price(site_price(s)));
                if (button(6, by, 150, bh, t, B_PRIMARY, can_afford(site_price(s)))) build_site(s);
            }
        }
        if (button(280, by, 34, bh, "X", B_NORMAL, true)) deselect_all();
    } else if (e) {
        const EnemySpec *s = &enemy_specs[e->spec];
        Defense *sd = def_get(e->strike_def);
        gx_text(F_M, 6, py + 4, C_RED, s->name);
        snprintf(t, sizeof t, "Hits %d  Weapons %d  AAM %d  -> %s", e->c.hits, e->mun_left, e->aa_left,
                 e->egress ? "leaving" : (sd ? def_specs[sd->spec].name : site_specs[e->target_site].name));
        text_fit(F_S, 6, py + 18, 308, C_DIM, t);
        if (button(6, by, 90, bh, "RETREAT", B_NORMAL, true)) retreat_enemy();
        if (button(100, by, 90, bh, "REMOVE", B_DANGER, true)) remove_enemy();
        gx_text(F_S, 196, by + 3, C_DIM, "Tap a base or");
        gx_text(F_S, 196, by + 14, C_DIM, "unit to attack");
        if (button(280, by, 34, bh, "X", B_NORMAL, true)) deselect_all();
    }
    return py;
}

static bool ghost_drag;

static void tab_map(void) {
    View v = map_view();
    world_draw(&v);
    /* what the top screen is looking at */
    float vw = 400 / A.zoom * v.s, vh = 240 / A.zoom * v.s;
    float vx = v.x0 + v.w / 2 + (A.cam_x - v.cx) * v.s - vw / 2, vy = v.y0 + v.h / 2 - (A.cam_y - v.cy) * v.s - vh / 2;
    gx_frame(vx, vy, vw, vh, 1, RGBA(255, 255, 255, A.map_local ? 70 : 170));
    if (!A.map_local) {
        gx_rect(vx, vy, 6, 1, RGBA(255, 255, 255, 255)); gx_rect(vx, vy, 1, 6, RGBA(255, 255, 255, 255));
        gx_rect(vx + vw - 6, vy + vh - 1, 6, 1, RGBA(255, 255, 255, 255)); gx_rect(vx + vw - 1, vy + vh - 6, 1, 6, RGBA(255, 255, 255, 255));
    }
    /* the panel is drawn first so the map cannot steal its taps */
    float panel_y = map_panel();
    const float tx = 264, ty = CY0 + 4, tw = 52, th = 28;
    bool on_toggle = inside(T.sx, T.sy, tx, ty, tw, th);
    if (button(tx, ty, tw, th, A.map_local ? "WIDE" : "ZOOM", B_ON, true)) A.map_local = !A.map_local;
    if (!A.map_local) {
        gx_text(F_S, 4, CY0 + 4, C_DIM, "THEATRE");
        gx_text(F_S, 4, CY0 + 15, C_DIM, "Tap: look");
    }
    bool on_map = T.sy >= CY0 && T.sy < panel_y && !on_toggle;
    if (!on_map) { ghost_drag = false; return; }
    float wx = view_wx(&v, (float)T.x), wy = view_wy(&v, (float)T.y);
    float slop = 7 / v.s;
    if (T.pressed) {
        float gx = v.x0 + v.w / 2 + (G.ghost_x - v.cx) * v.s, gy = v.y0 + v.h / 2 - (G.ghost_y - v.cy) * v.s;
        ghost_drag = is_placing() && fabsf(T.x - gx) < 18 && fabsf(T.y - gy) < 18;
    }
    if (T.down && T.drag) {
        if (ghost_drag) { G.ghost_x = wx; G.ghost_y = wy; }
        else if (A.map_local) { A.cam_x -= T.dx / v.s; A.cam_y += T.dy / v.s; cam_clamp(); }
        else cam_focus(wx, wy);
    }
    if (T.tap && T.y < panel_y) {
        if (G.sp_placing) {
            creative_spawn(G.sp_type, G.sp_count, G.sp_target, G.sp_edge, true, wx, wy);
            G.sp_placing = false;
        } else if (is_placing()) {
            G.ghost_x = wx; G.ghost_y = wy;
        } else {
            int before = G.strike_target;
            select_at(wx, wy, slop);
            bool got = def_get(G.sel_def) || G.sel_site >= 0 || en_get(G.sel_enemy) || G.strike_target != before;
            if (!A.map_local && !got) cam_focus(wx, wy);
        }
    }
    if (T.released) ghost_drag = false;
}

/* ---------- ARMS tab ---------- */

static const char *lock_reason_def(const DefSpec *s) {
    static char buf[32];
    if (!unlocked(s->unlock)) { snprintf(buf, sizeof buf, "Unlocks at Lv %d", s->unlock); return buf; }
    if (emplacement_cap() - placed_count() <= 0) return "All emplacements used";
    if (!can_afford(s->cost)) { snprintf(buf, sizeof buf, "Need %s", price(s->cost)); return buf; }
    return NULL;
}

static void stat_row(float x, float y, float w, const char *k, const char *v, u32 vc) {
    gx_text(F_S, x, y, C_DIM, k);
    gx_text_r(F_S, x + w, y, vc, v);
}

static void arms_detail_page(int spec) {
    const DefSpec *s = &def_specs[spec];
    char t[64];
    float y = CY0 + 5;
    gx_rect(6, y, 4, 15, s->color);
    gx_text(F_M, 15, y, C_TEXT, s->name);
    gx_text_r(F_M, 314, y, C_GOLD, price(s->cost));
    y += 17;
    y += gx_wrap(F_S, 6, y, 308, C_DIM, s->role, 3) * 12 + 3;
    gx_rect(6, y, 308, 1, C_EDGE);
    y += 4;
    const float c1 = 6, c2 = 166, cw = 148;
    snprintf(t, sizeof t, "%.1f km", km(s->engage_r)); stat_row(c1, y, cw, "Weapon range", t, C_TEXT);
    snprintf(t, sizeof t, "%.1f km", km(s->radar_r)); stat_row(c2, y, cw, s->sensor == SENS_RADAR ? "Radar range" : "IR seeker range", t, C_TEXT);
    y += 12;
    snprintf(t, sizeof t, "%d%%", (int)(s->pk * 100 + 0.5f)); stat_row(c1, y, cw, s->weapon == WPN_GUN ? "Hit chance / burst" : "Hit chance / missile", t, C_TEXT);
    snprintf(t, sizeof t, "%d%%", (int)(s->eccm * 100 + 0.5f)); stat_row(c2, y, cw, "Jam resistance", s->sensor == SENS_IR ? "Immune" : t, C_TEXT);
    y += 12;
    snprintf(t, sizeof t, "%d, reload %ds", s->mag, (int)s->reload); stat_row(c1, y, cw, "Magazine", t, C_TEXT);
    snprintf(t, sizeof t, "%d HP%s", (int)s->hp, s->pd ? ", hardened" : ""); stat_row(c2, y, cw, "Emplacement", t, C_TEXT);
    y += 12;
    stat_row(c1, y, cw, "Targets", s->hits_air && s->hits_mun ? (s->pri == PRI_MUNITIONS ? "Munitions first" : "Aircraft + munitions") : (s->hits_air ? "Aircraft only" : "Munitions only"), C_TEXT);
    snprintf(t, sizeof t, "x%.2f", s->ab); stat_row(c2, y, cw, "Vs ballistic", s->hits_mun ? t : "-", s->ab >= 1 ? C_GREEN : C_TEXT);
    y += 12;
    if (s->salvo > 1) { stat_row(c1, y, cw, "Doctrine", "2-missile salvos", C_BLUE); }
    if (s->guided_only) stat_row(c2, y, cw, "Ignores", "Rockets, bombs", C_TEXT);
    if (s->pd) { stat_row(c1, y, cw, "Point defense", "Decoys 55% of ARMs", C_BLUE); stat_row(c2, y, cw, "Damage taken", "Half", C_BLUE); }
    y += 14;
    snprintf(t, sizeof t, "%.1f shots/s sustained", s->mag / (s->mag * s->interval + s->reload)); stat_row(c1, y, cw, "Rate of fire", t, C_TEXT);
    snprintf(t, sizeof t, "every %.2fs", s->scan); stat_row(c2, y, cw, "Radar scan", t, C_TEXT);
    y += 16;
    /* how far it reaches compared with the longest-ranged system */
    gx_text(F_S, c1, y, C_DIM, "Reach");
    hp_bar(50, y + 3, 264, 5, s->engage_r / def_specs[D_THAAD].engage_r, s->color);
    int owned = 0;
    for (int i = 0; i < MAX_DEF; i++) owned += G.def[i].active && G.def[i].spec == spec;
    const char *why = lock_reason_def(s);
    snprintf(t, sizeof t, "Owned: %d    Emplacements %d/%d", owned, placed_count(), emplacement_cap());
    gx_text(F_S, 6, CY1 - 48, why ? C_RED : C_DIM, why ? why : t);
    if (button(6, CY1 - 34, 96, 30, "BACK", B_NORMAL, true)) arms_detail = -1;
    snprintf(t, sizeof t, "PLACE  %s", price(s->cost));
    if (button(108, CY1 - 34, 206, 30, t, B_PRIMARY, why == NULL)) {
        if (begin_placement(spec)) { arms_detail = -1; go_place(); }
    }
}

static void tab_arms(void) {
    if (arms_detail >= 0) { arms_detail_page(arms_detail); return; }
    const float top = CY0 + 16, ch = 52;
    float y0 = list_begin(L_ARMS, top, CY1), y = y0;
    char t[48];
    for (int i = 0; i < N_SHOP_DEF; i++) {
        const DefSpec *s = &def_specs[i];
        float x = 3 + (i % 2) * 158, cy = y + (i / 2) * (ch + 3);
        if (!row_visible(cy, ch)) continue;
        bool open = unlocked(s->unlock), afford = can_afford(s->cost);
        bool held = area_held(x, cy, 155, ch);
        gx_panel(x, cy, 155, ch, held ? C_PANEL2 : C_PANEL, open ? C_EDGE : RGBA(48, 54, 48, 255));
        gx_rect(x + 1, cy + 1, 3, ch - 2, open ? s->color : RGBA(70, 76, 70, 255));
        text_fit(F_M, x + 8, cy + 3, 144, open ? C_TEXT : C_DIM, s->name);
        if (open) {
            gx_text(F_M, x + 8, cy + 18, afford ? C_GOLD : C_RED, price(s->cost));
            int owned = 0;
            for (int k = 0; k < MAX_DEF; k++) owned += G.def[k].active && G.def[k].spec == i;
            if (owned) { snprintf(t, sizeof t, "x%d", owned); gx_text_r(F_S, x + 150, cy + 20, C_BLUE, t); }
        } else {
            snprintf(t, sizeof t, "Unlocks at Lv %d", s->unlock);
            gx_text(F_S, x + 8, cy + 20, C_DIM, t);
        }
        snprintf(t, sizeof t, "%s  %.1f km  %s", s->weapon == WPN_GUN ? "Gun" : "SAM", km(s->engage_r),
                 s->hits_air && s->hits_mun ? "air+mun" : (s->hits_air ? "air" : "munitions"));
        gx_text(F_S, x + 8, cy + 36, C_DIM, t);
        if (area_tap(x, cy, 155, ch)) arms_detail = i;
    }
    list_end(L_ARMS, y + 4 * (ch + 3), y0);
    gx_rect(0, CY0, 320, 16, C_BG);
    snprintf(t, sizeof t, "Emplacements %d/%d", placed_count(), emplacement_cap());
    gx_text(F_S, 5, CY0 + 3, C_TEXT, t);
    gx_text_r(F_S, 315, CY0 + 3, C_DIM, "Tap a system for details");
}

/* ---------- BASES tab ---------- */

static void upgrade_rows(int list, int first, int count, bool need_airfield) {
    float y0 = list_begin(list, CY0 + SEG_H, CY1), y = y0;
    char t[128];
    if (need_airfield && !has_airfield()) {
        gx_wrap(F_S, 8, y + 8, 300, C_DIM, "Build an airfield or the carrier first (BASES tab). Air upgrades apply to every jet you own.", 3);
        y += 44;
    }
    for (int u = first; u < first + count; u++) {
        const float h = 56;
        if (row_visible(y, h)) {
            const UpgSpec *s = &upg_specs[u];
            int cost = upgrade_cost(u), lvl = G.upg[u];
            bool open = unlocked(s->unlock);
            gx_panel(3, y, 311, h - 3, C_PANEL, C_EDGE);
            gx_text(F_M, 9, y + 3, open ? C_TEXT : C_DIM, s->title);
            for (int k = 0; k < s->max; k++)
                gx_rect(10.0f + k * 9, y + 19, 7, 4, k < lvl ? C_GREEN : RGBA(54, 62, 54, 255));
            upgrade_detail(u, t, sizeof t);
            gx_wrap(F_S, 9, y + 25, 214, C_DIM, t, 2);
            bool can = open && cost >= 0 && can_afford(cost) && (!need_airfield || has_airfield());
            if (cost < 0) snprintf(t, sizeof t, "MAX");
            else if (!open) snprintf(t, sizeof t, "LV %d", s->unlock);
            else snprintf(t, sizeof t, "%s", price(cost));
            if (button(230, y + 6, 78, 34, t, B_PRIMARY, can)) buy_upgrade(u);
        }
        y += h;
    }
    list_end(list, y, y0);
}

static void tab_bases(void) {
    static const char *const subs[] = {"SITES", "UPGRADES"};
    char t[96];
    if (A.sub[TAB_BASES] == 1) upgrade_rows(L_UPG, 0, N_BASE_UPG, false);
    else {
        float y0 = list_begin(L_SITES, CY0 + SEG_H, CY1), y = y0;
        int total = total_repair_cost();
        if (total > 0) {
            snprintf(t, sizeof t, "REPAIR ALL BASES  %s", price(total));
            if (button(3, y + 2, 311, 30, t, B_PRIMARY, can_afford(total))) repair_all_sites();
            y += 36;
        }
        for (int s = 0; s < N_SITE; s++) {
            const float h = 50;
            const SiteSpec *sp = &site_specs[s];
            const Site *st = &G.sites[s];
            if (row_visible(y, h)) {
                static const char *const kinds[4] = {"HQ", "FOB", "AIRFIELD", "CARRIER"};
                bool held = area_held(3, y, 222, h - 3);
                gx_panel(3, y, 311, h - 3, held ? C_PANEL2 : C_PANEL, st->built ? RGBA(96, 130, 96, 255) : C_EDGE);
                int w = gx_text(F_M, 9, y + 3, st->built || unlocked(sp->unlock) ? C_TEXT : C_DIM, sp->name);
                gx_text(F_S, 15.0f + w, y + 5, C_BLUE, kinds[sp->kind]);
                if (st->built) {
                    float f = st->hp / st->max_hp;
                    hp_bar(9, y + 20, 120, 5, f, hp_col(f));
                    snprintf(t, sizeof t, "%d/%d", (int)ceilf(st->hp), (int)st->max_hp);
                    gx_text(F_S, 134, y + 16, C_DIM, t);
                    if (sp->income) snprintf(t, sizeof t, "+$%d/s income, +4 emplacements", sp->income);
                    else if (sp->hangars) snprintf(t, sizeof t, "Hangars %d/%d in use", jets_at(s), sp->hangars);
                    else snprintf(t, sizeof t, "Headquarters. Lose it and the war is over.");
                    gx_text(F_S, 9, y + 31, C_DIM, t);
                    int rc = site_repair_cost(s);
                    snprintf(t, sizeof t, rc > 0 ? "FIX %s" : "OK", price(rc));
                    if (button(230, y + 6, 78, 34, t, B_PRIMARY, rc > 0 && can_afford(rc))) repair_site(s);
                } else {
                    if (st->destroyed) gx_text(F_S, 9, y + 17, C_RED, "DESTROYED");
                    gx_wrap(F_S, st->destroyed ? 70.0f : 9.0f, y + 17, st->destroyed ? 154.0f : 216.0f, C_DIM, sp->blurb, st->destroyed ? 1 : 2);
                    if (!unlocked(sp->unlock)) {
                        snprintf(t, sizeof t, "LV %d", sp->unlock);
                        button(230, y + 6, 78, 34, t, B_NORMAL, false);
                    } else {
                        snprintf(t, sizeof t, "%s", price(site_price(s)));
                        if (button(230, y + 6, 78, 34, t, B_PRIMARY, can_afford(site_price(s)))) build_site(s);
                    }
                }
                if (area_tap(3, y, 222, h - 3)) cam_focus(sp->x, sp->y);
            }
            y += h;
        }
        gx_text(F_S, 8, y + 2, C_DIM, "Tap a row to look at the site on the top screen.");
        y += 16;
        list_end(L_SITES, y, y0);
    }
    A.sub[TAB_BASES] = segments(subs, 2, A.sub[TAB_BASES]);
}

/* ---------- AIR tab ---------- */

static const char *const phase_label[6] = {"READY", "REARMING", "STANDBY", "STRIKE", "PATROL", "RTB"};
static const u32 phase_col[6] = {RGBA(120, 220, 130, 255), RGBA(255, 190, 80, 255), RGBA(150, 162, 142, 255),
                                 RGBA(245, 180, 70, 255), RGBA(120, 190, 255, 255), RGBA(190, 160, 255, 255)};

static void jet_detail_page(Jet *j) {
    const JetSpec *s = &jet_specs[j->spec];
    char t[96];
    int ph = jet_phase(j);
    float y = CY0 + 5;
    gx_text(F_M, 6, y, C_TEXT, j->title);
    gx_text(F_S, 12.0f + gx_textw(F_M, j->title), y + 2, C_DIM, s->name);
    gx_text_r(F_M, 314, y, phase_col[ph], phase_label[ph]);
    y += 16;
    jet_status(j, t, sizeof t);
    gx_text(F_S, 6, y, C_TEXT, t);
    y += 12;
    snprintf(t, sizeof t, "Home: %s    Kills %d    Sorties %d", site_specs[j->home].name, j->kills, j->sorties);
    gx_text(F_S, 6, y, C_DIM, t);
    y += 12;
    snprintf(t, sizeof t, "Airframe %d/%d", j->hits, s->hits);
    gx_text(F_S, 6, y, j->hits < s->hits ? C_RED : C_DIM, t);
    if (s->missiles) { snprintf(t, sizeof t, "%s %d/%d", s->msl_name, j->missiles, s->missiles); gx_text(F_S, 100, y, C_DIM, t); }
    if (j->state == JS_PATROL && s->endurance > 0) {
        gx_text(F_S, 190, y, C_DIM, "Fuel");
        hp_bar(216, y + 3, 96, 5, j->fuel / jet_endurance(s), C_BLUE);
    }
    y += 16;
    gx_text(F_S, 6, y, C_DIM, "MISSION");
    y += 12;
    static const char *const mis[3] = {"AUTO", "PATROL", "STRIKE"};
    for (int m = 0; m < 3; m++) {
        bool ok = m == MIS_AUTO || (m == MIS_PATROL && JET_CAN_PATROL(s)) || (m == MIS_STRIKE && JET_CAN_STRIKE(s));
        if (button(6.0f + m * 104, y, 100, 30, mis[m], j->mission == m ? B_ON : B_NORMAL, ok)) set_mission(j, m);
    }
    y += 36;
    if (button(6, y, 100, 30, "LOCATE", B_NORMAL, true)) { cam_focus(j->x, j->y); j->flash = G.real_time + 1.8f; }
    int opt = next_rebase_option(j);
    if (opt >= 0) snprintf(t, sizeof t, "TO %s", site_specs[opt].name);
    else snprintf(t, sizeof t, "NO OTHER BASE");
    if (button(110, y, 204, 30, t, B_NORMAL, opt >= 0)) rebase_jet(j, opt);
    if (button(6, CY1 - 34, 100, 30, "BACK", B_NORMAL, true)) jet_detail = NOREF;
    snprintf(t, sizeof t, creative() ? "REMOVE" : "SELL %s", price(s->cost / 2));
    if (button(190, CY1 - 34, 124, 30, t, B_DANGER, j->state == JS_PARKED)) { sell_jet(j); jet_detail = NOREF; }
}

static void wing_page(void) {
    char t[96];
    float y0 = list_begin(L_WING, CY0 + SEG_H, CY1), y = y0;
    int total = hangars_used(), airborne = 0;
    for (int i = 0; i < MAX_JETS; i++) airborne += G.jets[i].alive && G.jets[i].state != JS_PARKED;
    if (!has_airfield()) {
        gx_wrap(F_M, 10, y + 14, 300, C_TEXT, "No airfield yet.", 1);
        gx_wrap(F_S, 10, y + 32, 300, C_DIM, "Build Hollis Air Base from the BASES tab (threat level 3) to unlock fighters, strike aircraft and retaliation raids on the enemy.", 4);
        list_end(L_WING, y + 90, y0);
        return;
    }
    snprintf(t, sizeof t, "Airborne %d/%d   Lost %d   Hangars %d/%d", airborne, total, G.aircraft_lost, total, hangars_total());
    gx_text(F_S, 6, y + 4, C_TEXT, t);
    y += 18;
    snprintf(t, sizeof t, "PATROL STATION: %s", G.patrol_site < 0 ? "AUTO (threatened base)" : site_specs[G.patrol_site].name);
    if (button(3, y, 311, 28, t, B_NORMAL, true)) {
        int s = G.patrol_site;
        do { s++; } while (s < N_SITE && !G.sites[s].built);
        G.patrol_site = s >= N_SITE ? -1 : s;
    }
    y += 32;
    for (int i = 0; i < MAX_JETS; i++) {
        Jet *j = &G.jets[i];
        if (!j->alive) continue;
        const float h = 44;
        if (row_visible(y, h)) {
            const JetSpec *s = &jet_specs[j->spec];
            int ph = jet_phase(j);
            bool held = area_held(3, y, 311, h - 3);
            gx_panel(3, y, 311, h - 3, held ? C_PANEL2 : C_PANEL, C_EDGE);
            gx_rect(4, y + 1, 4, h - 5, phase_col[ph]);
            int w = gx_text(F_M, 13, y + 3, C_TEXT, j->title);
            gx_rect(19.0f + w, y + 4, gx_textw(F_S, phase_label[ph]) + 8.0f, 12, gx_alpha(phase_col[ph], 0.25f));
            gx_text(F_S, 23.0f + w, y + 5, phase_col[ph], phase_label[ph]);
            jet_status(j, t, sizeof t);
            text_fit(F_S, 13, y + 20, 200, C_DIM, t);
            for (int k = 0; k < s->hits; k++) gx_rect(222.0f + k * 9, y + 6, 7, 7, k < j->hits ? C_GREEN : RGBA(90, 40, 36, 255));
            if (s->missiles) { snprintf(t, sizeof t, "%s %d/%d", s->msl_name, j->missiles, s->missiles); gx_text_r(F_S, 308, y + 5, C_DIM, t); }
            if (j->state == JS_PATROL && s->endurance > 0) hp_bar(222, y + 25, 86, 4, j->fuel / jet_endurance(s), C_BLUE);
            else if (j->state == JS_PARKED && j->rearm_left > 0) hp_bar(222, y + 25, 86, 4, 1 - j->rearm_left / (s->rearm * 1.5f), RGBA(255, 190, 80, 255));
            if (area_tap(3, y, 311, h - 3)) jet_detail = jet_ref(j);
        }
        y += h;
    }
    if (total == 0) { gx_text(F_S, 8, y + 6, C_DIM, "No aircraft yet. Buy some from the HANGAR."); y += 24; }
    if (G.n_lost) {
        gx_text(F_S, 6, y + 4, C_RED, "RECENT LOSSES");
        y += 17;
        for (int i = 0; i < G.n_lost; i++) {
            snprintf(t, sizeof t, "%s  %s", G.lost[i].title, G.lost[i].reason);
            text_fit(F_S, 8, y, 304, C_DIM, t);
            y += 12;
        }
        y += 4;
    }
    list_end(L_WING, y, y0);
}

static const char *lock_reason_jet(const JetSpec *s) {
    static char buf[40];
    if (!unlocked(s->unlock)) { snprintf(buf, sizeof buf, "Unlocks at Lv %d", s->unlock); return buf; }
    if (!has_airfield()) return "Needs an airfield";
    if (airbase_with_space(s) < 0) return s->carrier ? "No free hangar" : "No free hangar on land";
    if (!can_afford(s->cost)) { snprintf(buf, sizeof buf, "Need %s", price(s->cost)); return buf; }
    return NULL;
}

static void hangar_detail_page(int k) {
    const JetSpec *s = &jet_specs[k];
    char t[64];
    float y0 = list_begin(L_JETINFO, CY0, CY1 - 38), y = y0 + 4;
    gx_photo(k, 6, y, 128, 72, 1);
    gx_frame(5, y - 1, 130, 74, 1, C_EDGE);
    gx_text(F_M, 142, y, C_TEXT, s->name);
    gx_text(F_S, 142, y + 14, C_BLUE, s->role);
    gx_text(F_M, 142, y + 26, C_GOLD, price(s->cost));
    snprintf(t, sizeof t, "Speed %d kt  Hits %d", (int)(s->speed / GS * 2.2f), s->hits);
    gx_text(F_S, 142, y + 42, C_DIM, t);
    if (s->missiles) snprintf(t, sizeof t, "%dx %s, gun", s->missiles, s->msl_name);
    else snprintf(t, sizeof t, s->gun_pk > 0 ? "Cannon only" : "No air-to-air");
    gx_text(F_S, 142, y + 53, C_DIM, t);
    if (JET_CAN_STRIKE(s)) snprintf(t, sizeof t, "Strike %.1f, range %d%s", s->strike, s->radius, s->antiship ? ", Harpoon" : "");
    else snprintf(t, sizeof t, "No strike role");
    gx_text(F_S, 142, y + 64, C_DIM, t);
    y += 80;
    snprintf(t, sizeof t, "Patrol %ds   Rearm %ds   Flares %d   %s", (int)s->endurance, (int)s->rearm, s->cm, s->carrier ? "Carrier OK" : "Land only");
    gx_text(F_S, 6, y, C_DIM, t);
    y += 15;
    gx_text(F_S, 6, y, C_GREEN, "+");
    y += gx_wrap(F_S, 16, y, 296, C_TEXT, s->strengths, 5) * 12 + 3;
    gx_text(F_S, 6, y, C_RED, "-");
    y += gx_wrap(F_S, 16, y, 296, C_TEXT, s->weaknesses, 5) * 12 + 6;
    list_end(L_JETINFO, y, y0);
    gx_rect(0, CY1 - 38, 320, 38, C_BG);
    gx_rect(0, CY1 - 38, 320, 1, C_EDGE);
    const char *why = lock_reason_jet(s);
    if (button(6, CY1 - 34, 80, 30, "BACK", B_NORMAL, true)) hangar_detail = -1;
    if (why) snprintf(t, sizeof t, "%s", why);
    else snprintf(t, sizeof t, "BUY  %s", price(s->cost));
    if (button(92, CY1 - 34, 222, 30, t, B_PRIMARY, why == NULL)) buy_jet(k);
}

static void hangar_page(void) {
    char t[48];
    const float ch = 52;
    float y0 = list_begin(L_HANGAR, CY0 + SEG_H, CY1), y = y0 + 3;
    for (int i = 0; i < N_JET; i++) {
        const JetSpec *s = &jet_specs[i];
        float x = 3 + (i % 2) * 158, cy = y + (i / 2) * (ch + 3);
        if (!row_visible(cy, ch)) continue;
        bool open = unlocked(s->unlock), held = area_held(x, cy, 155, ch);
        gx_panel(x, cy, 155, ch, held ? C_PANEL2 : C_PANEL, open ? C_EDGE : RGBA(48, 54, 48, 255));
        gx_photo(i, x + 2, cy + 2, 64, 36, open ? 1 : 0.35f);
        text_fit(F_M, x + 70, cy + 2, 83, open ? C_TEXT : C_DIM, s->tag);
        if (open) gx_text(F_M, x + 70, cy + 16, can_afford(s->cost) ? C_GOLD : C_RED, price(s->cost));
        else { snprintf(t, sizeof t, "Lv %d", s->unlock); gx_text(F_S, x + 70, cy + 18, C_DIM, t); }
        int owned = 0;
        for (int k = 0; k < MAX_JETS; k++) owned += G.jets[k].alive && G.jets[k].spec == i;
        if (owned) { snprintf(t, sizeof t, "x%d", owned); gx_text_r(F_S, x + 151, cy + 29, C_BLUE, t); }
        gx_text(F_S, x + 4, cy + 39, C_BLUE, s->role);
        if (area_tap(x, cy, 155, ch)) { hangar_detail = i; sc_off[L_JETINFO] = 0; }
    }
    float cy = y + 2 * (ch + 3), x = 161;
    snprintf(t, sizeof t, "Hangars %d/%d", hangars_used(), hangars_total());
    gx_text(F_S, x + 6, cy + 6, C_TEXT, t);
    gx_wrap(F_S, x + 6, cy + 20, 148, C_DIM, has_airfield() ? "Tap a jet for its photo, strengths and weaknesses." : "Build an airfield first.", 3);
    list_end(L_HANGAR, y + 3 * (ch + 3), y0);
}

static void targets_page(void) {
    char t[96];
    float y0 = list_begin(L_TARGETS, CY0 + SEG_H, CY1), y = y0 + 2;
    if (button(3, y, 311, 28, G.strike_target < 0 ? "AUTO TARGETING: ON" : "SWITCH TO AUTO TARGETING", G.strike_target < 0 ? B_ON : B_NORMAL, true))
        set_strike_target(-1);
    y += 32;
    for (int b = 0; b < N_EB; b++) {
        const float h = 48;
        const EBSpec *s = &eb_specs[b];
        if (row_visible(y, h)) {
            bool open = unlocked(s->unlock), down = eb_down(b), chosen = G.strike_target == b;
            bool held = open && area_held(3, y, 250, h - 3);
            gx_panel(3, y, 311, h - 3, held ? C_PANEL2 : C_PANEL, chosen ? C_GOLD : C_EDGE);
            gx_text(F_M, 9, y + 3, open ? C_TEXT : C_DIM, s->name);
            if (!open) { snprintf(t, sizeof t, "Intel at threat level %d", s->unlock); gx_text(F_S, 9, y + 18, C_DIM, t); }
            else {
                hp_bar(9, y + 20, 80, 5, G.eb[b].health / s->health, C_RED);
                if (down) snprintf(t, sizeof t, "DESTROYED, back in %ds", (int)ceilf(G.eb[b].down_left));
                else snprintf(t, sizeof t, "SAM %d%%  Range %d%s", (int)(effective_defense(b) * 100 + 0.5f), s->depth, s->antiship ? "  Anti-ship only" : "");
                gx_text(F_S, 94, y + 16, down ? C_GREEN : C_DIM, t);
                int n = eb_assigned(b);
                if (n) { snprintf(t, sizeof t, "%d inbound", n); gx_text_r(F_S, 250, y + 4, C_GOLD, t); }
            }
            text_fit(F_S, 9, y + 31, 240, open ? C_BLUE : C_DIM, s->effect);
            if (button(258, y + 5, 52, 34, "VIEW", B_NORMAL, true)) cam_focus(s->x, s->y);
            if (open && area_tap(3, y, 250, h - 3)) set_strike_target(chosen ? -1 : b);
        }
        y += h;
    }
    gx_text(F_S, 8, y + 2, C_DIM, "Tap a target to order every strike jet onto it.");
    y += 16;
    list_end(L_TARGETS, y, y0);
}

static void tab_air(void) {
    static const char *const subs[] = {"WING", "HANGAR", "UPGRADES", "TARGETS"};
    Jet *jd = jet_get(jet_detail);
    if (A.sub[TAB_AIR] == 0 && jd) { jet_detail_page(jd); return; }
    if (A.sub[TAB_AIR] == 1 && hangar_detail >= 0) { hangar_detail_page(hangar_detail); return; }
    switch (A.sub[TAB_AIR]) {
    case 0: wing_page(); break;
    case 1: hangar_page(); break;
    case 2: upgrade_rows(L_AIRUPG, N_BASE_UPG, N_UPG - N_BASE_UPG, true); break;
    default: targets_page();
    }
    A.sub[TAB_AIR] = segments(subs, 4, A.sub[TAB_AIR]);
}

/* ---------- MORE tab ---------- */

static void log_page(void) {
    static const u32 kc[3] = {RGBA(150, 235, 150, 255), RGBA(255, 130, 110, 255), RGBA(205, 212, 195, 255)};
    float y0 = list_begin(L_LOG, CY0 + SEG_H, CY1), y = y0 + 3;
    for (int i = 0; i < G.log_n; i++) {
        const LogEntry *e = log_at(i);
        int lines = gx_wrap(F_S, 12, y, 300, 0, e->text, 4);
        if (row_visible(y, lines * 12.0f)) {
            gx_rect(5, y + 2, 3, lines * 12.0f - 4, kc[e->kind]);
            gx_wrap(F_S, 12, y, 300, kc[e->kind], e->text, 4);
        }
        y += lines * 12 + 3;
    }
    list_end(L_LOG, y, y0);
}

static void stats_page(void) {
    char t[64];
    float y0 = list_begin(L_STATS, CY0 + SEG_H, CY1), y = y0 + 6;
    int nd = placed_count();
#define ROW(k, ...) do { snprintf(t, sizeof t, __VA_ARGS__); stat_row(10, y, 300, k, t, C_TEXT); y += 14; } while (0)
    ROW("Difficulty", "%s", settings()->title);
    ROW("Threat level", "%d", G.level);
    if (!creative()) ROW("XP to next level", "%d / %d", G.xp, xp_to_next());
    ROW("Aircraft shot down", "%d", G.kills);
    ROW("Munitions intercepted", "%d", G.intercepts);
    ROW("Shots fired", "%d", G.shots);
    ROW("Hit rate", "%d%%", G.shots ? (int)(100.0f * G.shot_hits / G.shots + 0.5f) : 0);
    ROW("Your aircraft lost", "%d", G.aircraft_lost);
    ROW("Emplacements", "%d / %d", nd, emplacement_cap());
    ROW("Hangars", "%d / %d", hangars_used(), hangars_total());
    ROW("Income", "$%d/s", income_per_second());
    ROW("Armor", "-%d%% damage", (int)(armor_reduction() * 100 + 0.5f));
    ROW("Time in battle", "%d:%02d", (int)G.time / 60, (int)G.time % 60);
#undef ROW
    list_end(L_STATS, y, y0);
}

static void options_page(void) {
    static const char *const depth[3] = {"3D DEPTH: SUBTLE", "3D DEPTH: NORMAL", "3D DEPTH: DEEP"};
    float y0 = list_begin(L_OPTIONS, CY0 + SEG_H, CY1), y = y0 + 4;
    if (button(3, y, 154, 32, G.paused ? "RESUME" : "PAUSE", G.paused ? B_ON : B_NORMAL, true)) G.paused = !G.paused;
    if (button(161, y, 154, 32, G.fast ? "SPEED: 2x" : "SPEED: 1x", G.fast ? B_ON : B_NORMAL, true)) G.fast = !G.fast;
    y += 36;
    if (button(3, y, 154, 32, A.rings ? "RINGS: ALL UNITS" : "RINGS: SELECTED", B_NORMAL, true)) A.rings = !A.rings;
    if (button(161, y, 154, 32, depth[A.depth], B_NORMAL, true)) A.depth = (A.depth + 1) % 3;
    y += 36;
    if (confirm_button(3, y, 154, 32, "RESTART", 1)) { game_restart(); ui_reset(); A.tab = TAB_MORE; }
    if (confirm_button(161, y, 154, 32, "QUIT TO MENU", 2)) { game_menu(); ui_reset(); }
    y += 40;
    gx_text(F_S, 6, y, C_BLUE, "CONTROLS");
    y += 13;
    static const char *const help[] = {
        "Circle Pad: move the camera     C-Stick: move fast",
        "L / R: zoom out / in            ZL / ZR: change tab",
        "X: 2x speed    Y: back to HQ    START: pause",
        "SELECT: switch the lower map between theatre and zoom",
        "A: confirm placement            B: cancel / deselect",
        "D-Pad: nudge a unit being placed, else move camera",
        "",
        "Guns and C-RAM kill rockets and bombs. Missiles need a",
        "solid radar track to fire. Point defense (C-RAM, Iron",
        "Dome, THAAD) takes half damage and decoys anti-radar",
        "missiles. Build an airfield to strike back.",
    };
    for (unsigned i = 0; i < sizeof help / sizeof *help; i++) { gx_text(F_S, 6, y, C_DIM, help[i]); y += 12; }
    list_end(L_OPTIONS, y + 4, y0);
}

static bool stepper(float x, float y, float w, const char *label, const char *value, int *v, int lo, int hi) {
    gx_text(F_S, x, y + 9, C_DIM, label);
    float bx = x + 70;
    bool ch = false;
    if (button(bx, y, 36, 30, "<", B_NORMAL, *v > lo)) { (*v)--; ch = true; }
    gx_panel(bx + 38, y, w - 70 - 76, 30, C_PANEL, C_EDGE);
    label_fit(bx + 38 + (w - 146) / 2, y, 30, w - 146, C_TEXT, value);
    if (button(x + w - 36, y, 36, 30, ">", B_NORMAL, *v < hi)) { (*v)++; ch = true; }
    return ch;
}

static void creative_page(void) {
    static const char *const edges[3] = {"North (airfields)", "West flank", "East (carrier)"};
    char t[48];
    float y0 = list_begin(L_CREATIVE, CY0 + SEG_H, CY1), y = y0 + 4;
    stepper(4, y, 312, "Aircraft", enemy_specs[G.sp_type].name, &G.sp_type, 0, N_ENEMY - 1);
    y += 34;
    snprintf(t, sizeof t, "%d", G.sp_count);
    stepper(4, y, 312, "Count", t, &G.sp_count, 1, 12);
    y += 34;
    if (!G.sites[G.sp_target].built) G.sp_target = 0;
    snprintf(t, sizeof t, "TARGET: %s", site_specs[G.sp_target].name);
    if (button(4, y, 154, 30, t, B_NORMAL, true)) {
        do { G.sp_target = (G.sp_target + 1) % N_SITE; } while (!G.sites[G.sp_target].built);
    }
    snprintf(t, sizeof t, "FROM: %s", edges[G.sp_edge]);
    if (button(162, y, 154, 30, t, B_NORMAL, true)) G.sp_edge = (G.sp_edge + 1) % 3;
    y += 34;
    if (button(4, y, 154, 32, "LAUNCH", B_PRIMARY, true)) { G.sp_placing = false; creative_spawn(G.sp_type, G.sp_count, G.sp_target, G.sp_edge, false, 0, 0); }
    if (button(162, y, 154, 32, "PLACE ON MAP", B_PRIMARY, true)) { cancel_placement(); deselect_all(); G.sp_placing = true; A.tab = TAB_MAP; }
    y += 40;
    gx_text(F_S, 6, y, C_RED, "STRIKES");
    y += 13;
    if (button(4, y, 76, 30, "RAID", B_DANGER, true)) creative_raid();
    if (button(83, y, 76, 30, "ISKANDER", B_DANGER, true)) creative_strike(M_SRBM);
    if (button(162, y, 76, 30, "KH-101", B_DANGER, true)) creative_strike(M_ALCM);
    if (button(241, y, 75, 30, "ICBM", B_DANGER, true)) creative_strike(M_ICBM);
    y += 38;
    if (button(4, y, 154, 30, G.auto_waves ? "AUTO WAVES: ON" : "AUTO WAVES: OFF", G.auto_waves ? B_ON : B_NORMAL, true)) G.auto_waves = !G.auto_waves;
    if (button(162, y, 154, 30, G.indestructible ? "INDESTRUCTIBLE: ON" : "INDESTRUCTIBLE: OFF", G.indestructible ? B_ON : B_NORMAL, true)) G.indestructible = !G.indestructible;
    y += 34;
    int lvl = G.level;
    snprintf(t, sizeof t, "%d", G.level);
    if (stepper(4, y, 312, "Threat level", t, &lvl, 1, 20)) set_threat_level(lvl);
    y += 34;
    snprintf(t, sizeof t, "CLEAR THE SKY (%d hostile%s)", G.hostiles, G.hostiles == 1 ? "" : "s");
    if (button(4, y, 312, 30, t, B_NORMAL, true)) clear_hostiles();
    y += 36;
    gx_wrap(F_S, 6, y, 308, C_DIM, "On the map, tap an enemy aircraft to select it, then tap any base or unit to send it there.", 2);
    y += 28;
    list_end(L_CREATIVE, y, y0);
}

static void tab_more(void) {
    static const char *const subs[] = {"LOG", "STATS", "OPTIONS", "CREATIVE"};
    int n = creative() ? 4 : 3;
    if (A.sub[TAB_MORE] >= n) A.sub[TAB_MORE] = 0;
    switch (A.sub[TAB_MORE]) {
    case 0: log_page(); break;
    case 1: stats_page(); break;
    case 2: options_page(); break;
    default: creative_page();
    }
    A.sub[TAB_MORE] = segments(subs, n, A.sub[TAB_MORE]);
}

/* ---------- Chrome ---------- */

static void status_bar(void) {
    char t[48];
    gx_rect(0, 0, 320, STATUS_H, RGBA(10, 14, 12, 255));
    gx_rect(0, STATUS_H - 1, 320, 1, C_EDGE);
    float x = 5;
    if (creative()) x += gx_text(F_M, x, 2, C_GOLD, "FREE BUILD");
    else {
        fmt_money(t, sizeof t, G.money);
        x += gx_text(F_M, x, 2, C_GOLD, t);
        snprintf(t, sizeof t, "+%d/s", income_per_second());
        gx_text(F_S, x + 4, 4, C_DIM, t);
    }
    snprintf(t, sizeof t, "LV %d", G.level);
    gx_text(F_M, 134, 2, C_TEXT, t);
    if (!creative()) hp_bar(134.0f + gx_textw(F_M, t) + 4, 6, 30, 5, (float)G.xp / xp_to_next(), C_BLUE);
    snprintf(t, sizeof t, "Units %d/%d", placed_count(), emplacement_cap());
    gx_text(F_S, 208, 4, C_DIM, t);
    snprintf(t, sizeof t, "Jets %d/%d", hangars_used(), hangars_total());
    gx_text_r(F_S, 316, 4, C_DIM, t);
}

static void tab_bar(void) {
    static const char *const names[N_TAB] = {"MAP", "WEAPONS", "BASES", "AIR WING", "MORE"};
    gx_rect(0, CY1, 320, TAB_H, RGBA(10, 14, 12, 255));
    gx_rect(0, CY1, 320, 1, C_EDGE);
    for (int i = 0; i < N_TAB; i++) {
        float x = i * 64.0f;
        bool on = A.tab == i, held = area_held(x, CY1, 64, TAB_H);
        if (on || held) gx_rect(x + 1, CY1 + 1, 62, TAB_H - 1, on ? RGBA(40, 58, 44, 255) : C_PANEL);
        if (on) gx_rect(x + 1, CY1 + 1, 62, 3, C_GREEN);
        label_fit(x + 32, CY1 + 3, TAB_H - 3, 64, on ? C_TEXT : C_DIM, names[i]);
        if (i) gx_rect(x, CY1 + 6, 1, TAB_H - 12, RGBA(50, 60, 50, 255));
        if (area_tap(x, CY1, 64, TAB_H)) { A.tab = i; arms_detail = hangar_detail = -1; jet_detail = NOREF; }
    }
    /* alert when something is inbound and you're in a menu */
    if (A.tab != TAB_MAP && G.hostiles > 0 && fmodf(A.t, 1.0f) < 0.6f) gx_circle(56, CY1 + 8, 3, C_RED);
}

static void title_bottom(void) {
    static const u32 dc[N_DIFF] = {RGBA(120, 220, 130, 255), RGBA(245, 200, 100, 255), RGBA(255, 110, 95, 255), RGBA(140, 200, 255, 255)};
    gx_rect(0, 0, 320, 240, C_BG);
    gx_text_c(F_M, 160, 5, C_TEXT, "SELECT DIFFICULTY");
    for (int i = 0; i < N_DIFF; i++) {
        float x = 6 + (i % 2) * 156, y = 24 + (i / 2) * 50;
        bool on = A.menu_diff == i, held = area_held(x, y, 152, 46);
        gx_panel(x, y, 152, 46, on ? RGBA(40, 56, 44, 255) : (held ? C_PANEL2 : C_PANEL), on ? dc[i] : C_EDGE);
        gx_rect(x + 1, y + 1, 4, 44, dc[i]);
        gx_text_c(F_L, x + 78, y + 10, on ? C_TEXT : C_DIM, diff_specs[i].title);
        if (area_tap(x, y, 152, 46)) A.menu_diff = i;
    }
    const DiffSpec *d = &diff_specs[A.menu_diff];
    float y = 128;
    y += gx_wrap(F_S, 8, y, 304, C_TEXT, d->tagline, 2) * 12 + 3;
    for (int i = 0; i < 3; i++) {
        gx_rect(10, y + 4, 3, 3, dc[A.menu_diff]);
        gx_text(F_S, 18, y, C_DIM, d->details[i]);
        y += 12;
    }
    if (button(40, 198, 240, 36, "DEPLOY", B_PRIMARY, true)) { game_start(A.menu_diff); ui_reset(); cam_focus(WORLD_W / 2, D(300)); }
}

static void overlay_menu(void) {
    char t[64];
    gx_rect(0, 0, 320, 240, RGBA(6, 8, 7, 225));
    if (G.over) {
        gx_text_c(F_L, 160, 22, C_RED, "DEFEAT");
        snprintf(t, sizeof t, "%s difficulty, threat level %d", settings()->title, G.level);
        gx_text_c(F_M, 160, 52, C_TEXT, t);
        snprintf(t, sizeof t, "%d aircraft shot down, %d munitions intercepted", G.kills, G.intercepts);
        gx_text_c(F_S, 160, 72, C_DIM, t);
        snprintf(t, sizeof t, "Held the line for %d:%02d", (int)G.time / 60, (int)G.time % 60);
        gx_text_c(F_S, 160, 86, C_DIM, t);
        if (button(40, 120, 240, 40, "TRY AGAIN", B_PRIMARY, true)) { game_restart(); ui_reset(); cam_focus(WORLD_W / 2, D(300)); }
        if (button(40, 168, 240, 40, "MAIN MENU", B_NORMAL, true)) { game_menu(); ui_reset(); }
        return;
    }
    gx_text_c(F_L, 160, 18, C_TEXT, "PAUSED");
    if (button(40, 56, 240, 40, "RESUME", B_PRIMARY, true)) { A.pause_menu = false; G.paused = false; }
    if (confirm_button(40, 104, 240, 40, "RESTART", 3)) { game_restart(); ui_reset(); A.pause_menu = false; cam_focus(WORLD_W / 2, D(300)); }
    if (confirm_button(40, 152, 240, 40, "QUIT TO MENU", 4)) { game_menu(); ui_reset(); A.pause_menu = false; }
    gx_text_c(F_S, 160, 206, C_DIM, "START resumes. Press START on the title screen to exit.");
}

void ui_bottom(void) {
    touch_update();
    gx_dx = 0;
    clip0 = 0; clip1 = 240;
    if (G.phase != PH_PLAY) { title_bottom(); return; }
    if (G.over || A.pause_menu) {
        /* keep the last screen underneath, but don't let it react */
        bool down = T.down, tap = T.tap;
        T.down = T.tap = false;
        if (A.tab == TAB_MAP) { View v = map_view(); world_draw(&v); }
        status_bar();
        T.down = down; T.tap = tap;
        overlay_menu();
        return;
    }
    switch (A.tab) {
    case TAB_MAP: tab_map(); break;
    case TAB_ARMS: tab_arms(); break;
    case TAB_BASES: tab_bases(); break;
    case TAB_AIR: tab_air(); break;
    default: tab_more();
    }
    status_bar();
    tab_bar();
}
