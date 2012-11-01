#ifndef __TREINT__
#define __TREINT__

#include <tre/tre.h>
#include <stdlib.h>

#include "buffer.h"

struct augmented_lpoint_t {
	buffer_t *buffer;
	int start_glyph;
	int offset;
};

void tre_bridge_init(struct augmented_lpoint_t *point, tre_str_source *tss);


#endif