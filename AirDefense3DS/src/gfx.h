/* Drawing helpers built on the platform's triangles, rectangles and textured quads. */
#ifndef GFX_H
#define GFX_H
#include "plat.h"

enum { F_S, F_M, F_L };

/* Added to every x coordinate: the stereo parallax of whatever is being drawn. */
extern float gx_dx;

void gx_init(void);
u32 gx_alpha(u32 color, float a);
u32 gx_mix(u32 a, u32 b, float t);
void gx_rect(float x, float y, float w, float h, u32 c);
void gx_frame(float x, float y, float w, float h, float th, u32 c);
void gx_panel(float x, float y, float w, float h, u32 fill, u32 edge);
void gx_line(float x0, float y0, float x1, float y1, float th, u32 c);
void gx_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c);
void gx_circle(float cx, float cy, float r, u32 c);
void gx_ring(float cx, float cy, float r, float th, u32 c, bool dashed);
void gx_rrect(float cx, float cy, float w, float h, float ang, u32 c);
/* Airframe silhouette. `ang` is the screen-space rotation of the nose (0 = right, PI/2 = up). */
void gx_shape(int shape, float cx, float cy, float size, float ang, u32 fill, u32 stroke);

int gx_textw(int font, const char *s);
int gx_lineh(int font);
int gx_text(int font, float x, float y, u32 c, const char *s);
void gx_text_c(int font, float cx, float y, u32 c, const char *s);
void gx_text_r(int font, float rx, float y, u32 c, const char *s);
void gx_textf(int font, float x, float y, u32 c, const char *fmt, ...);
/* Word-wrapped text; returns the number of lines drawn (or needed, if c == 0). */
int gx_wrap(int font, float x, float y, float w, u32 c, const char *s, int max_lines);
void gx_photo(int index, float x, float y, float w, float h, float alpha);
#endif
