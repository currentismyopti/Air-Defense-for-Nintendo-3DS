/* PC test backend: software-renders both screens and plays back a script of inputs.
   Script commands (one per line):  wait N | tap X Y | drag X0 Y0 X1 Y1 N | key NAME | hold NAME N |
   pad CX CY N | slider V | money N | level N | seed N | shot NAME | end */
#include "../src/plat.h"
#include "../src/assets.h"
#include <stdlib.h>
#include <time.h>

static u8 fb[3][240][400][3];
static int fbw[3] = {400, 400, 320};
static int cur;
static FILE *script;
static const char *outdir = ".";
static float slider;
static Pad pad;
static u32 hold_keys;
static int wait_frames, hold_frames, drag_frames, drag_total, tap_frames;
static float dx0, dy0, dx1, dy1, pcx, pcy;
static int pad_frames;
static char shot_name[64];
static long frames, tris;

static u32 key_by_name(const char *n) {
    static const struct { const char *n; u32 k; } m[] = {{"A", K_A}, {"B", K_B}, {"X", K_X}, {"Y", K_Y}, {"L", K_L}, {"R", K_R},
        {"ZL", K_ZL}, {"ZR", K_ZR}, {"START", K_START}, {"SELECT", K_SELECT}, {"UP", K_UP}, {"DOWN", K_DOWN}, {"LEFT", K_LEFT}, {"RIGHT", K_RIGHT}};
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++) if (!strcmp(n, m[i].n)) return m[i].k;
    fprintf(stderr, "unknown key %s\n", n);
    return 0;
}

void plat_init(void) {
    const char *path = getenv("ADC_SCRIPT");
    script = path ? fopen(path, "r") : stdin;
    if (getenv("ADC_OUT")) outdir = getenv("ADC_OUT");
    rng_seed(4242);
}
void plat_exit(void) { fprintf(stderr, "frames %ld, peak tris/frame tracked below\n", frames); }

static long peak_tris;
bool plat_loop(void) {
    pad.down = 0;
    if (tris > peak_tris) peak_tris = tris;
    tris = 0;
    frames++;
    if (tap_frames > 0) { if (--tap_frames == 0) pad.touch = false; return true; }
    if (wait_frames > 0) { wait_frames--; return true; }
    if (hold_frames > 0) { if (--hold_frames == 0) pad.held &= ~hold_keys; return true; }
    if (pad_frames > 0) { if (--pad_frames == 0) pad.cx = pad.cy = 0; return true; }
    if (drag_frames > 0) {
        drag_frames--;
        float f = 1 - (float)drag_frames / drag_total;
        pad.tx = (int)(dx0 + (dx1 - dx0) * f); pad.ty = (int)(dy0 + (dy1 - dy0) * f);
        if (drag_frames == 0) { tap_frames = 1; }
        return true;
    }
    char line[256], cmd[32], arg[64];
    while (script && fgets(line, sizeof line, script)) {
        if (sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "wait")) { sscanf(line, "%*s %d", &wait_frames); return true; }
        if (!strcmp(cmd, "tap")) { sscanf(line, "%*s %d %d", &pad.tx, &pad.ty); pad.touch = true; tap_frames = 2; return true; }
        if (!strcmp(cmd, "drag")) {
            sscanf(line, "%*s %f %f %f %f %d", &dx0, &dy0, &dx1, &dy1, &drag_total);
            drag_frames = drag_total; pad.tx = (int)dx0; pad.ty = (int)dy0; pad.touch = true; return true;
        }
        if (!strcmp(cmd, "key")) { sscanf(line, "%*s %63s", arg); pad.down = key_by_name(arg); return true; }
        if (!strcmp(cmd, "hold")) { sscanf(line, "%*s %63s %d", arg, &hold_frames); hold_keys = key_by_name(arg); pad.held |= hold_keys; return true; }
        if (!strcmp(cmd, "pad")) { sscanf(line, "%*s %f %f %d", &pcx, &pcy, &pad_frames); pad.cx = pcx; pad.cy = pcy; return true; }
        if (!strcmp(cmd, "slider")) sscanf(line, "%*s %f", &slider);
        else if (!strcmp(cmd, "money")) sscanf(line, "%*s %d", &G.money);
        else if (!strcmp(cmd, "level")) sscanf(line, "%*s %d", &G.level);
        else if (!strcmp(cmd, "seed")) { int s; sscanf(line, "%*s %d", &s); rng_seed((u32)s); }
        else if (!strcmp(cmd, "auto")) {
            /* test helper: build every unlocked base, ring the HQ with weapons, fill the hangars */
            for (int i = 1; i < N_SITE; i++) build_site(i);
            for (int k = 0; k < N_SHOP_DEF; k++)
                for (int n = 0; n < 3; n++) {
                    if (!begin_placement(k)) continue;
                    float a = (k * 3 + n) * 0.262f, r = 110 + k * 22;
                    G.ghost_x = site_specs[0].x + cosf(a) * r * 1.6f; G.ghost_y = site_specs[0].y + 70 + sinf(a) * r;
                    if (!confirm_placement()) cancel_placement();
                }
            for (int n = 0; n < 12; n++) buy_jet(n % N_JET);
            for (int u = 0; u < N_UPG; u++) buy_upgrade(u);
        }
        else if (!strcmp(cmd, "shot")) { sscanf(line, "%*s %63s", shot_name); return true; }
        else if (!strcmp(cmd, "end")) break;
        else fprintf(stderr, "unknown command: %s", line);
    }
    fprintf(stderr, "peak triangles+quads per frame: %ld\n", peak_tris);
    return false;
}
void plat_poll(Pad *p) { *p = pad; }
float plat_slider(void) { return slider; }
bool plat_new3ds(void) { return true; }
float plat_dt(void) { return 1.0f / 60; }
void plat_begin(void) {}
void plat_target(int t, u32 clear) {
    cur = t;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 400; x++) { fb[t][y][x][0] = clear & 255; fb[t][y][x][1] = (clear >> 8) & 255; fb[t][y][x][2] = (clear >> 16) & 255; }
}
static void save(int t, const char *suffix) {
    char path[256];
    snprintf(path, sizeof path, "%s/%s_%s.ppm", outdir, shot_name, suffix);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d 240\n255\n", fbw[t]);
    for (int y = 0; y < 240; y++) fwrite(fb[t][y], 3, (size_t)fbw[t], f);
    fclose(f);
}
void plat_end(void) {
    if (!shot_name[0]) return;
    save(T_TOP_LEFT, "top");
    if (slider > 0.02f) save(T_TOP_RIGHT, "topR");
    save(T_BOTTOM, "bot");
    shot_name[0] = 0;
}
static inline void px(int x, int y, u32 c, float cover) {
    if (x < 0 || y < 0 || x >= fbw[cur] || y >= 240) return;
    float a = (c >> 24) / 255.0f * cover;
    u8 *d = fb[cur][y][x];
    d[0] = (u8)(d[0] + ((int)(c & 255) - d[0]) * a);
    d[1] = (u8)(d[1] + ((int)((c >> 8) & 255) - d[1]) * a);
    d[2] = (u8)(d[2] + ((int)((c >> 16) & 255) - d[2]) * a);
}
static inline float edge(float ax, float ay, float bx, float by, float x, float y) { return (bx - ax) * (y - ay) - (by - ay) * (x - ax); }
static inline bool topleft(float ax, float ay, float bx, float by) { float dx = bx - ax, dy = by - ay; return (dy == 0 && dx > 0) || dy < 0; }
void plat_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
    tris++;
    float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    if (area < 0) { float t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > fbw[cur]) maxx = fbw[cur];
    if (maxy > 240) maxy = 240;
    float b0 = topleft(x1, y1, x2, y2) ? 0 : -1e-4f, b1 = topleft(x2, y2, x0, y0) ? 0 : -1e-4f, b2 = topleft(x0, y0, x1, y1) ? 0 : -1e-4f;
    for (int y = miny; y < maxy; y++)
        for (int x = minx; x < maxx; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            if (edge(x1, y1, x2, y2, fx, fy) + b0 >= 0 && edge(x2, y2, x0, y0, fx, fy) + b1 >= 0 && edge(x0, y0, x1, y1, fx, fy) + b2 >= 0) px(x, y, c, 1);
        }
}
void plat_rect(float x, float y, float w, float h, u32 c) {
    tris++;
    int x0 = (int)floorf(x + 0.5f), y0 = (int)floorf(y + 0.5f), x1 = (int)floorf(x + w + 0.5f), y1 = (int)floorf(y + h + 0.5f);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) px(xx, yy, c, 1);
}
void plat_image(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 c) {
    tris++;
    int x0 = (int)floorf(x + 0.5f), y0 = (int)floorf(y + 0.5f), x1 = (int)floorf(x + w + 0.5f), y1 = (int)floorf(y + h + 0.5f);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            float u = u0 + (u1 - u0) * (xx + 0.5f - x) / w, v = v0 + (v1 - v0) * (yy + 0.5f - y) / h;
            int tx = (int)(u * 256), ty = (int)(v * 256);
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx > 255) tx = 255;
            if (ty > 255) ty = 255;
            if (tex == TEX_FONT) px(xx, yy, c, font_alpha[ty * 256 + tx] / 255.0f);
            else { const u8 *p = &photo_rgb[(ty * 256 + tx) * 3]; px(xx, yy, RGBA(p[0], p[1], p[2], c >> 24), 1); }
        }
}
