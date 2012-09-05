#include "point.h"
#include "buffer.h"

void copy_lpoint(lpoint_t *dst, lpoint_t *src) {
	dst->line = src->line;
	dst->glyph = src->glyph;
}

void freeze_point(point_t *dst, lpoint_t *src) {
	dst->lineno = src->line->lineno;
	dst->glyph = src->glyph;
}

bool points_equal(point_t *a, point_t *b) {
	return (a->lineno == b->lineno) && (a->glyph == b->glyph);
}

bool before_lpoint(lpoint_t *a, lpoint_t *b) {
	if (a->line->lineno == b->line->lineno)
		return a->glyph < b->glyph;
	else
		return a->line->lineno < b->line->lineno;
}