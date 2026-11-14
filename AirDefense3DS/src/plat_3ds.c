/* Nintendo 3DS backend: citro2d rendering on both screens (left and right eye on top), HID input,
   and the New 3DS extras (804 MHz mode, C-Stick, ZL/ZR). */
#include <3ds.h>
#include <citro2d.h>
#include <stdlib.h>
#include "plat.h"
#include "assets.h"

#define DEPTH 0.5f

static C3D_RenderTarget *targets[3];
static C3D_Tex textures[2];
static bool is_new;
static u64 last_tick;

/* GPU textures are stored as 8x8 tiles in Morton (Z) order, with RGBA8 texels laid out A,B,G,R. */
static u32 tile_offset(u32 x, u32 y) {
    static const u8 spread[8] = {0, 1, 4, 5, 16, 17, 20, 21};
    return (((y >> 3) * (256 >> 3) + (x >> 3)) << 6) + (spread[x & 7] | (spread[y & 7] << 1));
}

static void make_textures(void) {
    C3D_TexInit(&textures[TEX_FONT], 256, 256, GPU_RGBA8);
    C3D_TexInit(&textures[TEX_PHOTOS], 256, 256, GPU_RGBA8);
    u8 *font = (u8 *)textures[TEX_FONT].data, *photo = (u8 *)textures[TEX_PHOTOS].data;
    for (u32 y = 0; y < 256; y++)
        for (u32 x = 0; x < 256; x++) {
            u32 o = tile_offset(x, y) * 4, i = y * 256 + x;
            font[o + 0] = font_alpha[i];
            font[o + 1] = font[o + 2] = font[o + 3] = 255;
            photo[o + 0] = 255;
            photo[o + 1] = photo_rgb[i * 3 + 2];
            photo[o + 2] = photo_rgb[i * 3 + 1];
            photo[o + 3] = photo_rgb[i * 3 + 0];
        }
    C3D_TexFlush(&textures[TEX_FONT]);
    C3D_TexFlush(&textures[TEX_PHOTOS]);
    /* Text is drawn 1:1 on whole pixels, so point sampling keeps it crisp. */
    C3D_TexSetFilter(&textures[TEX_FONT], GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&textures[TEX_PHOTOS], GPU_LINEAR, GPU_LINEAR);
}

void plat_init(void) {
    gfxInitDefault();
    gfxSet3D(true);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
    C2D_Init(16384);
    C2D_Prepare();
    targets[T_TOP_LEFT] = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    targets[T_TOP_RIGHT] = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    targets[T_BOTTOM] = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    make_textures();
    APT_CheckNew3DS(&is_new);
    /* New 3DS: run the CPU at 804 MHz with the L2 cache on. Ignored on older systems. */
    osSetSpeedupEnable(true);
    rng_seed((u32)osGetTime() ^ (u32)svcGetSystemTick());
    last_tick = svcGetSystemTick();
}

void plat_exit(void) {
    C3D_TexDelete(&textures[TEX_FONT]);
    C3D_TexDelete(&textures[TEX_PHOTOS]);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

bool plat_loop(void) { return aptMainLoop(); }
bool plat_new3ds(void) { return is_new; }
float plat_slider(void) { return osGet3DSliderState(); }

float plat_dt(void) {
    u64 now = svcGetSystemTick();
    float dt = (float)((double)(now - last_tick) / (double)SYSCLOCK_ARM11);
    last_tick = now;
    if (dt < 0.001f) dt = 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    return dt;
}

static float stick(s16 v, float range) {
    float f = v / range;
    if (f > -0.18f && f < 0.18f) return 0;
    return f < -1 ? -1 : (f > 1 ? 1 : f);
}

static u32 map_keys(u32 k) {
    u32 o = 0;
    if (k & KEY_A) o |= K_A;
    if (k & KEY_B) o |= K_B;
    if (k & KEY_X) o |= K_X;
    if (k & KEY_Y) o |= K_Y;
    if (k & KEY_L) o |= K_L;
    if (k & KEY_R) o |= K_R;
    if (k & KEY_ZL) o |= K_ZL;
    if (k & KEY_ZR) o |= K_ZR;
    if (k & KEY_START) o |= K_START;
    if (k & KEY_SELECT) o |= K_SELECT;
    if (k & KEY_DUP) o |= K_UP;
    if (k & KEY_DDOWN) o |= K_DOWN;
    if (k & KEY_DLEFT) o |= K_LEFT;
    if (k & KEY_DRIGHT) o |= K_RIGHT;
    return o;
}

void plat_poll(Pad *p) {
    hidScanInput();
    u32 held = hidKeysHeld();
    p->down = map_keys(hidKeysDown());
    p->held = map_keys(held);
    circlePosition c;
    hidCircleRead(&c);
    p->cx = stick(c.dx, 150.0f);
    p->cy = stick(c.dy, 150.0f);
    p->sx = p->sy = 0;
    if (is_new) {
        circlePosition s;
        hidCstickRead(&s);
        p->sx = stick(s.dx, 140.0f);
        p->sy = stick(s.dy, 140.0f);
    }
    p->touch = (held & KEY_TOUCH) != 0;
    if (p->touch) {
        touchPosition t;
        hidTouchRead(&t);
        p->tx = t.px;
        p->ty = t.py;
    }
}

void plat_begin(void) { C3D_FrameBegin(C3D_FRAME_SYNCDRAW); }
void plat_target(int t, u32 clear) {
    C2D_TargetClear(targets[t], clear);
    C2D_SceneBegin(targets[t]);
}
void plat_end(void) { C3D_FrameEnd(0); }

void plat_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, DEPTH);
}
void plat_rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, DEPTH, w, h, c); }

void plat_image(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 c) {
    /* Row 0 of the image sits at v = 1 (see tile_offset), hence the flipped v coordinates. */
    Tex3DS_SubTexture sub = {(u16)w, (u16)h, u0, 1.0f - v0, u1, 1.0f - v1};
    C2D_Image img = {&textures[tex], &sub};
    C2D_DrawParams params = {{x, y, w, h}, {0, 0}, DEPTH, 0};
    C2D_ImageTint tint;
    if (tex == TEX_FONT) C2D_PlainImageTint(&tint, c, 1.0f);
    else C2D_AlphaImageTint(&tint, (c >> 24) / 255.0f);
    C2D_DrawImage(img, &params, &tint);
}
