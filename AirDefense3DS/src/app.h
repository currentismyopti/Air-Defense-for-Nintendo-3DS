/* App-level state shared by the top-screen view, the bottom-screen UI and the main loop. */
#ifndef APP_H
#define APP_H
#include "gfx.h"

typedef struct { float x0, y0, w, h, cx, cy, s, par; bool tac; } View;

enum { TAB_MAP, TAB_ARMS, TAB_BASES, TAB_AIR, TAB_MORE, N_TAB };

typedef struct {
    float cam_x, cam_y, zoom;   /* top-screen camera: centre in world units, pixels per unit */
    bool map_local;             /* bottom map: false = whole theatre, true = follows the camera */
    int tab, sub[N_TAB];
    int rings;                  /* 0 = range ring of the selected unit only, 1 = all */
    int depth;                  /* 3D strength: 0 low, 1 normal, 2 high */
    bool pause_menu;
    int menu_diff;
    float t;                    /* seconds since launch */
    float slider;
    Pad pad;
    bool is_new;
} App;
extern App A;

void cam_clamp(void);
void cam_focus(float x, float y);
float zoom_min(void);

/* view.c */
void world_draw(const View *v);
void top_draw(int eye);
float view_wx(const View *v, float sx);
float view_wy(const View *v, float sy);
void fmt_money(char *out, size_t n, int v);

/* ui.c */
void ui_bottom(void);
void ui_reset(void);
#endif
