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
