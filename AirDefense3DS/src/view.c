/* Draws the battlefield. The same code renders the stereoscopic top screen (once per eye, with every
   layer shifted by its depth) and the flat tactical map on the touch screen. */
#include "app.h"
#include "sim_int.h"

static const View *V;

#define COL_BG RGBF(0.07f, 0.085f, 0.075f, 1)
#define COL_SEA RGBF(0.05f, 0.10f, 0.15f, 1)
#define COL_OK RGBF(0.45f, 0.9f, 0.5f, 1)
#define COL_WARN RGBF(1, 0.75f, 0.3f, 1)
#define COL_BAD RGBF(1, 0.35f, 0.3f, 1)
#define COL_JET RGBF(0.30f, 0.58f, 0.95f, 1)
#define COL_JET_EDGE RGBF(0.72f, 0.88f, 1, 1)
#define COL_EN RGBF(0.85f, 0.30f, 0.27f, 1)
#define COL_EN_EDGE RGBF(1, 0.6f, 0.55f, 1)
#define COL_EB_FILL RGBF(0.26f, 0.13f, 0.12f, 1)
#define COL_EB_EDGE RGBF(0.92f, 0.44f, 0.38f, 1)

static inline float SX(float wx) { return V->x0 + V->w * 0.5f + (wx - V->cx) * V->s; }
static inline float SY(float wy) { return V->y0 + V->h * 0.5f - (wy - V->cy) * V->s; }
float view_wx(const View *v, float sx) { return v->cx + (sx - v->x0 - v->w * 0.5f) / v->s; }
float view_wy(const View *v, float sy) { return v->cy - (sy - v->y0 - v->h * 0.5f) / v->s; }

/* Stereo depth: the ground sits behind the screen, aircraft near the screen plane, and high-flying
   ballistic missiles come out of it. */
static inline void Z(float alt) {
    float d = 1.0f - 0.9f * alt;
    if (d < -1.6f) d = -1.6f;
    gx_dx = V->par * d;
}
static inline float zshift(float alt) {
    float d = 1.0f - 0.9f * alt;
    if (d < -1.6f) d = -1.6f;
    return V->par * d;
}

static bool vis(float wx, float wy, float margin) {
    float sx = SX(wx), sy = SY(wy), m = margin * V->s + 12;
    return sx > V->x0 - m && sx < V->x0 + V->w + m && sy > V->y0 - m && sy < V->y0 + V->h + m;
}
/* False when a circle outline can't cross the view (fully outside, or the view is fully inside it). */
static bool ring_vis(float wx, float wy, float r) {
    float hw = V->w * 0.5f / V->s + 20, hh = V->h * 0.5f / V->s + 20;
    float dx = fabsf(wx - V->cx), dy = fabsf(wy - V->cy);
    if (dx > hw + r || dy > hh + r) return false;
    float fx = dx + hw, fy = dy + hh;
    return fx * fx + fy * fy >= r * r;
}
static void wring(float wx, float wy, float r, float th, u32 c, bool dashed) {
    if (ring_vis(wx, wy, r)) gx_ring(SX(wx), SY(wy), r * V->s, th, c, dashed);
}
/* Rectangle given relative to a centre, y up. */
static void wrect(float cx, float cy, float x, float y, float w, float h, u32 c) {
    gx_rect(SX(cx + x), SY(cy + y + h), w * V->s, h * V->s, c);
}
static void wframe(float cx, float cy, float x, float y, float w, float h, u32 c) {
    gx_frame(SX(cx + x), SY(cy + y + h), w * V->s, h * V->s, 1, c);
}
static void wline(float x0, float y0, float x1, float y1, float th, u32 c) {
    gx_line(SX(x0), SY(y0), SX(x1), SY(y1), th, c);
}
static void wdashbox(float cx, float cy, Rect r, u32 c) {
    float x0 = SX(cx + r.x), x1 = SX(cx + r.x + r.w), y0 = SY(cy + r.y + r.h), y1 = SY(cy + r.y);
    for (float x = x0; x < x1; x += 8) {
        float w = minf(5, x1 - x);
        gx_rect(x, y0, w, 1, c);
        gx_rect(x, y1, w, 1, c);
    }
    for (float y = y0; y < y1; y += 8) {
        float h = minf(5, y1 - y);
        gx_rect(x0, y, 1, h, c);
        gx_rect(x1, y, 1, h, c);
    }
}
static void bar(float sx, float sy, float w, float h, float f, u32 c) {
    gx_rect(sx - w / 2, sy, w, h, RGBF(0.18f, 0.18f, 0.18f, 1));
    gx_rect(sx - w / 2, sy, w * clampf(f, 0, 1), h, c);
}
static u32 hp_color(float f) { return f > 0.6f ? COL_OK : (f > 0.3f ? COL_WARN : COL_BAD); }

void fmt_money(char *out, size_t n, int v) {
    if (v < 0) v = 0;
    if (v >= 1000000) snprintf(out, n, "$%d,%03d,%03d", v / 1000000, (v / 1000) % 1000, v % 1000);
    else if (v >= 1000) snprintf(out, n, "$%d,%03d", v / 1000, v % 1000);
    else snprintf(out, n, "$%d", v);
}

/* ---------- Terrain ---------- */

static struct { float x, y, rx, ry, rot; } blobs[30];
static struct { float x, y; } waves[70];
static bool terrain_ready;

static void terrain_init(void) {
    u32 s = 0xC0FFEE11u;
#define TR() (s ^= s << 13, s ^= s >> 17, s ^= s << 5, (float)(s >> 8) / 16777216.0f)
    for (int i = 0; i < 30; i++) {
        do {
            blobs[i].x = TR() * WORLD_W;
            blobs[i].y = D(180) + TR() * (WORLD_H - D(180));
        } while (is_water(blobs[i].x, blobs[i].y));
        blobs[i].rx = (90 + TR() * 190) * 0.5f;
        blobs[i].ry = (50 + TR() * 120) * 0.5f;
        blobs[i].rot = TR() * PI;
    }
    for (int i = 0; i < 70; i++) {
        waves[i].y = TR() * WORLD_H;
        float c = coast_x(waves[i].y) + 30;
        waves[i].x = c + TR() * (WORLD_W + 60 - c);
    }
#undef TR
    terrain_ready = true;
}

static void draw_terrain(void) {
    if (!terrain_ready) terrain_init();
    float pad = 16 / V->s;
    float wl = view_wx(V, V->x0) - pad, wr = view_wx(V, V->x0 + V->w) + pad;
    float wb = view_wy(V, V->y0 + V->h) - pad, wt = view_wy(V, V->y0) + pad;
    Z(0);
    /* land texture */
    if (!V->tac) {
        u32 c = RGBF(0.16f, 0.2f, 0.15f, 0.35f);
        for (int i = 0; i < 30; i++) {
            if (!vis(blobs[i].x, blobs[i].y, blobs[i].rx)) continue;
            float cx = SX(blobs[i].x), cy = SY(blobs[i].y), ca = cosf(blobs[i].rot), sa = sinf(blobs[i].rot);
            float px = 0, py = 0;
            for (int k = 0; k <= 12; k++) {
                float a = 2 * PI * k / 12, ex = cosf(a) * blobs[i].rx * V->s, ey = sinf(a) * blobs[i].ry * V->s;
                float x = cx + ex * ca - ey * sa, y = cy + ex * sa + ey * ca;
                if (k) plat_tri(cx + gx_dx, cy, px + gx_dx, py, x + gx_dx, y, c);
                px = x; py = y;
            }
        }
    }
    /* enemy territory and the front line */
    {
        float x0 = floorf(wl / 40) * 40;
        u32 tint = RGBF(0.9f, 0.2f, 0.15f, V->tac ? 0.10f : 0.07f);
        for (float x = x0; x < wr; x += 40) {
            float fy0 = front_y(x), fy1 = front_y(x + 40);
            if (fy0 > wt && fy1 > wt) continue;
            gx_quad(SX(x), SY(fy0), SX(x + 40), SY(fy1), SX(x + 40), SY(wt), SX(x), SY(wt), tint);
        }
        u32 fc = RGBF(1, 0.5f, 0.4f, 0.6f);
        float step = V->s < 0.3f ? 40 : 20;
        x0 = floorf(wl / (step * 2)) * step * 2;
        for (float x = x0; x < wr; x += step * 2) {
            float fy0 = front_y(x), fy1 = front_y(x + step * 1.3f);
            if ((fy0 < wb && fy1 < wb) || (fy0 > wt && fy1 > wt)) continue;
            wline(x, fy0, x + step * 1.3f, fy1, V->tac ? 1 : 1.5f, fc);
        }
    }
    /* sea along the east coast */
    {
        float y0 = floorf(wb / 40) * 40;
        for (float y = y0; y < wt; y += 40) {
            float c0 = coast_x(y), c1 = coast_x(y + 40);
            if (c0 > wr && c1 > wr) continue;
            gx_quad(SX(c0), SY(y), SX(wr), SY(y), SX(wr), SY(y + 40), SX(c1), SY(y + 40), COL_SEA);
            wline(c0, y, c1, y + 40, 1.5f, RGBF(0.45f, 0.6f, 0.65f, 0.5f));
        }
        if (!V->tac) {
            u32 wc = RGBF(0.5f, 0.7f, 0.8f, 0.2f);
            float drift = fmodf(G.real_time * 3, 28);
            for (int i = 0; i < 70; i++) {
                if (!vis(waves[i].x, waves[i].y, 20)) continue;
                gx_rect(SX(waves[i].x + drift - 14), SY(waves[i].y), 14 * V->s, 1, wc);
            }
        }
    }
    /* grid, map border, range rings around the HQ */
    if (!V->tac && V->s >= 0.3f) {
        u32 gc = RGBF(0.45f, 0.55f, 0.45f, 0.07f);
        for (float x = maxf(0, ceilf(wl / 50) * 50); x <= minf(WORLD_W, wr); x += 50)
            gx_rect(SX(x), SY(minf(WORLD_H, wt)), 1, (minf(WORLD_H, wt) - maxf(0, wb)) * V->s, gc);
        for (float y = maxf(0, ceilf(wb / 50) * 50); y <= minf(WORLD_H, wt); y += 50)
            gx_rect(SX(maxf(0, wl)), SY(y), (minf(WORLD_W, wr) - maxf(0, wl)) * V->s, 1, gc);
    }
    gx_frame(SX(0), SY(WORLD_H), WORLD_W * V->s, WORLD_H * V->s, 1, RGBF(0.6f, 0.7f, 0.55f, 0.25f));
    if (!V->tac) {
        for (int k = 1; k <= 4; k++) {
            float r = D(200) * k;
            wring(site_specs[0].x, site_specs[0].y, r, 1, RGBF(0.6f, 0.7f, 0.55f, 0.16f), true);
            if (V->s >= 0.35f && vis(site_specs[0].x, site_specs[0].y + r, 10)) {
                char t[16];
                snprintf(t, sizeof t, "%.1f km", r * 25 / GS / 1000);
                gx_text(F_S, SX(site_specs[0].x) + 3, SY(site_specs[0].y + r) - 11, RGBF(0.6f, 0.7f, 0.55f, 0.5f), t);
            }
        }
        if (V->s >= 0.3f) {
            u32 tc = RGBF(1, 0.55f, 0.5f, 0.6f);
            for (float x = D(160); x <= WORLD_W - 100; x += D(460))
                if (vis(x, front_y(x), 60)) gx_text_c(F_S, SX(x), SY(front_y(x) - 8), tc, "FRONT LINE");
            if (vis(D(1050), WORLD_H - 40, 100)) gx_text_c(F_M, SX(D(1050)), SY(WORLD_H - 30), RGBF(1, 0.55f, 0.5f, 0.45f), "ENEMY TERRITORY");
            if (vis(D(1830), D(800), 60)) gx_text_c(F_S, SX(D(1830)), SY(D(800)), RGBF(0.5f, 0.7f, 0.8f, 0.5f), "OPEN SEA");
        }
    }
}

/* ---------- Bases ---------- */

static void name_tag(float wx, float wy, float off_px, u32 c, const char *name) {
    if (V->s < (V->tac ? 0.25f : 0.3f)) return;
    char up[40];
    int i = 0;
    for (; name[i] && i < 39; i++) up[i] = (name[i] >= 'a' && name[i] <= 'z') ? (char)(name[i] - 32) : name[i];
    up[i] = 0;
    gx_text_c(F_S, SX(wx), SY(wy) + off_px, c, up);
}

static void draw_site_structure(int s) {
    const SiteSpec *sp = &site_specs[s];
    float x = sp->x, y = sp->y;
    u32 name_c = RGBF(0.8f, 0.85f, 0.7f, 1);
    switch (sp->kind) {
    case SK_HQ:
        wrect(x, y, -110, -68, 220, 12, RGBF(0.2f, 0.2f, 0.2f, 1));
        wframe(x, y, -110, -68, 220, 12, RGBF(0.35f, 0.35f, 0.35f, 1));
        wrect(x, y, -60, -35, 120, 70, RGBF(0.15f, 0.17f, 0.14f, 1));
        wframe(x, y, -60, -35, 120, 70, RGBF(0.55f, 0.6f, 0.48f, 1));
        wrect(x, y, -51, -23, 26, 18, RGBF(0.26f, 0.29f, 0.24f, 1));
        wrect(x, y, -13, 5, 26, 18, RGBF(0.26f, 0.29f, 0.24f, 1));
        wrect(x, y, 25, -23, 26, 18, RGBF(0.26f, 0.29f, 0.24f, 1));
        name_tag(x, y - 84, 0, name_c, sp->name);
        break;
    case SK_FOB:
        wrect(x, y, -52, -35, 104, 70, RGBF(0.14f, 0.16f, 0.13f, 1));
        wframe(x, y, -52, -35, 104, 70, RGBF(0.62f, 0.56f, 0.40f, 1));
        wrect(x, y, -39, 4, 30, 16, RGBF(0.27f, 0.30f, 0.25f, 1));
        wrect(x, y, 7, 4, 30, 16, RGBF(0.27f, 0.30f, 0.25f, 1));
        gx_circle(SX(x), SY(y - 14), 11 * V->s, RGBF(0.18f, 0.18f, 0.18f, 1));
        gx_ring(SX(x), SY(y - 14), 11 * V->s, 1, RGBF(0.55f, 0.55f, 0.55f, 1), false);
        if (V->s >= 0.6f) gx_text_c(F_S, SX(x), SY(y - 14) - 5, RGBF(0.75f, 0.75f, 0.75f, 1), "H");
        name_tag(x, y - 50, 0, name_c, sp->name);
        break;
    case SK_AIRFIELD:
        wrect(x, y, -110, -8, 220, 16, RGBF(0.19f, 0.19f, 0.19f, 1));
        wframe(x, y, -110, -8, 220, 16, RGBF(0.36f, 0.36f, 0.36f, 1));
        if (V->s >= 0.35f)
            for (float dx = -96; dx < 96; dx += 16) wrect(x, y, dx, -0.5f, 8, 1, RGBF(0.7f, 0.7f, 0.7f, 0.7f));
        wrect(x, y, -48, 14, 96, 38, RGBF(0.16f, 0.18f, 0.16f, 1));
        wframe(x, y, -48, 14, 96, 38, RGBF(0.32f, 0.32f, 0.32f, 1));
        if (V->s >= 0.4f)
            for (int i = 0; i < 4; i++) wframe(x, y, -42.0f + i * 22, 18, 18, 30, RGBF(0.42f, 0.42f, 0.42f, 0.8f));
        wrect(x, y, 59, 23, 10, 10, RGBF(0.3f, 0.34f, 0.3f, 1));
        name_tag(x, y - 26, 0, name_c, sp->name);
        break;
    default: { /* carrier */
        static const float hull[7][2] = {{0, 112}, {22, 72}, {24, -90}, {16, -100}, {-16, -100}, {-24, -90}, {-22, 72}};
        u32 hc = RGBF(0.30f, 0.33f, 0.36f, 1), ec = RGBF(0.62f, 0.70f, 0.78f, 1);
        if (!V->tac) {
            u32 wk = RGBF(0.9f, 0.9f, 0.9f, 0.25f);
            wline(x - 14, y - 102, x - 30, y - 170, 2, wk);
            wline(x + 14, y - 102, x + 30, y - 170, 2, wk);
        }
        for (int i = 1; i < 6; i++)
            plat_tri(SX(x + hull[0][0]) + gx_dx, SY(y + hull[0][1]), SX(x + hull[i][0]) + gx_dx, SY(y + hull[i][1]),
                     SX(x + hull[i + 1][0]) + gx_dx, SY(y + hull[i + 1][1]), hc);
        for (int i = 0; i < 7; i++)
            wline(x + hull[i][0], y + hull[i][1], x + hull[(i + 1) % 7][0], y + hull[(i + 1) % 7][1], 1, ec);
        if (V->s >= 0.35f) {
            wline(x - 10, y - 92, x + 12, y + 30, 1, RGBF(0.85f, 0.85f, 0.85f, 0.5f));
            wline(x, y + 40, x, y + 104, 1, RGBF(0.85f, 0.85f, 0.85f, 0.5f));
        }
        wrect(x, y, 15, -8, 8, 30, RGBF(0.45f, 0.5f, 0.55f, 1));
        name_tag(x, y - 116, 0, name_c, sp->name);
    }
    }
}

static void draw_sites(void) {
    Z(0);
    for (int s = 0; s < N_SITE; s++) {
        const SiteSpec *sp = &site_specs[s];
        const Site *st = &G.sites[s];
        Rect nb = site_nb[sp->kind];
        if (!vis(sp->x, sp->y, 180)) continue;
        if (st->built) {
            draw_site_structure(s);
            float f = st->max_hp > 0 ? st->hp / st->max_hp : 0;
            bar(SX(sp->x), SY(sp->y + nb.y + nb.h + 9) - 2, maxf(24, 70 * V->s), V->tac ? 3 : 4, f, hp_color(f));
            /* construction flash */
            float age = G.real_time - st->born;
            if (age < 0.6f) wframe(sp->x, sp->y, nb.x - 6 - age * 40, nb.y - 6 - age * 40, nb.w + 12 + age * 80, nb.h + 12 + age * 80, gx_alpha(RGBA(255, 255, 255, 255), 1 - age / 0.6f));
        } else if (st->destroyed) {
            Rect b = site_fp[sp->kind][0];
            static const float pc[5][4] = {{0.1f, 0.2f, 0.25f, 0.3f}, {0.45f, 0.1f, 0.2f, 0.25f}, {0.7f, 0.45f, 0.22f, 0.3f}, {0.2f, 0.6f, 0.3f, 0.2f}, {0.5f, 0.55f, 0.15f, 0.2f}};
            wrect(sp->x, sp->y, b.x, b.y, b.w, b.h, RGBF(0.05f, 0.05f, 0.05f, 0.35f));
            for (int i = 0; i < 5; i++) {
                wrect(sp->x, sp->y, b.x + b.w * pc[i][0], b.y + b.h * pc[i][1], b.w * pc[i][2], b.h * pc[i][3], RGBF(0.18f, 0.16f, 0.14f, 1));
                wframe(sp->x, sp->y, b.x + b.w * pc[i][0], b.y + b.h * pc[i][1], b.w * pc[i][2], b.h * pc[i][3], RGBF(0.35f, 0.28f, 0.22f, 1));
            }
            if (V->s >= 0.3f) {
                char t[48];
                if (!unlocked(sp->unlock)) snprintf(t, sizeof t, "DESTROYED");
                else if (creative()) snprintf(t, sizeof t, "DESTROYED - rebuild free");
                else snprintf(t, sizeof t, "DESTROYED - rebuild $%d", site_price(s));
                gx_text_c(F_S, SX(sp->x), SY(sp->y + nb.y) + 2, RGBF(1, 0.5f, 0.45f, 0.9f), t);
            }
        } else {
            bool open = unlocked(sp->unlock);
            wdashbox(sp->x, sp->y, nb, RGBF(0.6f, 0.66f, 0.56f, open ? 0.7f : 0.4f));
            if (V->s >= 0.3f) {
                char t[48];
                name_tag(sp->x, sp->y + nb.y + nb.h / 2, -11, RGBF(0.75f, 0.8f, 0.7f, 0.8f), sp->name);
                if (!open) snprintf(t, sizeof t, "Unlocks at Lv %d", sp->unlock);
                else if (creative()) snprintf(t, sizeof t, "Free - tap to build");
                else snprintf(t, sizeof t, "$%d - tap to build", sp->cost);
                gx_text_c(F_S, SX(sp->x), SY(sp->y + nb.y + nb.h / 2) + 1, open ? RGBF(0.96f, 0.78f, 0.4f, 0.9f) : RGBF(0.6f, 0.66f, 0.56f, 0.7f), t);
            } else if (open) {
                gx_text_c(F_S, SX(sp->x), SY(sp->y) - 5, RGBF(0.96f, 0.78f, 0.4f, 0.9f), "+");
            }
        }
        if (G.sel_site == s) {
            float pulse = 0.6f + 0.4f * sinf(G.real_time * 6);
            wframe(sp->x, sp->y, nb.x - 6, nb.y - 6, nb.w + 12, nb.h + 12, RGBF(1, 1, 1, 0.85f * pulse));
        }
    }
}

static void draw_enemy_bases(void) {
    Z(0);
    for (int b = 0; b < N_EB; b++) {
        const EBSpec *s = &eb_specs[b];
        const EBase *eb = &G.eb[b];
        if (!vis(s->x, s->y, s->sam_r)) continue;
        bool active = unlocked(s->unlock), down = eb->down_left > 0;
        float dim = active ? 1 : 0.4f;
        u32 fill = gx_alpha(COL_EB_FILL, dim), edge = gx_alpha(COL_EB_EDGE, dim);
        if (active && !down) wring(s->x, s->y, s->sam_r, 1, RGBF(1, 0.4f, 0.35f, 0.25f), true);
        float x = s->x, y = s->y;
        if (down) {
            wrect(x, y, -40, -24, 80, 48, RGBF(0.05f, 0.05f, 0.05f, 0.4f));
            wrect(x, y, -30, -14, 22, 16, RGBF(0.18f, 0.16f, 0.14f, 1));
            wrect(x, y, 4, -6, 26, 14, RGBF(0.18f, 0.16f, 0.14f, 1));
            wrect(x, y, -12, 6, 18, 12, RGBF(0.18f, 0.16f, 0.14f, 1));
            /* smoke */
            if (!V->tac)
                for (int k = 0; k < 3; k++) {
                    float ph = fmodf(G.real_time * 0.5f + k * 0.33f, 1);
                    gx_circle(SX(x - 10 + k * 12 + ph * 14), SY(y + ph * 46), (4 + ph * 9) * V->s, RGBF(0.3f, 0.3f, 0.3f, 0.35f * (1 - ph)));
                }
        } else switch (b) {
        case EB_AIRSTRIP:
            wrect(x, y, -75, -7, 150, 14, fill); wframe(x, y, -75, -7, 150, 14, edge);
            wrect(x, y, -40, 12, 22, 14, fill); wframe(x, y, -40, 12, 22, 14, edge);
            wrect(x, y, -10, 12, 22, 14, fill); wframe(x, y, -10, 12, 22, 14, edge);
            break;
        case EB_SAMBELT:
            for (int k = -1; k <= 1; k++) {
                wrect(x, y, k * 34 - 9.0f, -22, 18, 18, fill); wframe(x, y, k * 34 - 9.0f, -22, 18, 18, edge);
                wline(x + k * 34, y - 13, x + k * 34 + 6, y + 4, 2, edge);
            }
            gx_circle(SX(x), SY(y + 18), 9 * V->s, fill);
            gx_ring(SX(x), SY(y + 18), 9 * V->s, 1, edge, false);
            if (!V->tac) wline(x, y + 18, x + cosf(-G.real_time * 2) * 16, y + 18 + sinf(-G.real_time * 2) * 16, 1, edge);
            break;
        case EB_ISKANDER:
            for (int k = -1; k <= 1; k++) {
                wrect(x, y, k * 30 - 7.0f, -20, 14, 40, fill); wframe(x, y, k * 30 - 7.0f, -20, 14, 40, edge);
                wrect(x, y, k * 30 - 2.0f, -14, 4, 28, edge);
            }
            break;
        case EB_AIRBASE:
            wrect(x, y, -90, -8, 180, 16, fill); wframe(x, y, -90, -8, 180, 16, edge);
            wrect(x, y, -8, -50, 16, 100, fill); wframe(x, y, -8, -50, 16, 100, edge);
            for (int k = 0; k < 3; k++) { wrect(x, y, 24.0f + k * 22, 16, 18, 14, fill); wframe(x, y, 24.0f + k * 22, 16, 18, 14, edge); }
            wrect(x, y, -60, 16, 30, 20, fill); wframe(x, y, -60, 16, 30, 20, edge);
            break;
        default: {
            static const float hull[7][2] = {{0, 70}, {15, 45}, {16, -56}, {10, -62}, {-10, -62}, {-16, -56}, {-15, 45}};
            for (int i = 1; i < 6; i++)
                plat_tri(SX(x + hull[0][0]) + gx_dx, SY(y + hull[0][1]), SX(x + hull[i][0]) + gx_dx, SY(y + hull[i][1]),
                         SX(x + hull[i + 1][0]) + gx_dx, SY(y + hull[i + 1][1]), fill);
            for (int i = 0; i < 7; i++)
                wline(x + hull[i][0], y + hull[i][1], x + hull[(i + 1) % 7][0], y + hull[(i + 1) % 7][1], 1, edge);
            wrect(x, y, 9, -6, 6, 22, edge);
            /* escorts */
            wrect(x, y, -46, 10, 8, 26, fill); wframe(x, y, -46, 10, 8, 26, edge);
            wrect(x, y, 40, -30, 8, 26, fill); wframe(x, y, 40, -30, 8, 26, edge);
        }
        }
        if (active) {
            float f = eb->health / s->health;
            bar(SX(x), SY(y + 58), maxf(22, 70 * V->s), V->tac ? 3 : 4, f, RGBF(1, 0.42f, 0.36f, 1));
        }
        name_tag(x, y + 62, -13, edge, s->name);
        if (V->s >= 0.3f) {
            char t[40];
            if (!active) snprintf(t, sizeof t, "Intel at Lv %d", s->unlock);
            else if (down) snprintf(t, sizeof t, "DESTROYED - %ds", (int)ceilf(eb->down_left));
            else snprintf(t, sizeof t, "SAM %d%%%s", (int)(effective_defense(b) * 100 + 0.5f), s->antiship ? " - ships only" : "");
            gx_text_c(F_S, SX(x), SY(y - 72), RGBF(1, 0.72f, 0.66f, 0.85f), t);
        }
        bool chosen = G.strike_target == b;
        if (eb->targeted || chosen) {
            float pulse = 0.65f + 0.35f * sinf(G.real_time * 8);
            wring(x, y, 58, 2, RGBF(0.96f, 0.72f, 0.28f, eb->targeted ? 0.9f * pulse : 0.5f), !eb->targeted);
        }
    }
}

/* ---------- Units ---------- */

static void draw_defense(const Defense *d, bool selected) {
    const DefSpec *s = &def_specs[d->spec];
    float grow = clampf((G.real_time - d->born) / 0.25f, 0, 1);
    float sc = (d->fixed ? 0.8f : 1.0f) * (0.2f + 0.8f * grow);
    float px = 26 * V->s * sc;
    if (px < (V->tac ? 5 : 6)) px = V->tac ? 5 : 6;
    float cx = SX(d->x), cy = SY(d->y);
    gx_rect(cx - px / 2, cy - px / 2, px, px, RGBF(0.11f, 0.13f, 0.11f, 1));
    gx_frame(cx - px / 2, cy - px / 2, px, px, 1, s->color);
    float k = px / 26;
    if (px >= 10) {
        float a = d->aim;
        if (s->weapon == WPN_GUN) {
            gx_rrect(cx + cosf(a) * 7 * k, cy - sinf(a) * 7 * k, 3 * k, 12 * k, a, s->color);
            gx_circle(cx, cy, 5 * k, gx_mix(RGBF(0.11f, 0.13f, 0.11f, 1), s->color, 0.6f));
        } else {
            float ox = -sinf(a) * 4.5f * k, oy = -cosf(a) * 4.5f * k;
            gx_rrect(cx + ox, cy + oy, 5 * k, 16 * k, a, gx_mix(RGBF(0.11f, 0.13f, 0.11f, 1), s->color, 0.75f));
            gx_rrect(cx - ox, cy - oy, 5 * k, 16 * k, a, gx_mix(RGBF(0.11f, 0.13f, 0.11f, 1), s->color, 0.75f));
        }
        if (s->sensor == SENS_RADAR && !V->tac) {
            float period = maxf(0.6f, s->scan * 4), sa = -2 * PI * fmodf(G.time, period) / period + d->x;
            gx_line(cx + 9 * k, cy - 9 * k, cx + 9 * k + cosf(sa) * 14 * k, cy - 9 * k - sinf(sa) * 14 * k, 1, gx_alpha(s->color, 0.7f));
        }
    } else {
        gx_rect(cx - 1, cy - 1, 2, 2, s->color);
    }
    /* ammo and health */
    float bw = maxf(px - 2, 6), by = cy + px / 2 + 2;
    if (d->reload_left > 0) gx_rect(cx - bw / 2, by, bw * clampf(1 - d->reload_left / s->reload, 0.04f, 1), 2, RGBF(1, 0.65f, 0.2f, 1));
    else gx_rect(cx - bw / 2, by, bw * clampf((float)d->ammo / s->mag, 0.04f, 1), 2, s->color);
    float f = d->hp / s->hp;
    if (f < 0.999f) gx_rect(cx - bw / 2, cy - px / 2 - 4, bw * clampf(f, 0.04f, 1), 2, hp_color(f));
    if (!V->tac && V->s >= 0.45f) gx_text_c(F_S, cx, by + 2, s->color, s->tag);
    if (selected) {
        float pulse = 0.5f + 0.5f * sinf(G.real_time * 6);
        float b = px + 8;
        gx_frame(cx - b / 2, cy - b / 2, b, b, 1, RGBF(1, 1, 1, 0.4f + 0.6f * pulse));
    }
}

static void draw_defenses(void) {
    Z(0);
    Defense *sel = def_get(G.sel_def);
    /* range rings first so they sit under every unit */
    for (int i = 0; i < MAX_DEF; i++) {
        const Defense *d = &G.def[i];
        if (!d->active) continue;
        const DefSpec *s = &def_specs[d->spec];
        if (d == sel) wring(d->x, d->y, s->engage_r, 1.5f, gx_alpha(s->color, 0.7f), true);
        else if (A.rings && !V->tac) wring(d->x, d->y, s->engage_r, 1, gx_alpha(s->color, 0.2f), true);
    }
    for (int i = 0; i < MAX_DEF; i++) {
        const Defense *d = &G.def[i];
        if (d->active && vis(d->x, d->y, 30)) draw_defense(d, d == sel);
    }
}

static void draw_sensors(void) {
    Z(0);
    if (G.upg[U_RADAR] > 0) {
        float x = site_specs[0].x, y = site_specs[0].y, r = ew_range();
        wring(x, y, r, 1, RGBF(0.45f, 0.85f, 1, 0.2f), true);
        if (!V->tac) {
            float a = -2 * PI * fmodf(G.time, 4) / 4;
            wline(x, y, x + cosf(a) * r, y + sinf(a) * r, 1.5f, RGBF(0.45f, 0.85f, 1, 0.16f));
            wline(x, y, x + cosf(a + 0.05f) * r, y + sinf(a + 0.05f) * r, 1.5f, RGBF(0.45f, 0.85f, 1, 0.08f));
        }
        gx_circle(SX(x), SY(y), maxf(3, 7 * V->s), RGBF(0.3f, 0.7f, 0.9f, 0.7f));
    }
}

static const struct { float w, h; u32 c; } mun_look[N_MUN] = {
    {2, 7, RGBA(255, 191, 102, 255)},  {2.6f, 2.6f, RGBA(217, 217, 217, 255)}, {2, 7, RGBA(255, 191, 102, 255)},
    {2.5f, 9, RGBA(255, 102, 191, 255)}, {3, 10, RGBA(255, 140, 77, 255)},   {3, 10, RGBA(255, 140, 77, 255)},
    {3, 10, RGBA(255, 140, 77, 255)},  {3, 13, RGBA(255, 242, 217, 255)},   {4, 16, RGBA(255, 242, 217, 255)},
};

/* Aircraft: the full silhouette when it is big enough to read, a plain arrowhead when it is not. */
static void plane(int shape, float cx, float cy, float size, float heading, u32 fill, u32 stroke) {
    if (size >= 8.5f) { gx_shape(shape, cx, cy, size, heading, fill, stroke); return; }
    float c = cosf(heading), s = -sinf(heading), w = shape == SH_BOMBER || shape == SH_ATTACK ? 0.85f : 0.7f;
    float nx = cx + gx_dx + c * size, ny = cy + s * size, tx = cx + gx_dx - c * size * 0.8f, ty = cy - s * size * 0.8f;
    float mx = cx + gx_dx - c * size * 0.35f, my = cy - s * size * 0.35f;
    plat_tri(nx, ny, tx - s * size * w, ty + c * size * w, mx, my, fill);
    plat_tri(nx, ny, tx + s * size * w, ty - c * size * w, mx, my, fill);
    (void)stroke;
}

static void dart(float wx, float wy, float w, float h, float ang, u32 c) {
    float k = V->s < 0.6f ? 0.6f : V->s;
    gx_rrect(SX(wx), SY(wy), maxf(1.5f, w * k), maxf(4, h * k), ang, c);
}

static void draw_air(void) {
    bool shadows = !V->tac && V->s >= 0.3f;
    float size_min = V->tac ? 5.5f : 7;
    float big = V->tac ? 1.0f : 1.3f; /* aircraft are drawn a little oversize on the top screen so they read */
    u32 shade = RGBA(0, 0, 0, 80);

    /* impact markers and strike designators on the ground */
    Z(0);
    for (int i = 0; i < MAX_MUN; i++) {
        const Munition *m = &G.mun[i];
        if (!m->c.alive || !m->marker || !vis(m->ax, m->ay, 80)) continue;
        float r = maxf(16, mun_specs[m->kind].splash), pulse = 0.65f + 0.35f * sinf(G.time * 12);
        u32 red = RGBF(1, 0.3f, 0.25f, 0.9f * pulse);
        gx_circle(SX(m->ax), SY(m->ay), r * V->s, RGBF(1, 0.3f, 0.25f, 0.12f * pulse));
        wring(m->ax, m->ay, r, 1.5f, red, false);
        wline(m->ax - r * 0.5f, m->ay, m->ax + r * 0.5f, m->ay, 1, red);
        wline(m->ax, m->ay - r * 0.5f, m->ax, m->ay + r * 0.5f, 1, red);
    }
    for (int i = 0; i < MAX_JETS; i++) {
        const Jet *j = &G.jets[i];
        if (j->alive && j->state == JS_OUTBOUND && j->strike_target >= 0)
            wline(j->x, j->y, eb_specs[j->strike_target].x, eb_specs[j->strike_target].y, 1, RGBF(0.96f, 0.72f, 0.28f, 0.35f));
    }
    /* shadows */
    if (shadows) {
        for (int i = 0; i < MAX_EN; i++) {
            const Enemy *e = &G.en[i];
            if (!e->c.alive || e->c.dispq < 0.3f || !vis(e->c.x, e->c.y, 40)) continue;
            plane(enemy_specs[e->spec].shape, SX(e->c.x + 9 * e->c.alt), SY(e->c.y - 13 * e->c.alt),
                     maxf(size_min, enemy_specs[e->spec].size * V->s * big), e->heading, shade, 0);
        }
        for (int i = 0; i < MAX_JETS; i++) {
            const Jet *j = &G.jets[i];
            if (!j->alive || j->state == JS_PARKED || !vis(j->x, j->y, 40)) continue;
            float a = jet_alt(j);
            plane(jet_specs[j->spec].shape, SX(j->x + 9 * a), SY(j->y - 13 * a), maxf(size_min, jet_specs[j->spec].size * V->s * big), j->heading, shade, 0);
        }
        for (int i = 0; i < MAX_MUN; i++) {
            const Munition *m = &G.mun[i];
            if (!m->c.alive || !MUN_BALLISTIC(m->kind) || !vis(m->c.x, m->c.y, 60)) continue;
            gx_circle(SX(m->c.x + 9 * m->c.alt), SY(m->c.y - 13 * m->c.alt), 2.5f, shade);
        }
    }
    /* bombs and Harpoons from your jets */
    for (int i = 0; i < MAX_ORD; i++) {
        const Ordnance *o = &G.ord[i];
        if (!o->alive) continue;
        float f = clampf(o->t / o->dur, 0, 1), x = o->fx + (o->tx - o->fx) * f, y = o->fy + (o->ty - o->fy) * f;
        Z(1 - f);
        if (o->missile) dart(x, y, 2.5f, 9, atan2f(o->ty - o->fy, o->tx - o->fx), RGBF(0.75f, 0.9f, 1, 1));
        else gx_circle(SX(x), SY(y), maxf(1.2f, 2.4f * (1 - 0.4f * f) * V->s), RGBF(0.9f, 0.9f, 0.9f, 1));
    }
    /* enemy weapons */
    for (int i = 0; i < MAX_MUN; i++) {
        const Munition *m = &G.mun[i];
        if (!m->c.alive || !vis(m->c.x, m->c.y, 20)) continue;
        Z(m->c.alt);
        u32 c = gx_alpha(mun_look[m->kind].c, 0.3f + 0.7f * m->c.dispq);
        if (m->kind == M_BOMB) gx_circle(SX(m->c.x), SY(m->c.y), maxf(1.5f, 2.6f * V->s), c);
        else dart(m->c.x, m->c.y, mun_look[m->kind].w, mun_look[m->kind].h, atan2f(m->ay - m->c.y, m->ax - m->c.x), c);
    }
    /* SAMs and air-to-air missiles */
    for (int i = 0; i < MAX_ICP; i++) {
        const Interceptor *m = &G.icp[i];
        if (!m->alive || !vis(m->x, m->y, 20)) continue;
        Z(m->alt);
        dart(m->x, m->y, 2.5f, 8, m->ang, RGBA(242, 242, 242, 255));
    }
    Z(1);
    for (int i = 0; i < MAX_AM; i++) {
        const AirMissile *m = &G.am[i];
        if (!m->alive || !vis(m->x, m->y, 20)) continue;
        dart(m->x, m->y, 2.2f, 7, m->ang, m->friendly ? RGBF(0.75f, 0.9f, 1, 1) : RGBF(1, 0.5f, 0.45f, 1));
    }
    /* hostile aircraft: brightness follows how well your sensors hold them */
    Enemy *sel = en_get(G.sel_enemy);
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &G.en[i];
        if (!e->c.alive || !vis(e->c.x, e->c.y, 40)) continue;
        const EnemySpec *s = &enemy_specs[e->spec];
        Z(e->c.alt);
        float a = 0.16f + 0.84f * e->c.dispq, size = maxf(size_min, s->size * V->s * big);
        float cx = SX(e->c.x), cy = SY(e->c.y);
        plane(s->shape, cx, cy, size, e->heading, gx_alpha(COL_EN, a), gx_alpha(COL_EN_EDGE, a));
        if (e->locked) {
            float b = size * 2.4f;
            gx_frame(cx - b / 2, cy - b / 2, b, b, 1, RGBF(1, 0.45f, 0.4f, 0.85f));
            if (!V->tac && V->s >= 0.4f) gx_text_c(F_S, cx, cy + b / 2 + 1, RGBF(1, 0.7f, 0.65f, 1), s->name);
        }
        if (e == sel) {
            float b = size * 2.4f + 6;
            gx_frame(cx - b / 2, cy - b / 2, b, b, 1, RGBA(255, 255, 255, 255));
        }
    }
    /* your aircraft */
    for (int i = 0; i < MAX_JETS; i++) {
        const Jet *j = &G.jets[i];
        if (!j->alive || !vis(j->x, j->y, 40)) continue;
        const JetSpec *s = &jet_specs[j->spec];
        bool parked = j->state == JS_PARKED;
        float alt = jet_alt(j);
        Z(alt);
        float size = maxf(parked ? size_min * 0.8f : size_min, s->size * V->s * big * (parked ? 0.7f : 0.7f + 0.3f * minf(1, alt * 2)));
        float a = parked && j->rearm_left > 0 ? 0.5f : 1;
        float cx = SX(j->x), cy = SY(j->y);
        plane(s->shape, cx, cy, size, j->heading, gx_alpha(COL_JET, a), gx_alpha(COL_JET_EDGE, a));
        if (!parked && !V->tac && V->s >= 0.4f) gx_text_c(F_S, cx, cy + size + 1, RGBF(0.6f, 0.82f, 1, 0.95f), j->title);
        float fl = j->flash - G.real_time;
        if (fl > 0) gx_ring(cx, cy, 14 + 6 * sinf(fl * 10), 2, RGBF(0.6f, 0.85f, 1, minf(1, fl)), false);
    }
    if (awacs_active() && vis(G.awx, G.awy, 40)) {
        Z(1.3f);
        float dx = -sinf(G.awacs_ang) * 110, dy = cosf(G.awacs_ang) * 55, size = maxf(size_min, 15 * V->s * big);
        if (shadows) { float save = gx_dx; Z(0); plane(SH_BOMBER, SX(G.awx + 12), SY(G.awy - 17), size, atan2f(dy, dx), shade, 0); gx_dx = save; }
        plane(SH_BOMBER, SX(G.awx), SY(G.awy), size, atan2f(dy, dx), RGBF(0.55f, 0.7f, 0.85f, 1), RGBF(0.8f, 0.9f, 1, 1));
        gx_circle(SX(G.awx), SY(G.awy), size * 0.5f, RGBF(0.9f, 0.9f, 0.9f, 0.85f));
        if (!V->tac && V->s >= 0.4f) gx_text_c(F_S, SX(G.awx), SY(G.awy) + size + 2, RGBF(0.6f, 0.82f, 1, 0.9f), "E-2C");
    }
}

static void draw_fx(void) {
    for (int i = 0; i < MAX_FX; i++) {
        const Fx *f = &G.fx[i];
        if (f->kind == FX_NONE || !vis(f->x, f->y, 140)) continue;
        float t = f->t, u = t / f->life;
        Z(f->alt);
        switch (f->kind) {
        case FX_EXPLOSION: {
            float r = f->r * (0.3f + 0.9f * minf(1, t / 0.25f)) * V->s, a = 1 - u;
            if (r < 1.5f) r = 1.5f;
            gx_circle(SX(f->x), SY(f->y), r, gx_alpha(f->color, a));
            if (!V->tac && f->r >= 9) gx_circle(SX(f->x), SY(f->y), r * 0.45f, RGBF(1, 0.97f, 0.85f, 0.8f * a * a));
            break;
        }
        case FX_SHOCK:
            gx_ring(SX(f->x), SY(f->y), f->r * (0.1f + 0.9f * minf(1, t / 0.45f)) * V->s, 2, gx_alpha(f->color, 1 - u), false);
            break;
        case FX_TRACER: {
            float x0 = SX(f->x) + zshift(f->alt), x1 = SX(f->x2) + zshift(f->r);
            gx_dx = 0;
            gx_line(x0, SY(f->y), x1, SY(f->y2), 1.2f, gx_alpha(f->color, 0.85f * (1 - u)));
            break;
        }
        case FX_PUFF:
            if (V->tac) break;
            gx_circle(SX(f->x), SY(f->y), maxf(1, 1.6f * V->s), gx_alpha(f->color, 1 - u));
            break;
        case FX_DOT:
            gx_circle(SX(f->x + f->x2 * u), SY(f->y + f->y2 * u), maxf(1, f->r * V->s), gx_alpha(f->color, 1 - u));
            break;
        case FX_LABEL:
            if (V->tac || V->s < 0.35f) break;
            gx_text_c(F_S, SX(f->x), SY(f->y + 14 * u) - 10, gx_alpha(f->color, 1 - u), f->text);
            break;
        }
    }
}

static void draw_placement(void) {
    if (!is_placing() && !G.sp_placing) return;
    Z(0);
    if (G.sp_placing) return;
    if (!creative())
        for (int s = 0; s < N_SITE; s++)
            if (G.sites[s].built && site_specs[s].deploy_r > 0)
                wring(site_specs[s].x, site_specs[s].y, site_specs[s].deploy_r, 1.5f, RGBF(0.78f, 0.88f, 0.62f, 0.55f), true);
    Defense *mv = def_get(G.relocating);
    int spec = mv ? mv->spec : G.placing;
    if (spec < 0) return;
    const DefSpec *s = &def_specs[spec];
    const char *problem = ghost_problem();
    u32 c = problem ? RGBF(1, 0.36f, 0.3f, 1) : s->color;
    float gx = G.ghost_x + (G.ghost_nudge > 0 ? sinf(G.ghost_nudge * 60) * 5 : 0), gy = G.ghost_y;
    float cx = SX(gx), cy = SY(gy), px = maxf(V->tac ? 6 : 8, 26 * V->s);
    gx_circle(cx, cy, s->engage_r * V->s, gx_alpha(c, 0.06f));
    wring(gx, gy, s->engage_r, 1.5f, gx_alpha(c, 0.7f), false);
    gx_rect(cx - px / 2, cy - px / 2, px, px, gx_alpha(c, 0.25f));
    gx_frame(cx - px / 2, cy - px / 2, px, px, 1, c);
    gx_rect(cx - px, cy, px * 2, 1, gx_alpha(c, 0.5f));
    gx_rect(cx, cy - px, 1, px * 2, gx_alpha(c, 0.5f));
    if (!V->tac) {
        char t[40];
        if (problem) snprintf(t, sizeof t, "%s", problem);
        else if (mv) snprintf(t, sizeof t, "Move here");
        else if (creative()) snprintf(t, sizeof t, "%s  Free", s->tag);
        else snprintf(t, sizeof t, "%s  $%d", s->tag, s->cost);
        gx_text_c(F_S, cx, cy - px / 2 - 13, c, t);
    }
}

void world_draw(const View *v) {
    V = v;
    draw_terrain();
    draw_sensors();
    draw_enemy_bases();
    draw_sites();
    draw_defenses();
    draw_placement();
    draw_air();
    draw_fx();
    gx_dx = 0;
}

/* ---------- Top screen ---------- */

static void hud(float par) {
    char t[64];
    /* aiming mark on the ground at the centre of the view (A selects what is under it) */
    gx_dx = par * 1.0f;
    if (!G.over && !is_placing()) {
        gx_rect(194, 120, 5, 1, RGBA(255, 255, 255, 90)); gx_rect(202, 120, 5, 1, RGBA(255, 255, 255, 90));
        gx_rect(200, 114, 1, 5, RGBA(255, 255, 255, 90)); gx_rect(200, 122, 1, 5, RGBA(255, 255, 255, 90));
    }
    gx_dx = par * -0.25f;
    gx_rect(-8, 0, 416, 17, RGBA(8, 12, 10, 190));
    gx_rect(-8, 17, 416, 1, RGBA(120, 140, 110, 90));
    /* level and XP */
    snprintf(t, sizeof t, "LV %d", G.level);
    int w = gx_text(F_M, 5, 2, RGBA(235, 240, 220, 255), t);
    if (!creative()) {
        gx_rect(9.0f + w, 6, 34, 5, RGBA(40, 48, 40, 255));
        gx_rect(9.0f + w, 6, 34 * clampf((float)G.xp / xp_to_next(), 0, 1), 5, RGBA(140, 200, 255, 255));
    }
    /* money */
    if (creative()) gx_text(F_M, 86, 2, RGBA(245, 200, 100, 255), "CREATIVE");
    else {
        fmt_money(t, sizeof t, G.money);
        w = gx_text(F_M, 86, 2, RGBA(245, 200, 100, 255), t);
        snprintf(t, sizeof t, "+%d/s", income_per_second());
        gx_text(F_S, 90.0f + w, 4, RGBA(170, 190, 150, 255), t);
    }
    /* HQ */
    float f = G.sites[0].max_hp > 0 ? G.sites[0].hp / G.sites[0].max_hp : 0;
    gx_text(F_S, 196, 4, RGBA(200, 210, 180, 255), "HQ");
    gx_rect(212, 5, 82, 7, RGBA(40, 44, 40, 255));
    gx_rect(213, 6, 80 * clampf(f, 0, 1), 5, hp_color(f));
    /* hostiles and speed */
    snprintf(t, sizeof t, "HOSTILES %d", G.hostiles);
    gx_text(F_S, 302, 4, G.hostiles ? RGBA(255, 120, 100, 255) : RGBA(150, 165, 140, 255), t);
    gx_text_r(F_M, 396, 2, G.fast ? RGBA(255, 200, 90, 255) : RGBA(150, 165, 140, 255), G.fast ? "2x" : "1x");

    /* newest messages */
    int shown = 0;
    for (int i = 0; i < 3; i++) {
        const LogEntry *e = log_at(i);
        if (!e) break;
        float age = G.real_time - e->time;
        if (age > 7) break;
        shown++;
    }
    for (int i = 0; i < shown; i++) {
        const LogEntry *e = log_at(i);
        float age = G.real_time - e->time, a = clampf((7 - age) / 1.5f, 0, 1) * (i == 0 ? 1 : 0.75f);
        float y = 226.0f - i * 12;
        static const u32 kc[3] = {RGBA(150, 235, 150, 255), RGBA(255, 130, 110, 255), RGBA(210, 215, 200, 255)};
        char line[70];
        snprintf(line, sizeof line, "%.66s", e->text);
        while (gx_textw(F_S, line) > 388 && strlen(line) > 4) line[strlen(line) - 1] = 0;
        gx_rect(2, y - 1, gx_textw(F_S, line) + 8.0f, 12, RGBA(8, 12, 10, (int)(150 * a)));
        gx_text(F_S, 6, y, gx_alpha(kc[e->kind], a), line);
    }
    /* placement prompt */
    if (is_placing() || G.sp_placing) {
        const char *msg = G.sp_placing ? "Tap the lower map to drop the formation   B: cancel"
                        : "Touch map or D-Pad: aim    A: confirm    B: cancel";
        float w2 = gx_textw(F_S, msg) + 12.0f;
        gx_rect(200 - w2 / 2, 21, w2, 14, RGBA(8, 12, 10, 200));
        gx_text_c(F_S, 200, 23, RGBA(245, 220, 150, 255), msg);
    }
    gx_dx = par * -0.7f;
    if (G.over) {
        gx_rect(-8, 84, 416, 62, RGBA(30, 6, 4, 215));
        gx_text_c(F_L, 200, 90, RGBA(255, 110, 90, 255), "FOB ANVIL HAS FALLEN");
        snprintf(t, sizeof t, "Threat level %d   %d kills   %d intercepts", G.level, G.kills, G.intercepts);
        gx_text_c(F_M, 200, 122, RGBA(240, 225, 210, 255), t);
    } else if (G.paused) {
        gx_rect(140, 100, 120, 34, RGBA(8, 12, 10, 215));
        gx_frame(140, 100, 120, 34, 1, RGBA(200, 210, 180, 160));
        gx_text_c(F_L, 200, 105, RGBA(235, 240, 220, 255), "PAUSED");
    }
    gx_dx = 0;
}

static void title_top(float par) {
    /* deep background grid */
    gx_dx = par * 1.6f;
    for (int x = -20; x <= 420; x += 20) gx_rect((float)x, 0, 1, 240, RGBA(110, 140, 110, 22));
    for (int y = 0; y <= 240; y += 20) gx_rect(-20, (float)y, 440, 1, RGBA(110, 140, 110, 22));
    /* radar scope */
    gx_dx = par * 0.9f;
    float cx = 200, cy = 132, a = A.t * 1.6f;
    for (int k = 1; k <= 4; k++) gx_ring(cx, cy, k * 26.0f, 1, RGBA(90, 200, 150, k == 4 ? 150 : 70), false);
    gx_rect(cx - 104, cy, 208, 1, RGBA(90, 200, 150, 50));
    gx_rect(cx, cy - 104, 1, 208, RGBA(90, 200, 150, 50));
    for (int k = 0; k < 14; k++) {
        float b = a - k * 0.045f;
        gx_line(cx, cy, cx + cosf(b) * 104, cy - sinf(b) * 104, 2.5f, RGBA(110, 255, 180, 150 - k * 10));
    }
    gx_dx = par * 0.4f;
    static const float blip[6][2] = {{0.6f, 70}, {1.9f, 88}, {2.5f, 52}, {3.7f, 95}, {4.6f, 61}, {5.5f, 80}};
    for (int k = 0; k < 6; k++) {
        float since = fmodf(a - blip[k][0] + 20 * PI, 2 * PI) / (2 * PI);
        float bx = cx + cosf(blip[k][0]) * blip[k][1], by = cy - sinf(blip[k][0]) * blip[k][1];
        gx_shape(k % 3 == 0 ? SH_BOMBER : SH_FIGHTER, bx, by, 7, blip[k][0] + PI, gx_alpha(RGBA(255, 110, 95, 255), 1 - since), 0);
    }
    /* title */
    gx_dx = par * -1.0f;
    gx_rect(-10, 22, 420, 50, RGBA(6, 10, 8, 170));
    gx_text_c(F_L, 201, 27, RGBA(0, 0, 0, 160), "AIR DEFENSE COMMAND");
    gx_text_c(F_L, 200, 26, RGBA(240, 245, 225, 255), "AIR DEFENSE COMMAND");
    gx_text_c(F_S, 200, 54, RGBA(150, 220, 175, 255), "Hold the line. Own the sky.");
    gx_dx = par * -0.3f;
    gx_text_c(F_S, 200, 222, RGBA(150, 165, 140, 255), A.is_new ? "New Nintendo 3DS enhanced  -  slide up the 3D" : "Nintendo 3DS  -  slide up the 3D");
    gx_dx = 0;
}

void top_draw(int eye) {
    static const float strength[3] = {3.0f, 5.0f, 7.5f};
    float par = eye * A.slider * strength[A.depth];
    if (G.phase != PH_PLAY) { title_top(par); return; }
    View v = {0, 0, 400, 240, A.cam_x, A.cam_y, A.zoom, par, false};
    world_draw(&v);
    hud(par);
}
