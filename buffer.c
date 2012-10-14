#include "buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <unicode/uchar.h>

#include "global.h"
#include "cfg.h"
#include "columns.h"
#include "lexy.h"
#include "interp.h"
#include "foundry.h"
#include "buffers.h"

#define WORDCOMPL_UPDATE_RADIUS 1000
#define MINIMUM_WORDCOMPL_WORD_LEN 3

static void buffer_init_font_extents(buffer_t *buffer) {
	cairo_text_extents_t extents;
	cairo_font_extents_t font_extents;

	teddy_fontset_t *font = foundry_lookup(config_strval(&(buffer->config), CFG_MAIN_FONT), true);

	cairo_scaled_font_text_extents(fontset_get_cairofont(font, 0), "M", &extents);
	buffer->em_advance = extents.x_advance;

	cairo_scaled_font_text_extents(fontset_get_cairofont(font, 0), "x", &extents);
	buffer->ex_height = extents.height;

	cairo_scaled_font_text_extents(fontset_get_cairofont(font, 0), " ", &extents);
	buffer->space_advance = extents.x_advance;

	cairo_scaled_font_extents(fontset_get_cairofont(font, 0), &font_extents);
	buffer->line_height = font_extents.height - config_intval(&(buffer->config), CFG_MAIN_FONT_HEIGHT_REDUCTION);
	buffer->ascent = font_extents.ascent;
	buffer->descent = font_extents.descent;

	fontset_underline_info(font, 0, &(buffer->underline_thickness), &(buffer->underline_position));

	//printf("Underline thickness: %g position: %g\n", buffer->underline_thickness, buffer->underline_position);

	foundry_release(font);
}

static void buffer_setup_hook(buffer_t *buffer) {
	const char *argv[] = { "buffer_setup_hook", buffer->path };
	interp_eval_command(NULL, buffer, 2, argv);
}

void buffer_set_mark_at_cursor(buffer_t *buffer) {
	copy_lpoint(&(buffer->mark), &(buffer->cursor));
	copy_lpoint(&(buffer->savedmark), &(buffer->cursor));
	buffer->select_type = BST_NORMAL;
	//printf("Mark set @ %d,%d\n", buffer->mark_line->lineno, buffer->mark_glyph);
}

void buffer_unset_mark(buffer_t *buffer) {
	if (buffer->mark.line != NULL) {
		buffer->mark.line = NULL;
		buffer->mark.glyph = -1;

		buffer->savedmark.line = NULL;
		buffer->savedmark.glyph = -1;
		//printf("Mark unset\n");
	}
}

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
	line->lexy_state_start = line->lexy_state_end = LEXY_STATUS_NUMBER;
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

	teddy_fontset_t *font = foundry_lookup(config_strval(&(buffer->config), CFG_MAIN_FONT), true);

	for (src = 0, dst = insertion_point; src < len; ) {
		bool valid = true;
		uint32_t code = utf8_to_utf32(text, &src, len, &valid);
		uint8_t fontidx = fontset_fontidx(font, code);
		//printf("First char: %02x\n", (uint8_t)text[src]);

		if ((code < 0x20) && (code != 0x09) && (code != 0x0a) && (code != 0x0d)) {
			valid = false;
		}

		if (valid) *valid_chars = *valid_chars + 1;
		else *invalid_chars = *invalid_chars + 1;

		FT_UInt glyph_index = fontset_glyph_index(font, fontidx, (code != 0x09) ? code : 0x20);

		grow_line(line, dst, 1);

		line->glyph_info[dst].code = code;
		line->glyph_info[dst].color = buffer->default_color;

		line->glyph_info[dst].kerning_correction = (previous_fontidx == fontidx) ? fontset_get_kerning(font, fontidx, previous, glyph_index) : 0.0;

		previous = line->glyph_info[dst].glyph_index = glyph_index;
		previous_fontidx = line->glyph_info[dst].fontidx = fontidx;
		line->glyph_info[dst].x = 0.0;
		line->glyph_info[dst].y = 0.0;

		/*if (code == 0x09) {
			extents.x_advance *= buffer->tab_width;
		}*/

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

	foundry_release(font);

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

static void freeze_selection(buffer_t *buffer, selection_t *selection, lpoint_t *start, lpoint_t *end) {
	if ((start->line == NULL) || (end->line == NULL)) {
		freeze_point(&(selection->start), &(buffer->cursor));
		freeze_point(&(selection->end), &(buffer->cursor));
		selection->text = malloc(sizeof(char));
		alloc_assert(selection->text);

		selection->text[0] = '\0';
	} else {
		freeze_point(&(selection->start), start);
		freeze_point(&(selection->end), end);

		selection->text = buffer_lines_to_text(buffer, start, end);
	}
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
	parmatch_invalidate(&(buffer->parmatch));

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

void load_empty(buffer_t *buffer) {
	if (buffer->has_filename) {
		return;
	}

	buffer->has_filename = 0;
	if (buffer->path == NULL)
		buffer->path = strdup("+empty+");

	buffer->cursor.line = buffer->real_line = new_real_line(0);
	buffer->cursor.glyph = 0;

	buffer_setup_hook(buffer);
}

int load_dir(buffer_t *buffer, const char *dirname) {
	/*DIR *dir = opendir(dirname);
	if (dir == NULL) {
		return -1;
	}*/

	buffer->cursor.line = buffer->real_line = new_real_line(0);
	buffer->cursor.glyph = 0;

	buffer->has_filename = 0;
	buffer->path = realpath(dirname, NULL);
	if (buffer->path[strlen(buffer->path)-1] != '/') {
		char *p = malloc((strlen(buffer->path) + 2) * sizeof(char));
		strcpy(p, buffer->path);
		strcat(p, "/");
		free(buffer->path);
		buffer->path = p;
	}

	buffer_setup_hook(buffer);

	buffer->modified = false;

	return 0;
}

int load_text_file(buffer_t *buffer, const char *filename) {
	buffer->mtime = time(NULL);

	FILE *fin = fopen(filename, "r");
	int ch;
	int i = 0;
	int text_allocation = 10;
	char *text = malloc(sizeof(char) * text_allocation);
	alloc_assert(text);

	real_line_t *prev_line = NULL;
	real_line_t **real_line_pp = &(buffer->real_line);
	int lineno = 0;

	if (!fin) {
		return -1;
	}

	if (buffer->has_filename) {
		return -1;
	}

	buffer->has_filename = 1;
	free(buffer->path);
	buffer->path = realpath(filename, NULL);

	buffer_setup_hook(buffer);

	int valid_chars = 0, invalid_chars = 0;

	while ((ch = fgetc(fin)) != EOF) {
		if (i >= text_allocation) {
			text_allocation *= 2;
			text = realloc(text, sizeof(char) * text_allocation);
			if (text == NULL) {
				perror("Couldn't allocate memory");
				exit(EXIT_FAILURE);
			}
		}
		if (ch == '\n') {
			text[i] = '\0';
			if (strlen(text) > 100000) {
				valid_chars = 0;
				invalid_chars = 2048;
				break;
			}
			if (*real_line_pp == NULL) *real_line_pp = new_real_line(lineno);
			buffer_line_insert_utf8_text(buffer, *real_line_pp, text, i, (*real_line_pp)->cap, &valid_chars, &invalid_chars);
			(*real_line_pp)->prev = prev_line;
			prev_line = *real_line_pp;
			real_line_pp = &((*real_line_pp)->next);
			i = 0;
			++lineno;

			if (valid_chars + invalid_chars > 1024) {
				if (((float)valid_chars / (float)(valid_chars + invalid_chars)) < 0.75) {
					break;
				}
			}
		} else {
			text[i++] = ch;
		}
	}

	text[i] = '\0';
	if (*real_line_pp == NULL) *real_line_pp = new_real_line(lineno);
	buffer_line_insert_utf8_text(buffer, *real_line_pp, text, i, (*real_line_pp)->cap, &valid_chars, &invalid_chars);
	(*real_line_pp)->prev = prev_line;

	buffer->cursor.line = buffer->real_line;
	buffer->cursor.glyph = 0;

	free(text);

	//printf("Loaded lines: %d (name: %s) (path: %s)\n", lineno, buffer->name, buffer->path);

	fclose(fin);

	buffer_wordcompl_update(buffer, &(buffer->cbt));
	lexy_update_starting_at(buffer, buffer->real_line, false);

	if (valid_chars + invalid_chars > 100) {
		if ((float)valid_chars / (float)(valid_chars + invalid_chars) < 0.75) {
			return -2;
		}
	}

	const char *argv[] = { "buffer_loaded_hook", buffer->path };
	interp_eval_command(NULL, buffer, 2, argv);

	return 0;
}

char *buffer_lines_to_text(buffer_t *buffer, lpoint_t *startp, lpoint_t *endp) {
	real_line_t *line;
	int allocated = 0;
	int cap = 0;
	char *r = NULL;

	allocated = 10;
	r = malloc(sizeof(char) * allocated);

	for (line = startp->line; line != NULL; line = line->next) {
		int start, end, i;
		if (line == startp->line) {
			start = startp->glyph;
		} else {
			start = 0;
		}

		if (line == endp->line) {
			end = endp->glyph;
			if (end > line->cap) end = line->cap;
		} else {
			end = line->cap;
		}

		for (i = start; i < end; ++i) {
			uint32_t code = line->glyph_info[i].code;

			utf32_to_utf8(code, &r, &cap, &allocated);
		}


		if (line == endp->line) break;
		else utf32_to_utf8((uint32_t)'\n', &r, &cap, &allocated);
	}

	if (cap >= allocated) {
		allocated *= 2;
		r = realloc(r, sizeof(char)*allocated);
	}
	r[cap++] = '\0';

	return r;
}

void save_to_text_file(buffer_t *buffer) {
	if (buffer->path[0] == '+') return;

	FILE *file = fopen(buffer->path, "w");

	if (!file) {
		quick_message("Error writing file", "Couldn't open file for write");
		return;
	}

	buffer_wordcompl_update(buffer, &(buffer->cbt));

	char *r; {
		lpoint_t startp = { buffer->real_line, 0 };
		lpoint_t endp = { NULL, -1 };
		r = buffer_lines_to_text(buffer, &startp, &endp);
	}

	if (r[strlen(r)-1] == '\n') r[strlen(r)-1] = '\0'; // removing spurious final newline added by loading function

	size_t towrite = strlen(r);
	size_t write_start = 0;

	while (towrite > 0) {
		size_t written = fwrite(r+write_start, sizeof(char), towrite, file);
		if (written == 0) {
			quick_message("Error writing file", "Error writing file to disk");
			return;
		}
		towrite -= written;
		write_start += written;
	}

	if (fclose(file) != 0) {
		quick_message("Error writing file", "Error writing file to disk");
		return;
	}

	free(r);

	buffer->modified = 0;
	buffer->mtime = time(NULL)+10;
}

void line_get_glyph_coordinates(buffer_t *buffer, lpoint_t *point, double *x, double *y) {
	if (point->line == NULL) {
		*y = 0.0;
		*x = 0.0;
		return;
	}

	if (point->line->cap == 0) {
		*x = buffer->left_margin;
		*y = point->line->start_y;
		return;
	}

	if (point->glyph >= point->line->cap) {
		*y = point->line->glyph_info[point->line->cap-1].y;
		*x = point->line->glyph_info[point->line->cap-1].x + point->line->glyph_info[point->line->cap-1].x_advance;
	} else {
		*y = point->line->glyph_info[point->glyph].y;
		*x = point->line->glyph_info[point->glyph].x;
	}
}

void buffer_point_from_position(buffer_t *buffer, double x, double y, lpoint_t *p) {
	real_line_t *line, *prev = NULL;
	int i, glyph = 0;

	for (line = buffer->real_line; line->next != NULL; line = line->next) {
		//printf("Cur y: %g (searching %g)\n", line->start_y, y);
		if (line->end_y > y) break;
	}

	//printf("New position lineno: %d\n", line->lineno);

	if (line == NULL) line = prev;
	assert(line != NULL);

	for (i = 0; i < line->cap; ++i) {
		if ((y >= line->glyph_info[i].y - buffer->line_height) && (y <= line->glyph_info[i].y)) {
			double glyph_start = line->glyph_info[i].x;
			double glyph_end = glyph_start + line->glyph_info[i].x_advance;

			if (x < glyph_start) {
				glyph = i;
				break;
			}

			if ((x >= glyph_start) && (x <= glyph_end)) {
				double dist_start = x - glyph_start;
				double dist_end = glyph_end - x;
				if (dist_start < dist_end) {
					glyph = i;
				} else {
					glyph = i+1;
				}
				break;
			}
		}
	}

	if (i >= line->cap) glyph = line->cap;

	p->line = line; p->glyph = glyph;
}

void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y) {
	lpoint_t p;
	buffer_point_from_position(buffer, x, y, &p);
	copy_lpoint(&(buffer->cursor), &p);
	buffer_extend_selection_by_select_type(buffer);
}

void buffer_update_parmatch(buffer_t *buffer) {
	if ((buffer->parmatch.cursor_cache.line == buffer->cursor.line) &&
		(buffer->parmatch.cursor_cache.glyph == buffer->cursor.glyph)) return;
	parmatch_find(&(buffer->parmatch), &(buffer->cursor));
}

buffer_t *buffer_create(void) {
	buffer_t *buffer = malloc(sizeof(buffer_t));

	config_init(&(buffer->config), &global_config);

	buffer->modified = 0;
	buffer->editable = 1;
	buffer->job = NULL;
	buffer->default_color = 0;
	buffer->inotify_wd = -1;
	buffer->mtime = 0;
	buffer->stale = false;

	asprintf(&(buffer->path), "+unnamed");
	alloc_assert(buffer->path);
	buffer->has_filename = 0;
	buffer->select_type = BST_NORMAL;

	buffer->lexy_last_update_line = NULL;

	undo_init(&(buffer->undo));

	buffer_init_font_extents(buffer);

	buffer->real_line = NULL;

	buffer->rendered_height = 0.0;
	buffer->rendered_width = 0.0;

	buffer->cursor.line = NULL;
	buffer->cursor.glyph = 0;

	buffer->mark.line = NULL;
	buffer->mark.glyph = -1;

	parmatch_init(&(buffer->parmatch));

	buffer->left_margin = 4.0;
	buffer->right_margin = 4.0;

	buffer->props = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
	buffer->keyprocessor = NULL;

	buffer->cbt.root = NULL;

	buffer->onchange = NULL;

	return buffer;
}

static int to_closed_buffers_critbit(const char *entry, void *p) {
	critbit0_insert(&closed_buffers_critbit, entry);
	return 1;
}

void buffer_free(buffer_t *buffer, bool save_critbit) {
	for (real_line_t *cursor = buffer->real_line; cursor != NULL; ) {
		real_line_t *next = cursor->next;
		free(cursor->glyph_info);
		free(cursor);
		cursor = next;
	}

	g_hash_table_destroy(buffer->props);

	undo_free(&(buffer->undo));

	if (save_critbit) critbit0_allprefixed(&(buffer->cbt), "", to_closed_buffers_critbit, NULL);

	critbit0_clear(&(buffer->cbt));

	free(buffer->path);
	if (buffer->keyprocessor != NULL) free(buffer->keyprocessor);
	free(buffer);
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

void buffer_get_selection_pointers(buffer_t *buffer, lpoint_t **start, lpoint_t **end) {
	if (buffer->mark.line == NULL) {
		*start = NULL;
		*end = NULL;
		return;
	}

	if (buffer->mark.line == buffer->cursor.line) {
		if (buffer->mark.glyph == buffer->cursor.glyph) {
			*start = &(buffer->cursor);
			*end = &(buffer->mark);
			return;
		} else if (buffer->mark.glyph < buffer->cursor.glyph) {
			*start = &(buffer->mark);
			*end = &(buffer->cursor);
		} else {
			*start = &(buffer->cursor);
			*end = &(buffer->mark);
		}
	} else if (buffer->mark.line->lineno < buffer->cursor.line->lineno) {
		*start = &(buffer->mark);
		*end = &(buffer->cursor);
	} else {
		*start = &(buffer->cursor);
		*end = &(buffer->mark);
	}

	return;
}

void buffer_get_selection(buffer_t *buffer, lpoint_t *start, lpoint_t *end) {
	lpoint_t *pstart, *pend;
	buffer_get_selection_pointers(buffer, &pstart, &pend);

	if ((pstart == NULL) || (pend == NULL)) {
		start->line = end->line = NULL;
		start->glyph = end->glyph = 0;
	} else {
		copy_lpoint(start, pstart);
		copy_lpoint(end, pend);
	}
}

void buffer_typeset_maybe(buffer_t *buffer, double width, bool single_line, bool force) {
	real_line_t *line;
	double y = single_line ? buffer->ascent : buffer->line_height + (buffer->ex_height / 2);

	if (!force) {
		if (fabs(width - buffer->rendered_width) < 0.001) {
			return;
		}
		buffer->rendered_width = width;
	}

	for (line = buffer->real_line; line != NULL; line = line->next) {
		buffer_line_adjust_glyphs(buffer, line, y);
		y += line->y_increment;
	}
}

void buffer_change_select_type(buffer_t *buffer, enum select_type select_type) {
	buffer->select_type = select_type;
	buffer_extend_selection_by_select_type(buffer);
}

void buffer_extend_selection_by_select_type(buffer_t *buffer) {
	if (buffer->select_type == BST_NORMAL) return;
	if (buffer->mark.line == NULL) return;
	if (buffer->savedmark.line == NULL) return;
	if (buffer->cursor.line == NULL) return;

	copy_lpoint(&(buffer->mark), &(buffer->savedmark));

	lpoint_t *start, *end;

	buffer_get_selection_pointers(buffer, &start, &end);

	switch (buffer->select_type) {
	case BST_LINES:
		start->glyph = 0;
		end->glyph = end->line->cap;
		break;
	case BST_WORDS:
		buffer_move_point_glyph(buffer, start, MT_RELW, -1);
		buffer_move_point_glyph(buffer, end, MT_RELW, +1);
		break;
	default:
		//nothing to do (is unknown)
		break;
	}
}

void buffer_config_changed(buffer_t *buffer) {
	buffer_init_font_extents(buffer);
	buffer_typeset_maybe(buffer, 0.0, false, true);
}

void buffer_set_onchange(buffer_t *buffer, void (*fn)(buffer_t *buffer)) {
	buffer->onchange = fn;
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

static UBool u_isalnum_or_underscore(uint32_t code) {
	return u_isalnum(code) || (code == 0x5f);
}

/*if it is at the beginning of a word (or inside a word) goes to the end of this word, if it is at the end of a word (or inside a non-word sequence) goes to the beginning of the next one*/
static bool buffer_aux_wnwa_next_ex(lpoint_t *point) {
	UBool searching_alnum;
	if (point->glyph >= point->line->cap) return false;

	searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

	bool r = false;

	for ( ; point->glyph < point->line->cap; ++(point->glyph)) {
		if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) {
			r = true;
			break;
		}
	}

	return r;
}

/* If it is at the beginning of a word (or inside a non-word sequence) goes to the end of the previous word, if it is at the end of a word (or inside a word) goes to the beginning of the word) */
static bool buffer_aux_wnwa_prev_ex(lpoint_t *point) {
	UBool searching_alnum;
	if (point->glyph <= 0) return false;

	--(point->glyph);

	searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

	bool r = false;

	for ( ; point->glyph >= 0; --(point->glyph)) {
		if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) {
			r = true;
			break;
		}
	}

	++(point->glyph);

	return r;
}

bool buffer_move_point_line(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg) {
	//printf("Move point line: %d (%d)\n", arg, type);

	bool r = true;

	switch (type) {
	case MT_REL:
		if (p->line == NULL) return false;

		while (arg < 0) {
			real_line_t *to = p->line->prev;
			if (to == NULL) { r = false; break; }
			p->line = to;
			++arg;
		}

		while (arg > 0) {
			real_line_t *to = p->line->next;
			if (to == NULL) { r = false; break; }
			p->line = to;
			--arg;
		}

		break;

	case MT_END:
		if (p->line == NULL) p->line = buffer->real_line;
		for (; p->line->next != NULL; p->line = p->line->next);
		break;

	case MT_ABS: {
		real_line_t *prev = buffer->real_line;
		for (p->line = buffer->real_line; p->line != NULL; p->line = p->line->next) {
			if (p->line->lineno+1 == arg) break;
			prev = p->line;
		}
		if (p->line == NULL) { r = false; p->line = prev; }
		break;
	}

	default:
		quick_message("Internal error", "Internal error buffer_move_point_line");
		return false;
	}

	if (p->glyph > p->line->cap) p->glyph = p->line->cap;
	if (p->glyph < 0) p->glyph = 0;

	return r;
}

bool buffer_move_point_glyph(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg) {
	if (p->line == NULL) return false;

	bool r = true;

	//printf("Move point glyph: %d (%d)\n", arg, type);

	switch (type) {
	case MT_REL:
		p->glyph += arg;

		while (p->glyph > p->line->cap) {
			if (p->line->next == NULL) { r = false; break; }
			p->glyph = p->glyph - p->line->cap - 1;
			p->line = p->line->next;
		}

		while (p->glyph < 0) {
			if (p->line->prev == NULL) { r = false; break; }
			p->line = p->line->prev;
			p->glyph = p->line->cap + (p->glyph + 1);
		}
		break;

	case MT_RELW:
		while (arg < 0) {
			r = buffer_aux_wnwa_prev_ex(p);
			++arg;
		}

		while (arg > 0) {
			r = buffer_aux_wnwa_next_ex(p);
			--arg;
		}
		break;

	case MT_END:
		p->glyph = p->line->cap;
		break;

	case MT_START:
		buffer_aux_go_first_nonws(p);
		break;

	case MT_HOME:
		buffer_aux_go_first_nonws_or_0(p);
		break;

	case MT_ABS:
		if (arg < 0) return false;
		p->glyph = arg-1;
		break;

	default:
		quick_message("Internal error", "Internal error buffer_move_point_glyph");
	}

	if (p->glyph > p->line->cap) p->glyph = p->line->cap;
	if (p->glyph < 0) p->glyph = 0;

	return r;
}

void buffer_indent_newline(buffer_t *buffer, char *r) {
	real_line_t *line;
	for (line = buffer->cursor.line; line != NULL; line = line->prev) {
		if (line->cap > 0) break;
	}

	if (line == NULL) line = buffer->cursor.line;

	r[0] = '\n';
	int i;
	for (i = 0; i < line->cap; ++i) {
		uint32_t code = line->glyph_info[i].code;
		if (code == 0x20) {
			r[i+1] = ' ';
		} else if (code == 0x09) {
			r[i+1] = '\t';
		} else {
			r[i+1] = '\0';
			break;
		}
	}
	r[i+1] = '\0';
}

bool wordcompl_charset[0x10000];

void buffer_wordcompl_init_charset(void) {
	for (uint32_t i = 0; i < 0x10000; ++i) {
		if (u_isalnum(i)) {
			wordcompl_charset[i] = true;
		} else if (i == 0x5f) { // underscore
			wordcompl_charset[i] = true;
		} else {
			wordcompl_charset[i] = false;
		}
	}
}

static uint16_t *buffer_to_utf16(buffer_t *buffer, int start, size_t len) {
	uint16_t *prefix = malloc(sizeof(uint16_t) * len);
	alloc_assert(prefix);

	for (int i = 0; i < len; ++i) prefix[i] = buffer->cursor.line->glyph_info[start+i].code;

	return prefix;
}

uint16_t *buffer_wordcompl_word_at_cursor(buffer_t *buffer, size_t *prefix_len) {
	*prefix_len = 0;

	if (buffer->cursor.line == NULL) return NULL;

	int start;
	for (start = buffer->cursor.glyph-1; start >= 0; --start) {
		uint32_t code = buffer->cursor.line->glyph_info[start].code;
		if ((code >= 0x10000) || (!wordcompl_charset[code])) { break; }
	}

	++start;

	if (start ==  buffer->cursor.glyph) return NULL;

	*prefix_len = buffer->cursor.glyph - start;

	return buffer_to_utf16(buffer, start, *prefix_len);
}

uint16_t *buffer_cmdcompl_word_at_cursor(buffer_t *buffer, size_t *prefix_len) {
	*prefix_len = 0;

	if (buffer->cursor.line == NULL) return NULL;

	int start;
	for (start = buffer->cursor.glyph-1; start >= 0; --start) {
		uint32_t code = buffer->cursor.line->glyph_info[start].code;
		if ((code >= 0x10000) || code == 0x20 || code == 0x09) break;
	}

	++start;

	*prefix_len = buffer->cursor.glyph - start;
	return buffer_to_utf16(buffer, start, *prefix_len);
}

uint16_t *buffer_historycompl_word_at_cursor(buffer_t *buffer, size_t *prefix_len) {
	*prefix_len = 0;

	if (buffer->cursor.line == NULL) return NULL;

	*prefix_len = buffer->cursor.line->cap;
	return buffer_to_utf16(buffer, 0, *prefix_len);
}

static void buffer_wordcompl_update_word(real_line_t *line, int start, int end, critbit0_tree *c) {
	if (end - start < MINIMUM_WORDCOMPL_WORD_LEN) return;

	int allocated = end-start, cap = 0;
	char *r = malloc(allocated * sizeof(char));
	alloc_assert(r);

	for (int j = 0; j < end-start; ++j) {
		utf32_to_utf8(line->glyph_info[j+start].code, &r, &cap, &allocated);
	}

	utf32_to_utf8(0, &r, &cap, &allocated);

	critbit0_insert(c, r);
	free(r);
}

void buffer_wordcompl_update_line(real_line_t *line, critbit0_tree *c) {
	int start = -1;
	for (int i = 0; i < line->cap; ++i) {
		if (start < 0) {
			if (line->glyph_info[i].code > 0x10000) continue;
			if (wordcompl_charset[line->glyph_info[i].code]) start = i;
		} else {
			if ((line->glyph_info[i].code >= 0x10000) || !wordcompl_charset[line->glyph_info[i].code]) {
				buffer_wordcompl_update_word(line, start, i, c);
				start = -1;
			}
		}
	}

	if (start >= 0) buffer_wordcompl_update_word(line, start, line->cap, c);
}

void buffer_wordcompl_update(buffer_t *buffer, critbit0_tree *cbt) {
	critbit0_clear(cbt);

	real_line_t *start = buffer->cursor.line;
	if (start == NULL) start = buffer->real_line;

	int count = WORDCOMPL_UPDATE_RADIUS;
	for (real_line_t *line = start; line != NULL; line = line->prev) {
		--count;
		if (count <= 0) break;

		buffer_wordcompl_update_line(line, cbt);
	}

	count = WORDCOMPL_UPDATE_RADIUS;
	for (real_line_t *line = start->next; line != NULL; line = line->next) {
		--count;
		if (count <= 0) break;

		buffer_wordcompl_update_line(line, cbt);
	}

	word_completer_full_update();
}

void buffer_get_extremes(buffer_t *buffer, lpoint_t *start, lpoint_t *end) {
	start->line = buffer->real_line;
	start->glyph = 0;

	for (end->line = start->line; end->line->next != NULL; end->line = end->line->next);
	end->glyph = end->line->cap;
}

void buffer_aux_clear(buffer_t *buffer) {
	buffer_get_extremes(buffer, &(buffer->mark), &(buffer->cursor));
	buffer_replace_selection(buffer, "");
}

void buffer_select_all(buffer_t *buffer) {
	buffer_get_extremes(buffer, &(buffer->mark), &(buffer->cursor));
}

char *buffer_get_selection_text(buffer_t *buffer) {
	lpoint_t start, end;
	buffer_get_selection(buffer, &start, &end);
	if (start.line == NULL) return NULL;
	if (end.line == NULL) return NULL;
	return buffer_lines_to_text(buffer, &start, &end);
}

char *buffer_all_lines_to_text(buffer_t *buffer) {
	lpoint_t start, end;
	buffer_get_extremes(buffer, &start, &end);
	return buffer_lines_to_text(buffer, &start, &end);
}

