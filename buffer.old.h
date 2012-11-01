#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H




#include "point.h"




typedef struct _real_line_t {
	my_glyph_info_t *glyph_info;
	int allocated;
	int cap;
	int lineno; // real line number
	double start_y;
	double end_y;
	double y_increment;
	uint16_t lexy_state_start, lexy_state_end;
	struct _real_line_t *prev;
	struct _real_line_t *next;
} real_line_t;

enum tab_mode {
	TAB_FIXED = 0,
	TAB_MODN = 1,
};





























#endif
