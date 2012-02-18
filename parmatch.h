#ifndef __PARMATCH_H__
#define __PARMATCH_H__

#include "point.h"

typedef struct _parmatch_t {
	lpoint_t cursor_cache;
	lpoint_t matched;
} parmatch_t;

void parmatch_init(parmatch_t *parmatch);
void parmatch_find(parmatch_t *parmatch, lpoint_t *cursor);
void parmatch_invalidate(parmatch_t *parmatch);

#endif