#include "buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>



#include "global.h"
#include "columns.h"
#include "lexy.h"
#include "foundry.h"
#include "interp.h"
#include "buffers.h"








static void buffer_set_to_real(buffer_t *buffer, lpoint_t *real_point) {
	copy_lpoint(&(buffer->cursor), real_point);

	if (buffer->cursor.glyph < 0) buffer->cursor.glyph = 0;
	if (buffer->cursor.glyph > buffer->cursor.line->cap) buffer->cursor.glyph = buffer->cursor.line->cap;
}

static void grow_line(real_line_t *line, int insertion_point, int size) {
	/*printf("cap: %d allocated: %d\n", line->glyphs_cap, line->allocated_glyphs);*/

	while (line->cap + size >= line->allocated) {
		line->allocated *= 2;
		if (line->allocated == 0) line->allocated = 10;
		/*printf("new size: %d\n", line->allocated_glyphs);*/
		line->glyph_info = realloc(line->glyph_info, sizeof(my_glyph_info_t) * line->allocated);
		alloc_assert(line->glyph_info);
	}

	if (insertion_point < line->cap) {
		/*printf("memmove %x <- %x %d\n", line->glyphs+dst+1, line->glyphs+dst, line->glyphs_cap - dst); */
		memmove(line->glyph_info+insertion_point+size, line->glyph_info+insertion_point, sizeof(my_glyph_info_t)*(line->cap - insertion_point));
		line->cap += size;
	}
}

static void buffer_join_lines(buffer_t *buffer, real_line_t *line1, real_line_t *line2) {
	real_line_t *line_cur;
	int lineno;

	if (line1 == NULL) return;
	if (line2 == NULL) return;

	grow_line(line1, line1->cap, line2->cap);
	memcpy(line1->glyph_info+line1->cap, line2->glyph_info, sizeof(my_glyph_info_t)*line2->cap);
	line1->cap += line2->cap;

	/* remove line2 from real_lines list */

	line1->next = line2->next;
	if (line2->next != NULL) line2->next->prev = line1;

	free(line2->glyph_info);
	free(line2);

	lineno = line1->lineno + 1;

	for (line_cur = line1->next; line_cur != NULL; line_cur = line_cur->next) {
		line_cur->lineno = lineno;
		++lineno;
	}
}

static void buffer_line_delete_from(buffer_t *buffer, real_line_t *real_line, int start, int size) {
	memmove(real_line->glyph_info+start, real_line->glyph_info+start+size, sizeof(my_glyph_info_t)*(real_line->cap-start-size));
	real_line->cap -= size;
}

static void buffer_remove_selection(buffer_t *buffer, lpoint_t *start, lpoint_t *end) {
	real_line_t *real_line;
	int lineno;

	if (start->line == NULL) return;
	if (end->line == NULL) return;

	buffer->modified = 1;

	//printf("Deleting %d from %d (size: %d)\n", start_line->lineno, start_glyph, start_line->cap-start_glyph);

	/* Special case when we are deleting a section of the same line */
	if (start->line == end->line) {
		buffer_line_delete_from(buffer, start->line, start->glyph, end->glyph-start->glyph);
		buffer_set_to_real(buffer, start);
		return;
	}

	/* Remove text from first and last real lines */
	buffer_line_delete_from(buffer, start->line, start->glyph, start->line->cap-start->glyph);
	buffer_line_delete_from(buffer, end->line, 0, end->glyph);

	/* Remove real_lines between start and end */
	for (real_line = start->line->next; (real_line != NULL) && (real_line != end->line); ) {
		real_line_t *next = real_line->next;
		free(real_line->glyph_info);
		free(real_line);
		real_line = next;
	}

	start->line->next = end->line;
	end->line->prev = start->line;

	/* Renumber real_lines */

	lineno = start->line->lineno+1;
	for (real_line = start->line->next; real_line != NULL; real_line = real_line->next) {
		real_line->lineno = lineno;
		++lineno;
	}

	/*
	printf("AFTER FIXING REAL LINES, BEFORE FIXING DISPLAY LINES:\n");
	debug_print_real_lines_state(buffer);
	debug_print_lines_state(buffer);
	*/

	buffer_set_to_real(buffer, start);

	buffer_join_lines(buffer, start->line, end->line);
}

static real_line_t *new_real_line(int lineno) {
	real_line_t *line = malloc(sizeof(real_line_t));
	line->allocated = 10;
	line->glyph_info = malloc(sizeof(my_glyph_info_t) * line->allocated);
	alloc_assert(line->glyph_info);
	line->cap = 0;
	line->prev = NULL;
	line->next = NULL;
	line->lineno = lineno;
	line->y_increment = 0.0;
	line->start_y = 0.0;
	line->lexy_state_start = line->lexy_state_end = LEXY_ROWS;
	return line;
}

static real_line_t *buffer_copy_line(buffer_t *buffer, real_line_t *real_line, int start, int size) {
	real_line_t *r = new_real_line(-1);

	//printf("buffer_copy_line start: %d, cap: %d, size to copy: %d\n", start, real_line->cap, size);

	grow_line(r, 0, size);

	memcpy(r->glyph_info, real_line->glyph_info+start, size * sizeof(my_glyph_info_t));

	r->cap = size;

	return r;
}

static void buffer_real_line_insert(buffer_t *buffer, real_line_t *insertion_line, real_line_t* real_line) {
	real_line_t *cur;
	int lineno;

	//debug_print_real_lines_state(buffer);

	real_line->next = insertion_line->next;
	if (real_line->next != NULL) real_line->next->prev = real_line;
	real_line->prev = insertion_line;
	insertion_line->next = real_line;

	lineno = insertion_line->lineno;

	//printf("Renumbering from %d\n", lineno);

	for (cur = insertion_line; cur != NULL; cur = cur->next) {
		cur->lineno = lineno;
		lineno++;
		//debug_print_real_lines_state(buffer);
	}
}

static void buffer_split_line(buffer_t *buffer, lpoint_t *point) {
	real_line_t *copied_segment = buffer_copy_line(buffer, point->line, point->glyph, point->line->cap - point->glyph);
	buffer_line_delete_from(buffer, point->line, point->glyph, point->line->cap - point->glyph);
	buffer_real_line_insert(buffer, point->line, copied_segment);
}



static int buffer_line_insert_utf8_text(buffer_t *buffer, real_line_t *line, const char *text, int len, int insertion_point, int *valid_chars, int *invalid_chars) {
	FT_UInt previous = 0;
	uint8_t previous_fontidx = 0;
	int src, dst;
	int inserted_glyphs = 0;

	if (insertion_point > 0) {
		previous = line->glyph_info[insertion_point-1].glyph_index;
		previous_fontidx = line->glyph_info[insertion_point-1].fontidx;
	}



	for (src = 0, dst = insertion_point; src < len; ) {
		bool valid = true;
		uint32_t code = utf8_to_utf32(text, &src, len, &valid);

		//printf("First char: %02x\n", (uint8_t)text[src]);

		if ((code < 0x20) && (code != 0x09) && (code != 0x0a) && (code != 0x0d)) {
			valid = false;
		}

		if (valid) *valid_chars = *valid_chars + 1;
		else *invalid_chars = *invalid_chars + 1;



		grow_line(line, dst, 1);


		line->glyph_info[dst].kerning_correction = (previous_fontidx == fontidx) ? fontset_get_kerning(font, fontidx, previous, glyph_index) : 0.0;

		previous = line->glyph_info[dst].glyph_index = glyph_index;
		previous_fontidx = line->glyph_info[dst].fontidx = fontidx;
		line->glyph_info[dst].x = 0.0;
		line->glyph_info[dst].y = 0.0;

		line->glyph_info[dst].x_advance = fontset_x_advance(font, fontidx, glyph_index);
		if (dst == line->cap) {
			++(line->cap);
		}
		++dst;
		++inserted_glyphs;
	}

	if (dst < line->cap) {
		uint8_t fontidx = line->glyph_info[dst].fontidx;
		line->glyph_info[dst].kerning_correction = (previous_fontidx == fontidx) ? fontset_get_kerning(font, fontidx, previous, line->glyph_info[dst].glyph_index) : 0.0;

	}



	return inserted_glyphs;
}

static void buffer_insert_multiline_text(buffer_t *buffer, lpoint_t *start_point, const char *text) {
	lpoint_t point;
	int start = 0;
	int end = 0;
	int valid_chars = 0, invalid_chars = 0;

	copy_lpoint(&point, start_point);

	//printf("Inserting multiline text [[%s]]\n\n", text);

	while (end < strlen(text)) {
		if (text[end] == '\n') {
			//printf("line cap: %d glyph %d\n", line->cap, glyph);
			point.glyph += buffer_line_insert_utf8_text(buffer, point.line, text+start, end-start, point.glyph, &valid_chars, &invalid_chars);
			//printf("    line cap: %d glyph: %d\n", line->cap, glyph);
			buffer_split_line(buffer, &point);

			assert(point.line->next != NULL);
			point.line = point.line->next;
			point.glyph = 0;

			++end;
			start = end;
		} else {
			if (buffer->job != NULL) {
				if (text[end] == 0x08) {
					if (end == 0) {
						// if this the very first character delete one character from the buffer
						if (buffer->cursor.line->cap > 0) {
							--(buffer->cursor.line->cap);
							--(point.glyph);
						}
					} else {
						//printf("(bs) line cap: %d glyph: %d\n", line->cap, glyph);
						point.glyph += buffer_line_insert_utf8_text(buffer, point.line, text+start, end-start-1, point.glyph, &valid_chars, &invalid_chars);
						//printf("    line cap: %d glyph: %d\n", line->cap, glyph);
					}
					++end;
					start = end;
				} else {
					++end;
				}
			} else {
				++end;
			}
		}
	}

	if (start < end) {
		point.glyph += buffer_line_insert_utf8_text(buffer, point.line, text+start, end-start, point.glyph, &valid_chars, &invalid_chars);
		//printf("(end) line cap: %d glyph: %d\n", line->cap, glyph);
	}

	copy_lpoint(&(buffer->cursor), &point);
}



static void buffer_line_adjust_glyphs(buffer_t *buffer, real_line_t *line, double y) {
	int i;
	double y_increment = buffer->line_height;
	double x = buffer->left_margin;
	bool initial_spaces = true;

	line->start_y = y;
	line->end_y = y;

	//buffer_line_fix_spaces(buffer, line);

	//printf("setting type\n");
	for (i = 0; i < line->cap; ++i) {
		if (line->glyph_info[i].code == 0x20) {
			line->glyph_info[i].x_advance = initial_spaces ? buffer->em_advance : buffer->space_advance;
		} else if (line->glyph_info[i].code == 0x09) {
			double size = config_intval(&(buffer->config), CFG_TAB_WIDTH) * buffer->em_advance;
			double to_next_cell = size - fmod(x - buffer->left_margin, size);
			if (to_next_cell <= buffer->space_advance/2) {
				// if it is too small jump to next cell instead
				to_next_cell += size;
			}
			line->glyph_info[i].x_advance = to_next_cell;
		} else {
			initial_spaces = false;
		}

		x += line->glyph_info[i].kerning_correction;
		if (!config_intval(&(buffer->config), CFG_AUTOWRAP)) {
			if (x + buffer->right_margin > buffer->rendered_width) buffer->rendered_width = x + buffer->right_margin;
		} else {
			if (x+line->glyph_info[i].x_advance > buffer->rendered_width - buffer->right_margin) {
				y += buffer->line_height;
				line->end_y = y;
				y_increment += buffer->line_height;
				x = buffer->left_margin;
			}
		}
		line->glyph_info[i].x = x;
		line->glyph_info[i].y = y;
		x += line->glyph_info[i].x_advance;
		//printf("x: %g (%g)\n", x, glyph_info[i].x_advance);
	}

	line->y_increment = y_increment;
}

static void buffer_typeset_from(buffer_t *buffer, real_line_t *start_line) {
	real_line_t *line;
	double y = start_line->start_y;

	for (line = start_line; line != NULL; line = line->next) {
		buffer_line_adjust_glyphs(buffer, line, y);
		y += line->y_increment;
	}
}

void buffer_replace_region(buffer_t *buffer, const char *new_text, lpoint_t *start, lpoint_t *end) {
	lpoint_t mark_save = buffer->mark;
	lpoint_t cursor_save = buffer->cursor;

	buffer->mark = *end;
	buffer->cursor = *start;

	buffer_replace_selection(buffer, new_text);

	buffer->mark = mark_save;
	buffer->cursor = cursor_save;
}

void buffer_replace_selection(buffer_t *buffer, const char *new_text) {
	lpoint_t start_point, end_point;
	real_line_t *pre_start_line;
	undo_node_t *undo_node;

	//printf("buffer_replace_selection (call): %d %d\n", buffer->cursor.line->cap, buffer->cursor.glyph);

	if (!(buffer->editable)) return;

	buffer->modified = 1;

	if (buffer->job == NULL) {
		undo_node = malloc(sizeof(undo_node_t));
		undo_node->tag = NULL;
	}

	buffer_get_selection(buffer, &start_point, &end_point);

	if (buffer->job == NULL)
       freeze_selection(buffer, &(undo_node->before_selection), &start_point, &end_point);

	buffer_remove_selection(buffer, &start_point, &end_point);

	copy_lpoint(&start_point, &(buffer->cursor));
	if (start_point.glyph > 0) {
		pre_start_line = start_point.line;
	} else {
		pre_start_line = start_point.line->prev;
	}

	//printf("buffer_replace_selection: %d %d\n", buffer->cursor_line->cap, buffer->cursor_glyph);
	buffer_insert_multiline_text(buffer, &(buffer->cursor), new_text);

	copy_lpoint(&end_point, &(buffer->cursor));

	if (buffer->job == NULL)
		freeze_selection(buffer, &(undo_node->after_selection), &start_point, &end_point);

	if (buffer->job == NULL)
		undo_push(&(buffer->undo), undo_node);

	buffer_typeset_from(buffer, start_point.line);

	buffer_unset_mark(buffer);

	if (pre_start_line == NULL) {
		lexy_update_starting_at(buffer, buffer->real_line, true);
	} else {
		lexy_update_starting_at(buffer, pre_start_line, true);
	}

	if (buffer->onchange != NULL) buffer->onchange(buffer);
}

static real_line_t *buffer_search_line(buffer_t *buffer, int lineno) {
	real_line_t *real_line;
	for (real_line = buffer->real_line; real_line != NULL; real_line = real_line->next) {
		if (real_line->lineno == lineno) return real_line;
	}
	return real_line;
}

static void buffer_thaw_selection(buffer_t *buffer, selection_t *selection, lpoint_t *start, lpoint_t *end) {
	start->line = buffer_search_line(buffer, selection->start.lineno);
	start->glyph = selection->start.glyph;

	end->line = buffer_search_line(buffer, selection->end.lineno);
	end->glyph = selection->end.glyph;
}

void buffer_undo(buffer_t *buffer, bool redo) {
	lpoint_t start_point, end_point;
	real_line_t *typeset_start_line, *pre_start_line = NULL;
	undo_node_t *undo_node;

	if (!(buffer->editable)) return;
	if (buffer->job != NULL) return;

	undo_node = redo ? undo_redo_pop(&(buffer->undo)) : undo_pop(&(buffer->undo));

	if (undo_node == NULL) return;

	buffer->modified = 1;

	buffer_unset_mark(buffer);

	if (!redo) {
		buffer_thaw_selection(buffer, &(undo_node->after_selection), &start_point, &end_point);
	} else {
		buffer_thaw_selection(buffer, &(undo_node->before_selection), &start_point, &end_point);
	}

	buffer_remove_selection(buffer, &start_point, &end_point);

	typeset_start_line = buffer->cursor.line;
	if (buffer->cursor.glyph > 0) {
		pre_start_line = typeset_start_line;
	} else {
		pre_start_line = typeset_start_line->prev;
	}

	if (!redo) {
		buffer_insert_multiline_text(buffer, &(buffer->cursor), undo_node->before_selection.text);
	} else {
		buffer_insert_multiline_text(buffer, &(buffer->cursor), undo_node->after_selection.text);
	}

	buffer_typeset_from(buffer, typeset_start_line);

	if (pre_start_line == NULL) {
		lexy_update_starting_at(buffer, buffer->real_line, true);
	} else {
		lexy_update_starting_at(buffer, pre_start_line, true);
	}

	if (buffer->onchange != NULL) buffer->onchange(buffer);
}














void debug_print_real_lines_state(buffer_t *buffer) __attribute__ ((unused));
void debug_print_real_lines_state(buffer_t *buffer) {
	real_line_t *real_line;
	int i;

	for (real_line = buffer->real_line; real_line != NULL; real_line = real_line->next) {
		printf("%d [", real_line->lineno);
		for (i = 0; i < real_line->cap; ++i) {
			printf("%c", (char)(real_line->glyph_info[i].code));
		}
		printf("]\n");
	}

	printf("------------------------\n");

}





void buffer_change_select_type(buffer_t *buffer, enum select_type select_type) {
	buffer->select_type = select_type;
	buffer_extend_selection_by_select_type(buffer);
}





static void buffer_aux_go_first_nonws(lpoint_t *p) {
	int i;
	for (i = 0; i < p->line->cap; ++i) {
		uint32_t code = p->line->glyph_info[i].code;
		if ((code != 0x20) && (code != 0x09)) break;
	}
	p->glyph = i;
}

static void buffer_aux_go_first_nonws_or_0(lpoint_t *p) {
	int old_cursor_glyph = p->glyph;
	buffer_aux_go_first_nonws(p);
	if (old_cursor_glyph == p->glyph) {
		p->glyph = 0;
	}
}















const char *OPENING_PARENTHESIS = "([{<";
const char *CLOSING_PARENTHESIS = ")]}>";

static uint32_t point_to_char_to_find(lpoint_t *point, const char *tomatch, const char *tofind) {
	uint32_t code = LPOINTGI(*point).code;
	if (code > 0x7f) return false;

	for (int i = 0; i < strlen(tomatch); ++i) {
		if (tomatch[i] == (char)code) {
			return tofind[i];
		}
	}

	return 0;
}

static bool parmatch_find_ex(lpoint_t *start, lpoint_t *match, const char *tomatch, const char *tofind, int direction, int nlines) {
	if (start->glyph < 0) return false;
	if (start->glyph >= start->line->cap) return false;

	uint32_t cursor_char = LPOINTGI(*start).code;
	uint32_t char_to_find = point_to_char_to_find(start, tomatch, tofind);
	if (char_to_find == 0) return false;

	lpoint_t cur;

	copy_lpoint(&cur, start);

	int checked_lines = 0;
	int depth = 1;

	for (;;) {
		if (cur.line == NULL) break;
		if (checked_lines >= nlines) break;
		cur.glyph += direction;

		if (cur.glyph < 0) {
			cur.line = cur.line->prev;
			if (cur.line != NULL) cur.glyph = cur.line->cap;
			++checked_lines;
			continue;
		} else if (cur.glyph >= cur.line->cap) {
			cur.line = cur.line->next;
			cur.glyph = -1;
			++checked_lines;
			continue;
		}

		if (LPOINTGI(cur).code == cursor_char) {
			++depth;
		}

		if (LPOINTGI(cur).code == char_to_find) {
			--depth;
			if (depth == 0) {
				copy_lpoint(match, &cur);
				return true;
			}
		}
	}

	return false;
}
