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

bool inbetween_lpoint(lpoint *start, lpoint *x, lpoint *end) {
    if (start->line == end->line) {
        return ((x->line == start->line) && (x->glyph >= start.glyph) && (x->glyph <= end.glyph));
    } else {
        if (x->line == start->line) {
            return (x->glyph >= start->glyph);
        } else if (x->line == end->line) {
            return (x->glyph <= end->glyph);
        } else {
            return ((x->line->lineno > start->line->lineno) && (x->line->lineno < end->line->lineno));
        }
    }
}