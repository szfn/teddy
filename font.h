#ifndef __TEDDY_FONT_H__
#define __TEDDY_FONT_H__

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>

typedef struct _teddy_font_t {
	FT_Face face;
	cairo_font_face_t *cairoface;
	cairo_scaled_font_t *cairofont;
	cairo_matrix_t font_size_matrix, font_ctm;
	cairo_font_options_t *font_options;
	FT_Face scaled_face;
} teddy_font_t;

typedef struct _teddy_fontset_t {
	teddy_font_t fonts[0xff];
	uint8_t map[0xfffff+1];
	int count;
} teddy_fontset_t;

extern FT_Library library;
extern teddy_font_t main_font;
extern teddy_fontset_t main_fonts;
extern teddy_font_t posbox_font;

void teddy_font_real_init(void);
void teddy_font_real_free(void);

#endif
