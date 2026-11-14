#ifndef ASSETS_H
#define ASSETS_H
/* Bitmap font atlas (alpha only, 256x256) and aircraft photos (RGB, 256x256 sheet of 128x72 tiles). */
typedef struct { unsigned char x, y, w, h; signed char ox, oy; unsigned char adv; } Glyph;
typedef struct { int size, ascent, descent; const Glyph *g; } Font;
extern const unsigned char font_alpha[256 * 256];
extern const unsigned char photo_rgb[256 * 256 * 3];
extern const Font fonts[3];
#endif
