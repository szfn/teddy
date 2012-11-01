#include "buffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <unicode/uchar.h>

#include "global.h"
#include "foundry.h"
#include "interp.h"
#include "lexy.h"
#include "undo.h"
#include "compl.h"

#define SLOP 32

#define WORDCOMPL_UPDATE_RADIUS 5000
#define MINIMUM_WORDCOMPL_WORD_LEN 3

static int phisical(buffer_t *encl, int point) {
	return (point < encl->gap) ? point : point + encl->gapsz;
}

my_glyph_info_t *bat(buffer_t *encl, int point) {
	if (point < 0) return NULL;
	int pp = (point < encl->gap) ? point : point + encl->gapsz;
	return (pp < encl->size) ? encl->buf + pp : NULL;
}

static void movegap(buffer_t *buffer, int point) {
	int pp = phisical(buffer, point);
	if (pp < buffer->gap) {
		//printf("slide forward %d -> %zd (size: %d) (%p)\n", pp, pp + buffer->gapsz, buffer->gap - pp, buffer->buf);
		memmove(buffer->buf + pp + buffer->gapsz, buffer->buf + pp, sizeof(my_glyph_info_t) * (buffer->gap - pp));
		buffer->gap = pp;
		//printf("after slide: ");
		//gb_debug_print(buffer);
	} else if (pp > buffer->gap) {
		/*printf("slide backwards:\n");
		printf("\t gap: %d gapsz %zd pp %d (point: %d)\n", buffer->gap, buffer->gapsz, pp, point);
		printf("\t %zd -> %d (move size: %zd total size: %zd) (%p)\n", buffer->gap + buffer->gapsz, buffer->gap, pp - buffer->gap - buffer->gapsz, buffer->size, buffer->buf);*/
		memmove(buffer->buf + buffer->gap, buffer->buf + buffer->gap + buffer->gapsz, sizeof(my_glyph_info_t) * (pp - buffer->gap - buffer->gapsz));
		buffer->gap = pp - buffer->gapsz;
	}

	//printf("buffer->size = %zd\n", buffer->size);
}

static void regap(buffer_t *buffer) {
	/*printf("before regap: ");
	gb_debug_print(buffer);*/

	my_glyph_info_t *newbuf = malloc(sizeof(my_glyph_info_t) * (buffer->size+SLOP));
	alloc_assert(newbuf);

	memmove(newbuf, buffer->buf, sizeof(my_glyph_info_t) * buffer->gap);
	memmove(newbuf+buffer->gap+SLOP, buffer->buf + buffer->gap, sizeof(my_glyph_info_t) * (buffer->size - buffer->gap));

	buffer->gapsz = SLOP;
	buffer->size += SLOP;

	free(buffer->buf);
	buffer->buf = newbuf;

	/*printf("after regap: ");
	gb_debug_print(buffer);*/
}

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

	undo_init(&(buffer->undo));

	buffer_init_font_extents(buffer);

	buffer->buf = malloc(sizeof(my_glyph_info_t) * SLOP);
	alloc_assert(buffer->buf);
	buffer->size = SLOP;
	buffer->cursor = -1;
	buffer->mark = -1;
	buffer->gap = 0;
	buffer->gapsz = SLOP;

	buffer->rendered_height = 0.0;
	buffer->rendered_width = 0.0;

	buffer->savedmark = -1;

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
	free(buffer->buf);

	g_hash_table_destroy(buffer->props);

	undo_free(&(buffer->undo));

	if (save_critbit) critbit0_allprefixed(&(buffer->cbt), "", to_closed_buffers_critbit, NULL);

	critbit0_clear(&(buffer->cbt));

	free(buffer->path);
	if (buffer->keyprocessor != NULL) free(buffer->keyprocessor);
	free(buffer);
}

static void buffer_setup_hook(buffer_t *buffer) {
	const char *argv[] = { "buffer_setup_hook", buffer->path };
	interp_eval_command(NULL, buffer, 2, argv);
}

static int buffer_replace_selection_ex(buffer_t *buffer, const char *text) {
	teddy_fontset_t *font = foundry_lookup(config_strval(&(buffer->config), CFG_MAIN_FONT), true);

	// there is a mark, delete
	if (buffer->mark >= 0) {
		int region_size = MAX(buffer->mark, buffer->cursor) - MIN(buffer->mark, buffer->cursor);
		movegap(buffer, MIN(buffer->mark, buffer->cursor)+1);
		buffer->gapsz += region_size;
		buffer->cursor = MIN(buffer->mark, buffer->cursor);
		buffer->mark = -1;
	} else {
		//printf("Calling movegap: %d\n", buffer->cursor+1);
		movegap(buffer, buffer->cursor+1);
	}

	int start_cursor = buffer->cursor;

	int len = strlen(text);
	for (int i = 0; i < len; ) {
		if (buffer->gapsz <= 0) regap(buffer);

		bool valid = false;
		uint32_t code = utf8_to_utf32(text, &i, len, &valid);

		buffer->buf[buffer->gap].code = code;

		uint8_t fontidx = fontset_fontidx(font, code);
		FT_UInt glyph_index = fontset_glyph_index(font, fontidx, (code != 0x09) ? code : 0x20);

		buffer->buf[buffer->gap].code = code;
		buffer->buf[buffer->gap].color = buffer->default_color;

		//printf("gap: %d\n", buffer->gap);
		if ((buffer->gap > 0) && (buffer->buf[buffer->gap-1].fontidx == fontidx)) {
			buffer->buf[buffer->gap].kerning_correction = fontset_get_kerning(font, fontidx, buffer->buf[buffer->gap-1].glyph_index, glyph_index);
		} else {
			buffer->buf[buffer->gap].kerning_correction = 0.0;
		}

		buffer->buf[buffer->gap].glyph_index = glyph_index;
		buffer->buf[buffer->gap].fontidx = fontidx;
		buffer->buf[buffer->gap].x = buffer->buf[buffer->gap].y = 0.0;
		buffer->buf[buffer->gap].x_advance = fontset_x_advance(font, fontidx, glyph_index);

		++(buffer->cursor);
		++(buffer->gap);
		--(buffer->gapsz);
	}

	my_glyph_info_t *last_char = bat(buffer, buffer->cursor);
	my_glyph_info_t *next_char = bat(buffer, buffer->cursor+1);
	if ((last_char != NULL) && (next_char != NULL) && (last_char->fontidx == next_char->fontidx)) {
		next_char->kerning_correction = fontset_get_kerning(font, last_char->fontidx, last_char->glyph_index, next_char->glyph_index);
	}

	foundry_release(font);

	return start_cursor;
}

int load_text_file(buffer_t *buffer, const char *filename) {
	buffer->mtime = time(NULL);

	if (buffer->has_filename) {
		return -1;
	}

	FILE *fin = fopen(filename, "r");
	if (!fin) {
		return -1;
	}

	buffer->has_filename = 1;
	free(buffer->path);
	buffer->path = realpath(filename, NULL);

	buffer_setup_hook(buffer);

#define MAX_UTF8 12
	char text[MAX_UTF8];
	int i = 0;
	int ch;
	while ((ch = fgetc(fin)) != EOF) {
		text[i++] = ch;
		int charlen = utf8_first_byte_processing(ch);

		if (charlen < 8) {
			for (; i < charlen+1; ++i) {
				text[i++] = fgetc(fin);
			}
		}

		text[i] = '\0';

		buffer_replace_selection_ex(buffer, text);
	}

	//TODO: reset undo informations here

	buffer->cursor = 0;

	fclose(fin);

	buffer_wordcompl_update(buffer, &(buffer->cbt), WORDCOMPL_UPDATE_RADIUS);
	lexy_update_starting_at(buffer, 0, false);

	const char *argv[] = { "buffer_loaded_hook", buffer->path };
	interp_eval_command(NULL, buffer, 2, argv);

	return 0;
}

void load_empty(buffer_t *buffer) {
	if (buffer->has_filename) {
		return;
	}

	buffer->has_filename = 0;
	if (buffer->path == NULL)
		buffer->path = strdup("+empty+");

	buffer_setup_hook(buffer);
}

int load_dir(buffer_t *buffer, const char *dirname) {
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

void save_to_text_file(buffer_t *buffer) {
	if (buffer->path[0] == '+') return;

	FILE *file = fopen(buffer->path, "w");

	if (!file) {
		quick_message("Error writing file", "Couldn't open file for write");
		return;
	}

	buffer_wordcompl_update(buffer, &(buffer->cbt), WORDCOMPL_UPDATE_RADIUS);

	char *r; {
		r = buffer_lines_to_text(buffer, 0, buffer->size - buffer->gapsz);
	}

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

void buffer_set_mark_at_cursor(buffer_t *buffer) {
	buffer->savedmark = buffer->mark = buffer->cursor;
	buffer->select_type = BST_NORMAL;
	//printf("Mark set @ %d,%d\n", buffer->mark_line->lineno, buffer->mark_glyph);
}

void buffer_unset_mark(buffer_t *buffer) {
	buffer->mark = buffer->savedmark = -1;
}

void buffer_change_select_type(buffer_t *buffer, enum select_type select_type) {
	buffer->select_type = select_type;
	buffer_extend_selection_by_select_type(buffer);
}

static void freeze_selection(buffer_t *buffer, selection_t *selection, int start, int end) {
	selection->start = start;
	selection->end = end;
	selection->text = buffer_lines_to_text(buffer, start, end);
}

my_glyph_info_t *buffer_next_glyph(buffer_t *buffer, my_glyph_info_t *glyph) {
 	int pp = glyph - buffer->buf;
	++pp;
	if (pp == buffer->gap) pp += buffer->gapsz;
	return (pp < buffer->size) ? buffer->buf + pp : NULL;
}

static void buffer_typeset_from(buffer_t *buffer, int point, bool single_line) {
	my_glyph_info_t *glyph = bat(buffer, point);
	double y = (glyph != NULL) ? glyph->y : (single_line ? buffer->ascent : buffer->line_height + (buffer->ex_height / 2));
	double x = (glyph != NULL) ? (glyph->x + glyph->x_advance) : buffer->left_margin;
	glyph = (glyph == NULL) ? bat(buffer, point+1) : buffer_next_glyph(buffer, glyph);

	while (glyph != NULL) {
		if (glyph->code == 0x20) {
			glyph->x_advance = buffer->space_advance;
		} else if (glyph->code == 0x09) {
			double size = config_intval(&(buffer->config), CFG_TAB_WIDTH) * buffer->em_advance;
			double to_next_cell = size - fmod(x - buffer->left_margin, size);
			if (to_next_cell <= buffer->space_advance/2) {
				// if it is too small jump to next cell instead
				to_next_cell += size;
			}
			glyph->x_advance = to_next_cell;
		} else if (glyph->code == '\n') {
			y += buffer->line_height;
			x = buffer->left_margin;
			glyph->x_advance = 0.0;
		}

		x += glyph->kerning_correction;

		if (!config_intval(&(buffer->config), CFG_AUTOWRAP)) {
			if (x + buffer->right_margin > buffer->rendered_width) buffer->rendered_width = x + buffer->right_margin;
		} else {
			if (x+glyph->x_advance > buffer->rendered_width - buffer->right_margin) {
				y += buffer->line_height;
				x = buffer->left_margin;
			}
		}
		glyph->x = x;
		glyph->y = y;
		x += glyph->x_advance;

		glyph = buffer_next_glyph(buffer, glyph);
	}
}

void buffer_replace_selection(buffer_t *buffer, const char *new_text) {
	if (!(buffer->editable)) return;

	buffer->modified = true;

	undo_node_t *undo_node = NULL;

	if (buffer->job == NULL) {
		undo_node = malloc(sizeof(undo_node_t));
		undo_node->tag = NULL;
		freeze_selection(buffer, &(undo_node->before_selection), MIN(buffer->mark, buffer->cursor), MAX(buffer->mark, buffer->cursor));
	}

	int start_cursor = buffer_replace_selection_ex(buffer, new_text);

	if (buffer->job == NULL) {
		freeze_selection(buffer, &(undo_node->after_selection), start_cursor, buffer->cursor);
		undo_push(&(buffer->undo), undo_node);
	}

	buffer_typeset_from(buffer, start_cursor, false);
	buffer_unset_mark(buffer);
	lexy_update_starting_at(buffer, start_cursor, false);

	if (buffer->onchange != NULL) buffer->onchange(buffer);
}

bool wordcompl_charset[0x10000];
bool wordcompl_red_charset[0x10000];

void buffer_wordcompl_init_charset(void) {
	for (uint32_t i = 0; i < 0x10000; ++i) {
		if (u_isalnum(i)) {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = true;
		} else if (i == 0x5f) { // underscore
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = true;
		} else if (i == '.') {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = true;
		} else if (i == '-') {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = false;
		} else if (i == '+') {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = false;
		} else if (i == '/') {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = false;
		} else if (i == ',') {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = false;
		} else if (i == '~') {
			wordcompl_charset[i] = true;
			wordcompl_red_charset[i] = false;
		} else {
			wordcompl_charset[i] = false;
			wordcompl_red_charset[i] = false;
		}
	}
}

static void buffer_wordcompl_update_word(buffer_t *buffer, int start, int end, critbit0_tree *c) {
	if (end - start < MINIMUM_WORDCOMPL_WORD_LEN) return;

	int allocated = end-start, cap = 0;
	char *r = malloc(allocated * sizeof(char));
	alloc_assert(r);

	for (int j = 0; j < end-start; ++j) {
		utf32_to_utf8(bat(buffer, j+start)->code, &r, &cap, &allocated);
	}

	utf32_to_utf8(0, &r, &cap, &allocated);

	critbit0_insert(c, r);
	free(r);
}

void buffer_wordcompl_update(buffer_t *buffer, critbit0_tree *cbt, int radius) {
	critbit0_clear(cbt);

	int start = -1;
	bool first = true;
	for (int i = MAX(buffer->cursor - radius, 0); i < buffer->cursor+radius; ++i) {
		my_glyph_info_t *glyph = bat(buffer, i);
		if (glyph == NULL) break;

		if (start < 0) {
			if (glyph->code > 0x10000) continue;
			if (wordcompl_red_charset[glyph->code]) start = i;
		} else {
			if ((glyph->code >= 0x10000) || !wordcompl_red_charset[glyph->code]) {
				if (!first) {
					buffer_wordcompl_update_word(buffer, start, i, cbt);
					start = -1;
				} else {
					first = false;
				}
			}
		}
	}

	word_completer_full_update();
}

char *buffer_lines_to_text(buffer_t *buffer, int start, int end) {
	int allocated = 0;
	int cap = 0;
	char *r = NULL;

	allocated = 10;
	r = malloc(sizeof(char) * allocated);

	for (int i = start+1; i < end; ++i) {
		my_glyph_info_t *glyph = bat(buffer, i);
		if (glyph == NULL) break;
		utf32_to_utf8(glyph->code, &r, &cap, &allocated);
	}

	if (cap >= allocated) {
		allocated *= 2;
		r = realloc(r, sizeof(char)*allocated);
	}
	r[cap++] = '\0';

	return r;
}

void buffer_extend_selection_by_select_type(buffer_t *buffer) {
/*	if (buffer->select_type == BST_NORMAL) return;
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
	}*/
}

void line_get_glyph_coordinates(buffer_t *buffer, int point, double *x, double *y) {
	my_glyph_info_t *glyph = bat(buffer, point);
	if (glyph == NULL) {
		*y = 0.0;
		*x = 0.0;
		return;
	}

	*y = glyph->y;
	*x = glyph->x;
}

char *buffer_get_selection_text(buffer_t *buffer) {
	int start, end;
	buffer_get_selection(buffer, &start, &end);
	if (start < 0) return NULL;
	if (end < 0) return NULL;
	return buffer_lines_to_text(buffer, start, end);
}

void buffer_get_selection_pointers(buffer_t *buffer, int **start, int **end) {
	if (buffer->mark < 0) {
		*start = NULL;
		*end = NULL;
		return;
	}

	if (buffer->mark < buffer->cursor) {
		*start = &(buffer->mark);
		*end = &(buffer->cursor);
	} else {
		*start = &(buffer->cursor);
		*end = &(buffer->mark);
	}
}

void buffer_get_selection(buffer_t *buffer, int *start, int *end) {
	int *pstart, *pend;
	buffer_get_selection_pointers(buffer, &pstart, &pend);
	if ((pstart == NULL) || (pend == NULL)) {
		*start = -1;
		*end = -1;
	} else {
		*start = *pstart;
		*end = *pend;
	}
}

void buffer_typeset_maybe(buffer_t *buffer, double width, bool single_line, bool force) {
	if (buffer == NULL) return;

	if (!force) {
		if (fabs(width - buffer->rendered_width) < 0.001) {
			return;
		}
		buffer->rendered_width = width;
	}

	buffer_typeset_from(buffer, -1, single_line);
}

void buffer_undo(buffer_t *buffer, bool redo) {
	//TODO
}

bool buffer_move_point_line(buffer_t *buffer, int *p, enum movement_type_t type, int arg) {
	return false;
/*	//printf("Move point line: %d (%d)\n", arg, type);

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

	return r;*/
}

bool buffer_move_point_glyph(buffer_t *buffer, int *p, enum movement_type_t type, int arg) {
	return false;
/*	if (p->line == NULL) return false;

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

	return r;*/
}

char *buffer_indent_newline(buffer_t *buffer) {
	return strdup("\n");
	/*
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
	r[i+1] = '\0';*/
}

void buffer_point_from_position(buffer_t *buffer, double x, double y, int *p) {
	*p = -1;
/*	real_line_t *line, *prev = NULL;
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

	p->line = line; p->glyph = glyph;*/
}

void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y) {
	buffer_point_from_position(buffer, x, y, &(buffer->cursor));
	buffer_extend_selection_by_select_type(buffer);
}


int parmatch_find(buffer_t *buffer, int nlines) {
	return -1;
/*	match->line = NULL; match->glyph = 0;
	if ((cursor->line != NULL) && (cursor->glyph >= 0)) {
		if (parmatch_find_ex(cursor, match, OPENING_PARENTHESIS, CLOSING_PARENTHESIS, +1, nlines)) return;

		lpoint_t preceding_cursor;
		copy_lpoint(&preceding_cursor, cursor);
		--(preceding_cursor.glyph);

		if (parmatch_find_ex(&preceding_cursor, match, CLOSING_PARENTHESIS, OPENING_PARENTHESIS, -1, nlines)) return;
	}*/
}

void buffer_get_extremes(buffer_t *buffer, int *start, int *end) {
	*start = 0;
	*end = buffer->size - buffer->gapsz - 1;
}

void buffer_aux_clear(buffer_t *buffer) {
	buffer_get_extremes(buffer, &(buffer->mark), &(buffer->cursor));
	buffer_replace_selection(buffer, "");
}

static void buffer_reload_glyph_info(buffer_t *buffer) {
/*	teddy_fontset_t *font = foundry_lookup(config_strval(&(buffer->config), CFG_MAIN_FONT), true);
	for (real_line_t *line = buffer->real_line; line != NULL; line = line->next) {
		FT_UInt previous = 0;
		uint8_t previous_fontidx = 0;

		for (int i = 0; i < line->cap; ++i) {
			uint8_t fontidx = fontset_fontidx(font, line->glyph_info[i].code);
			line->glyph_info[i].glyph_index = fontset_glyph_index(font, fontidx, (line->glyph_info[i].code != 0x09) ? line->glyph_info[i].code : 0x20);
			line->glyph_info[i].kerning_correction = (previous_fontidx == fontidx) ? fontset_get_kerning(font, fontidx, previous, line->glyph_info[i].glyph_index) : 0.0;

			previous = line->glyph_info[i].glyph_index;
			previous_fontidx = line->glyph_info[i].fontidx = fontidx;

			line->glyph_info[i].x = 0.0;
			line->glyph_info[i].y = 0.0;

			line->glyph_info[i].x_advance = fontset_x_advance(font, fontidx, line->glyph_info[i].glyph_index);
		}
	}

	foundry_release(font);*/
}

void buffer_config_changed(buffer_t *buffer) {
	buffer_reload_glyph_info(buffer);
	buffer_init_font_extents(buffer);
	buffer_typeset_maybe(buffer, 0.0, false, true);
}

char *buffer_all_lines_to_text(buffer_t *buffer) {
	int start, end;
	buffer_get_extremes(buffer, &start, &end);
	return buffer_lines_to_text(buffer, start, end);
}

char *buffer_historycompl_word_at_cursor(buffer_t *buffer) {
	return NULL;
/*	if (buffer->cursor.line == NULL) return NULL;

	lpoint_t start;
	copy_lpoint(&start, &(buffer->cursor));
	start.glyph = 0;
	return buffer_lines_to_text(buffer, &start, &(buffer->cursor));*/
}

char *buffer_wordcompl_word_at_cursor(buffer_t *buffer) {
	return NULL;
/*	if (buffer->cursor.line == NULL) return NULL;

	lpoint_t start;
	copy_lpoint(&start, &(buffer->cursor));

	for (start.glyph = buffer->cursor.glyph-1; start.glyph >= 0; --(start.glyph)) {
		uint32_t code = buffer->cursor.line->glyph_info[start.glyph].code;
		if ((code >= 0x10000) || (!wordcompl_charset[code])) { break; }
	}

	++(start.glyph);

	if (start.glyph ==  buffer->cursor.glyph) return NULL;

	return buffer_lines_to_text(buffer, &start, &(buffer->cursor));*/
}

void buffer_set_onchange(buffer_t *buffer, void (*fn)(buffer_t *buffer)) {
	buffer->onchange = fn;
}
