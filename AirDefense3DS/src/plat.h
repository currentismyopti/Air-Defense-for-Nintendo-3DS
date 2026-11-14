/* The thin layer between the game and the machine. Two implementations:
   plat_3ds.c (citro2d on the console) and host/plat_host.c (software renderer used for testing on a PC). */
#ifndef PLAT_H
#define PLAT_H
#include "game.h"

enum {
    K_A = 1 << 0, K_B = 1 << 1, K_X = 1 << 2, K_Y = 1 << 3, K_L = 1 << 4, K_R = 1 << 5, K_ZL = 1 << 6, K_ZR = 1 << 7,
    K_START = 1 << 8, K_SELECT = 1 << 9, K_UP = 1 << 10, K_DOWN = 1 << 11, K_LEFT = 1 << 12, K_RIGHT = 1 << 13,
};
typedef struct {
    u32 down, held;       /* K_* bits: pressed this frame, currently held */
    float cx, cy;         /* Circle Pad, -1..1, y up */
    float sx, sy;         /* C-Stick (New 3DS), -1..1, y up */
    bool touch;
    int tx, ty;           /* bottom-screen pixel */
} Pad;

enum { T_TOP_LEFT, T_TOP_RIGHT, T_BOTTOM };
enum { TEX_FONT, TEX_PHOTOS };

void plat_init(void);
void plat_exit(void);
bool plat_loop(void);
void plat_poll(Pad *p);
float plat_slider(void);   /* 3D depth slider, 0..1 */
bool plat_new3ds(void);
float plat_dt(void);       /* seconds since the previous frame */

void plat_begin(void);
void plat_target(int target, u32 clear);
void plat_end(void);
void plat_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 color);
void plat_rect(float x, float y, float w, float h, u32 color);
/* Draws part of a 256x256 texture. The font texture takes its colour from `color`; photos only its alpha. */
void plat_image(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color);
#endif
