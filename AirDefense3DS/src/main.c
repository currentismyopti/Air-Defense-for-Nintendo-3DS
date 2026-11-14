/* Main loop: input, camera, simulation step, then both screens. */
#include "app.h"

App A;
static bool quit;

float zoom_min(void) { return 0.25f; }

void cam_clamp(void) {
    float hw = 200 / A.zoom, hh = 120 / A.zoom;
    A.cam_x = hw * 2 >= WORLD_W ? WORLD_W / 2 : (A.cam_x < hw ? hw : (A.cam_x > WORLD_W - hw ? WORLD_W - hw : A.cam_x));
    A.cam_y = hh * 2 >= WORLD_H ? WORLD_H / 2 : (A.cam_y < hh ? hh : (A.cam_y > WORLD_H - hh ? WORLD_H - hh : A.cam_y));
}
void cam_focus(float x, float y) {
    A.cam_x = x;
    A.cam_y = y;
    cam_clamp();
}

static void play_input(float dt) {
    const Pad *p = &A.pad;
    if (p->down & K_START) {
        if (!G.over) { A.pause_menu = !A.pause_menu; G.paused = A.pause_menu; }
        return;
    }
    if (G.over || A.pause_menu) return;
    if (p->down & K_X) G.fast = !G.fast;
    if (p->down & K_Y) cam_focus(site_specs[0].x, site_specs[0].y + D(180));
    if (p->down & K_SELECT) A.map_local = !A.map_local;
    if (p->down & K_ZR) A.tab = (A.tab + 1) % N_TAB;
    if (p->down & K_ZL) A.tab = (A.tab + N_TAB - 1) % N_TAB;

    /* zoom */
    float z = 0;
    if (p->held & K_R) z += 1;
    if (p->held & K_L) z -= 1;
    if (z != 0) {
        A.zoom *= expf(z * dt * 1.4f);
        if (A.zoom < zoom_min()) A.zoom = zoom_min();
        if (A.zoom > 1.0f) A.zoom = 1.0f;
    }
    /* camera */
    float mx = p->cx * 300 + p->sx * 700, my = p->cy * 300 + p->sy * 700;
    bool placing = is_placing();
    static float repeat;
    u32 dirs = K_UP | K_DOWN | K_LEFT | K_RIGHT;
    if (placing) {
        /* D-Pad nudges the unit being placed: one step per press, then a steady crawl while held. */
        float step = 0;
        if (p->down & dirs) { step = 4; repeat = 0.3f; }
        else if (p->held & dirs) { repeat -= dt; if (repeat <= 0) step = 90 * dt; }
        if (step > 0) {
            if (p->held & K_LEFT) G.ghost_x -= step;
            if (p->held & K_RIGHT) G.ghost_x += step;
            if (p->held & K_UP) G.ghost_y += step;
            if (p->held & K_DOWN) G.ghost_y -= step;
            float hw = 170 / A.zoom, hh = 90 / A.zoom;
            if (fabsf(G.ghost_x - A.cam_x) > hw || fabsf(G.ghost_y - A.cam_y) > hh) cam_focus(G.ghost_x, G.ghost_y);
        }
    } else {
        if (p->held & K_LEFT) mx -= 220;
        if (p->held & K_RIGHT) mx += 220;
        if (p->held & K_UP) my += 220;
        if (p->held & K_DOWN) my -= 220;
    }
    if (mx != 0 || my != 0) {
        A.cam_x += mx * dt / A.zoom;
        A.cam_y += my * dt / A.zoom;
    }
    cam_clamp();

    if (p->down & K_A) {
        if (placing) confirm_placement();
        else if (!G.sp_placing) select_at(A.cam_x, A.cam_y, 10 / A.zoom);
    }
    if (p->down & K_B) {
        if (G.sp_placing) G.sp_placing = false;
        else if (placing) cancel_placement();
        else deselect_all();
    }
}

static void menu_input(void) {
    const Pad *p = &A.pad;
    if (p->down & K_LEFT) A.menu_diff ^= 1;
    if (p->down & K_RIGHT) A.menu_diff ^= 1;
    if (p->down & (K_UP | K_DOWN)) A.menu_diff ^= 2;
    if (p->down & K_A) { game_start(A.menu_diff); ui_reset(); cam_focus(WORLD_W / 2, D(300)); }
    if (p->down & K_START) quit = true;
}

int main(void) {
    plat_init();
    catalog_init();
    gx_init();
    A.zoom = 0.5f;
    A.depth = 1;
    A.menu_diff = DF_MEDIUM;
    A.is_new = plat_new3ds();
    G.phase = PH_MENU;
    cam_focus(WORLD_W / 2, D(300));
    const u32 bg = RGBA(18, 22, 19, 255);
    while (!quit && plat_loop()) {
        float dt = plat_dt();
        plat_poll(&A.pad);
        A.t += dt;
        A.slider = plat_slider();
        if (G.phase == PH_PLAY) { play_input(dt); sim_update(dt); }
        else menu_input();

        plat_begin();
        u32 top_bg = G.phase == PH_PLAY ? bg : RGBA(8, 12, 10, 255);
        if (A.slider > 0.02f) {
            plat_target(T_TOP_LEFT, top_bg);
            top_draw(-1);
            plat_target(T_TOP_RIGHT, top_bg);
            top_draw(1);
        } else {
            plat_target(T_TOP_LEFT, top_bg);
            top_draw(0);
        }
        plat_target(T_BOTTOM, RGBA(14, 18, 16, 255));
        ui_bottom();
        plat_end();
    }
    plat_exit();
    return 0;
}
