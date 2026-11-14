#include "gfx.h"
#include "assets.h"

float gx_dx;

/* Each airframe outline triangulated once at start-up (ear clipping). */
static float sh_v[N_SHAPE][20][2];
static int sh_nv[N_SHAPE];
static u8 sh_tri[N_SHAPE][18][3];
static int sh_nt[N_SHAPE];

static float cross2(const float *a, const float *b, const float *c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
static bool in_tri(const float *p, const float *a, const float *b, const float *c) {
    float d1 = cross2(a, b, p), d2 = cross2(b, c, p), d3 = cross2(c, a, p);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

void gx_init(void) {
    for (int s = 0; s < N_SHAPE; s++) {
        int n = 0, np = shape_npts[s];
        for (int i = 0; i < np; i++) { sh_v[s][n][0] = shape_pts[s][i][0]; sh_v[s][n][1] = shape_pts[s][i][1]; n++; }
        for (int i = np - 1; i >= 0; i--) {
            if (shape_pts[s][i][0] == 0) continue;
            sh_v[s][n][0] = -shape_pts[s][i][0]; sh_v[s][n][1] = shape_pts[s][i][1]; n++;
        }
        sh_nv[s] = n;
        float area = 0;
        for (int i = 0; i < n; i++) {
            const float *a = sh_v[s][i], *b = sh_v[s][(i + 1) % n];
            area += a[0] * b[1] - b[0] * a[1];
        }
        float sign = area < 0 ? -1.0f : 1.0f;
        int idx[20], m = n, nt = 0, guard = 0;
        for (int i = 0; i < n; i++) idx[i] = i;
        while (m > 3 && guard++ < 400) {
            bool cut = false;
            for (int i = 0; i < m && !cut; i++) {
                int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
                if (cross2(sh_v[s][ia], sh_v[s][ib], sh_v[s][ic]) * sign <= 1e-6f) continue;
                bool ear = true;
                for (int k = 0; k < m && ear; k++) {
                    int ip = idx[k];
                    if (ip == ia || ip == ib || ip == ic) continue;
                    if (in_tri(sh_v[s][ip], sh_v[s][ia], sh_v[s][ib], sh_v[s][ic])) ear = false;
                }
                if (!ear) continue;
                sh_tri[s][nt][0] = (u8)ia; sh_tri[s][nt][1] = (u8)ib; sh_tri[s][nt][2] = (u8)ic; nt++;
                for (int k = i; k < m - 1; k++) idx[k] = idx[k + 1];
                m--;
                cut = true;
            }
            if (!cut) break;
        }
        if (m == 3) { sh_tri[s][nt][0] = (u8)idx[0]; sh_tri[s][nt][1] = (u8)idx[1]; sh_tri[s][nt][2] = (u8)idx[2]; nt++; }
        sh_nt[s] = nt;
    }
}

u32 gx_alpha(u32 c, float a) {
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    u32 al = (u32)((c >> 24) * a);
    return (c & 0x00FFFFFFu) | (al << 24);
}
u32 gx_mix(u32 a, u32 b, float t) {
    u32 o = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float va = (float)((a >> sh) & 255), vb = (float)((b >> sh) & 255);
        o |= ((u32)(va + (vb - va) * t) & 255) << sh;
    }
    return o;
}

void gx_rect(float x, float y, float w, float h, u32 c) {
    if (w <= 0 || h <= 0 || !(c >> 24)) return;
    plat_rect(x + gx_dx, y, w, h, c);
}
void gx_frame(float x, float y, float w, float h, float th, u32 c) {
    gx_rect(x, y, w, th, c);
    gx_rect(x, y + h - th, w, th, c);
    gx_rect(x, y + th, th, h - 2 * th, c);
    gx_rect(x + w - th, y + th, th, h - 2 * th, c);
}
void gx_panel(float x, float y, float w, float h, u32 fill, u32 edge) {
    gx_rect(x, y, w, h, fill);
    gx_frame(x, y, w, h, 1, edge);
}
void gx_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c) {
    if (!(c >> 24)) return;
    plat_tri(x0 + gx_dx, y0, x1 + gx_dx, y1, x2 + gx_dx, y2, c);
    plat_tri(x0 + gx_dx, y0, x2 + gx_dx, y2, x3 + gx_dx, y3, c);
}
void gx_line(float x0, float y0, float x1, float y1, float th, u32 c) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f || !(c >> 24)) return;
    float nx = -dy / len * th * 0.5f, ny = dx / len * th * 0.5f;
    gx_quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}
void gx_circle(float cx, float cy, float r, u32 c) {
    if (r <= 0 || !(c >> 24)) return;
    if (r < 1.2f) { gx_rect(cx - r, cy - r, r * 2, r * 2, c); return; }
    int n = (int)(r * 0.8f) + 8;
    if (n > 40) n = 40;
    float px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        float a = 2 * PI * i / n, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        plat_tri(cx + gx_dx, cy, px + gx_dx, py, x + gx_dx, y, c);
        px = x; py = y;
    }
}
void gx_ring(float cx, float cy, float r, float th, u32 c, bool dashed) {
    if (r <= 0 || !(c >> 24)) return;
    int n = dashed ? (int)(2 * PI * r / 6) : (int)(r * 0.7f) + 10;
    int cap = dashed ? 120 : 64;
    if (n < 12) n = 12;
    if (n > cap) n = cap;
    n &= ~1;
    float px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        float a = 2 * PI * i / n, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        if (!dashed || (i & 1)) gx_line(px, py, x, y, th, c);
        px = x; py = y;
    }
}
void gx_rrect(float cx, float cy, float w, float h, float ang, u32 c) {
    /* A w-by-h rectangle whose long (h) axis points along `ang`. */
    float ux = cosf(ang) * h * 0.5f, uy = -sinf(ang) * h * 0.5f;
    float vx = sinf(ang) * w * 0.5f, vy = cosf(ang) * w * 0.5f;
    gx_quad(cx + ux + vx, cy + uy + vy, cx + ux - vx, cy + uy - vy, cx - ux - vx, cy - uy - vy, cx - ux + vx, cy - uy + vy, c);
}
void gx_shape(int s, float cx, float cy, float size, float ang, u32 fill, u32 stroke) {
    float a = ang - PI / 2, ca = cosf(a) * size, sa = sinf(a) * size;
    float px[20], py[20];
    int n = sh_nv[s];
    for (int i = 0; i < n; i++) {
        float x = sh_v[s][i][0], y = sh_v[s][i][1];
        px[i] = cx + gx_dx + x * ca - y * sa;
        py[i] = cy - (x * sa + y * ca);
    }
    if (fill >> 24)
        for (int t = 0; t < sh_nt[s]; t++) {
            const u8 *k = sh_tri[s][t];
            plat_tri(px[k[0]], py[k[0]], px[k[1]], py[k[1]], px[k[2]], py[k[2]], fill);
        }
    if (stroke >> 24) {
        float save = gx_dx;
        gx_dx = 0;
        for (int i = 0; i < n; i++) gx_line(px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], 1, stroke);
        gx_dx = save;
    }
}

/* ---------- Text ---------- */

int gx_lineh(int font) { return fonts[font].ascent + fonts[font].descent; }
int gx_textw(int font, const char *s) {
    int w = 0;
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        w += fonts[font].g[ch - 32].adv;
    }
    return w;
}
static int draw_run(int font, float x, float y, u32 c, const char *s, int len) {
    const Font *f = &fonts[font];
    float px = floorf(x + gx_dx + 0.5f), py = floorf(y + 0.5f);
    int w = 0;
    for (int i = 0; i < len && s[i]; i++) {
        int ch = (unsigned char)s[i];
        if (ch < 32 || ch > 126) ch = '?';
        const Glyph *g = &f->g[ch - 32];
        if (g->w)
            plat_image(TEX_FONT, px + w + g->ox, py + g->oy, g->w, g->h, g->x / 256.0f, g->y / 256.0f,
                       (g->x + g->w) / 256.0f, (g->y + g->h) / 256.0f, c);
        w += g->adv;
    }
    return w;
}
int gx_text(int font, float x, float y, u32 c, const char *s) { return draw_run(font, x, y, c, s, 1 << 20); }
void gx_text_c(int font, float cx, float y, u32 c, const char *s) { gx_text(font, cx - gx_textw(font, s) * 0.5f, y, c, s); }
void gx_text_r(int font, float rx, float y, u32 c, const char *s) { gx_text(font, rx - gx_textw(font, s), y, c, s); }
void gx_textf(int font, float x, float y, u32 c, const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    gx_text(font, x, y, c, buf);
}
int gx_wrap(int font, float x, float y, float w, u32 c, const char *s, int max_lines) {
    const Font *f = &fonts[font];
    int lines = 0, lh = gx_lineh(font) + 1;
    while (*s && lines < max_lines) {
        int width = 0, i = 0, last_space = -1;
        while (s[i] && s[i] != '\n') {
            int ch = (unsigned char)s[i];
            if (ch < 32 || ch > 126) ch = '?';
            int adv = f->g[ch - 32].adv;
            if (width + adv > w && i > 0) break;
            if (s[i] == ' ') last_space = i;
            width += adv;
            i++;
        }
        int len = i;
        if (s[i] && s[i] != '\n' && last_space > 0) len = last_space;
        if (c) draw_run(font, x, y + lines * lh, c, s, len);
        lines++;
        s += len;
        while (*s == ' ') s++;
        if (*s == '\n') s++;
    }
    return lines;
}
void gx_photo(int index, float x, float y, float w, float h, float alpha) {
    float u0 = (index % 2) * 0.5f, v0 = (index / 2) * (72.0f / 256.0f);
    plat_image(TEX_PHOTOS, x + gx_dx, y, w, h, u0, v0, u0 + 0.5f, v0 + 72.0f / 256.0f, RGBA(255, 255, 255, (int)(alpha * 255)));
}
