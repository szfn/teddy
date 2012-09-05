#ifndef __POINT_H__
#define __POINT_H__

#include <stdbool.h>

typedef struct _point_t {
	int lineno;
	int glyph;
} point_t;

struct _real_line_t;

typedef struct _lpoint_t {
	struct _real_line_t *line;
	int glyph;
} lpoint_t;

#define LPOINTGI(x) ((x).line->glyph_info[(x).glyph])
#define LPOINTG(x) ((x).line->glyphs[(x).glyph])

// makes dst be the same point as src
void copy_lpoint(lpoint_t *dst, lpoint_t *src);

// copies lpoint into a point
void freeze_point(point_t *dst, lpoint_t *src);

bool points_equal(point_t *a, point_t *b);

bool before_lpoint(lpoint_t *a, lpoint_t *b);

#endif
