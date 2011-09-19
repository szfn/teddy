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

// makes dst be the same point as src
void copy_lpoint(lpoint_t *dst, lpoint_t *src);

// copies lpoint into a point
void freeze_point(point_t *dst, lpoint_t *src);

bool inbetween_lpoint(lpoint_t *start, lpoint_t *x, lpoint_t *end);

#endif