#include "treint.h"

static int tre_point_bridge_get_next_char(tre_char_t *c, unsigned int *pos_add, void *context) {
/*	struct augmented_lpoint_t *point = (struct augmented_lpoint_t *)context;

	if ((point->offset + point->start_glyph) >= point->line->cap) {
		*c = 0;
		*pos_add = 0;
		return -1;
	}

	*c = point->line->glyph_info[point->start_glyph + point->offset].code;
	*pos_add = 1;
	++(point->offset);
	return 0;*/
	return -1;
}

static void tre_point_bridge_rewind(size_t pos, void *context) {
	struct augmented_lpoint_t *point = (struct augmented_lpoint_t *)context;
	point->offset = pos;
}

static int tre_point_bridge_compare(size_t pos, size_t pos2, size_t len, void *context) {
/*	struct augmented_lpoint_t *point = (struct augmented_lpoint_t *)context;

	for (int i = 0; i < len; ++i) {
		if (point->start_glyph + pos + i >= point->line->cap) return -1;
		if (point->start_glyph + pos2 + i >= point->line->cap) return -1;
		if (point->line->glyph_info[point->start_glyph + pos + i].code == point->line->glyph_info[point->start_glyph + pos2 + i].code) {
			return -1;
		}
	}
	return 0;*/
	return -1;
}

void tre_bridge_init(struct augmented_lpoint_t *point, tre_str_source *tss) {
	tss->context = (void *)point;
	tss->rewind = tre_point_bridge_rewind;
	tss->compare = tre_point_bridge_compare;
	tss->get_next_char = tre_point_bridge_get_next_char;
}
