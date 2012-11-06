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
		int size = buffer->gap - pp;
		if (size > 0) memmove(buffer->buf + pp + buffer->gapsz, buffer->buf + pp, sizeof(my_glyph_info_t) * size);
		buffer->gap = pp;
	} else if (pp > buffer->gap) {
		int size = pp - buffer->gap - buffer->gapsz;
		if (size > 0) memmove(buffer->buf + buffer->gap, buffer->buf + buffer->gap + buffer->gapsz, sizeof(my_glyph_info_t) * size);
		buffer->gap = pp - buffer->gapsz;
	}
}

static void regap(buffer_t *buffer, bool twice) {
	/*printf("before regap: ");
	gb_debug_print(buffer);*/

	int slop = twice ? buffer->size : SLOP;

	my_glyph_info_t *newbuf = malloc(sizeof(my_glyph_info_t) * (buffer->size+slop));
	alloc_assert(newbuf);

	memmove(newbuf, buffer->buf, sizeof(my_glyph_info_t) * buffer->gap);
	memmove(newbuf+buffer->gap+slop, buffer->buf + buffer->gap, sizeof(my_glyph_info_t) * (buffer->size - buffer->gap));

	buffer->gapsz = slop;
	buffer->size += slop;

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
	buffer->single_line = false;

	asprintf(&(buffer->path), "+unnamed");
	alloc_assert(buffer->path);
	buffer->has_filename = 0;
	buffer->select_type = BST_NORMAL;

	buffer->lexy_last_update_point = -1;

	undo_init(&(buffer->undo));

	buffer_init_font_extents(buffer);

	buffer->buf = malloc(sizeof(my_glyph_info_t) * SLOP);
	alloc_assert(buffer->buf);
	buffer->size = SLOP;
	buffer->cursor = 0;
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

static int buffer_replace_selection_ex(buffer_t *buffer, const char *text, bool twice) {
	teddy_fontset_t *font = foundry_lookup(config_strval(&(buffer->config), CFG_MAIN_FONT), true);

	// there is a mark, delete
	if (buffer->mark >= 0) {
		//printf("bla! %d %d <%s>\n", buffer->mark, buffer->cursor, text);
		int region_size = MAX(buffer->mark, buffer->cursor) - MIN(buffer->mark, buffer->cursor);
		movegap(buffer, MIN(buffer->mark, buffer->cursor));
		buffer->gapsz += region_size;
		buffer->cursor = MIN(buffer->mark, buffer->cursor);
		buffer->mark = -1;
	} else {
		//printf("Calling movegap: %d\n", buffer->cursor);
		movegap(buffer, buffer->cursor);
	}

	int start_cursor = buffer->cursor;

	int len = strlen(text);
	for (int i = 0; i < len; ) {
		if (buffer->gapsz <= 0) regap(buffer, twice);

		bool valid = false;
		uint32_t code = utf8_to_utf32(text, &i, len, &valid);

		buffer->buf[buffer->gap].code = code;

		uint8_t fontidx = fontset_fontidx(font, code);
		FT_UInt glyph_index = fontset_glyph_index(font, fontidx, ((code != 0x09) && (code != '\n')) ? code : 0x20);

		buffer->buf[buffer->gap].code = code;
		buffer->buf[buffer->gap].color = buffer->default_color;
		buffer->buf[buffer->gap].status = 0xffff;

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

#define BUFSIZE 1024
	char text[BUFSIZE + 12];
	while (!feof(fin)) {
		int n = fread(text, sizeof(char), BUFSIZE, fin);
		if (n == 0) continue;

		int charlen = utf8_first_byte_processing(text[n-1]);

		if (charlen < 8) {
			int i;
			for (i = 0; i < charlen; ++i) {
				text[n+i] = fgetc(fin);
			}
			n += i;
		}

		text[n] = '\0';

		buffer_replace_selection_ex(buffer, text, true);
	}

	buffer->cursor = 0;

	fclose(fin);

	buffer_wordcompl_update(buffer, &(buffer->cbt), WORDCOMPL_UPDATE_RADIUS);
	//printf("Calling lexy update\n");
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
		r = buffer_lines_to_text(buffer, 0, BSIZE(buffer));
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
	if (selection->start < 0) selection->start = end;
	selection->text = buffer_lines_to_text(buffer, start, end);
	if (selection->text == NULL) {
		selection->text = strdup("");
		alloc_assert(selection->text);
	}
}

static void buffer_typeset_from(buffer_t *buffer, int point) {
	my_glyph_info_t *glyph = bat(buffer, point);

	double y, x;
	if (glyph != NULL) {
		y = glyph->y;
		x = glyph->x + glyph->x_advance;
		if (glyph->code == '\n') {
			y += buffer->line_height;
			x = buffer->left_margin;
		}
	} else {
		y = buffer->single_line ? buffer->ascent : buffer->line_height + (buffer->ex_height / 2);
		x = buffer->left_margin;
	}


	for (int i = point+1; i < BSIZE(buffer); ++i) {
		glyph = bat(buffer, i);
		if (glyph->code == 0x20) {
			if (i == 0) {
				glyph->x_advance = buffer->em_advance;
			} else {
				my_glyph_info_t *prev = bat(buffer, i - 1);
				if (prev == NULL) {
					glyph->x_advance = buffer->space_advance;
				} else if (prev->code == '\t') {
					glyph->x_advance = buffer->em_advance;
				} else if (prev->code == '\n') {
					glyph->x_advance = buffer->em_advance;
				} else if (prev->code == ' ') {
					glyph->x_advance = prev->x_advance;
				} else {
					glyph->x_advance = buffer->space_advance;
				}
			}
		} else if (glyph->code == 0x09) {
			double size = config_intval(&(buffer->config), CFG_TAB_WIDTH) * buffer->em_advance;
			double to_next_cell = size - fmod(x - buffer->left_margin, size);
			if (to_next_cell <= buffer->space_advance/2) {
				// if it is too small jump to next cell instead
				to_next_cell += size;
			}
			glyph->x_advance = to_next_cell;
		} else if (glyph->code == '\n') {
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

		if (glyph->code == '\n') {
			y += buffer->line_height;
			x = buffer->left_margin;
		}

		buffer->rendered_height = y;
	}
}

void buffer_undo(buffer_t *buffer, bool redo) {
	if (!(buffer->editable)) return;
	if (buffer->job != NULL) return;

	undo_node_t *undo_node = redo ? undo_redo_pop(&(buffer->undo)) : undo_pop(&(buffer->undo));
	if (undo_node == NULL) return;

	buffer->modified = 1;

	buffer_unset_mark(buffer);

	int start, end;

	if (redo) {
		start = undo_node->before_selection.start;
		end = undo_node->before_selection.end;
	} else {
		start = undo_node->after_selection.start;
		end = undo_node->after_selection.end;
	}

	buffer->mark = start;
	buffer->cursor = end;
	int start_cursor = buffer_replace_selection_ex(buffer, redo ? undo_node->after_selection.text : undo_node->before_selection.text, false);

	buffer_typeset_from(buffer, start_cursor-1);
	buffer_unset_mark(buffer);
	lexy_update_starting_at(buffer, start_cursor-1, false);

	if (buffer->onchange != NULL) buffer->onchange(buffer);
}

void buffer_replace_selection(buffer_t *buffer, const char *new_text) {
	if (!(buffer->editable)) return;

	//printf("buffer replace selection: <%s>\n", buffer->path);

	buffer->modified = true;

	undo_node_t *undo_node = NULL;

	if (buffer->job == NULL) {
		undo_node = malloc(sizeof(undo_node_t));
		undo_node->tag = NULL;
		freeze_selection(buffer, &(undo_node->before_selection), MIN(buffer->mark, buffer->cursor), MAX(buffer->mark, buffer->cursor));
	}

	int start_cursor = buffer_replace_selection_ex(buffer, new_text, false);

	if (buffer->job == NULL) {
		freeze_selection(buffer, &(undo_node->after_selection), start_cursor, buffer->cursor);
		undo_push(&(buffer->undo), undo_node);
	}

	buffer_typeset_from(buffer, start_cursor-1);
	buffer_unset_mark(buffer);

	//printf("\tcalling lexy update\n");
	lexy_update_starting_at(buffer, start_cursor-1, false);

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

	if (start < 0) return NULL;
	if (end < 0) return NULL;

	allocated = 10;
	r = malloc(sizeof(char) * allocated);

	for (int i = start; i < end; ++i) {
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

void line_get_glyph_coordinates(buffer_t *buffer, int point, double *x, double *y) {
	my_glyph_info_t *glyph = bat(buffer, point);
	if (glyph == NULL) {
		glyph = bat(buffer, point-1);
		if (glyph == NULL) { // either the point is corrupted or there is nothing in this buffer
			*y = buffer->single_line ? buffer->ascent : (buffer->line_height + (buffer->ex_height/2.0));
			*x = buffer->left_margin;
		} else { // after the last character of the buffer
			if (glyph->code == '\n') {
				*y = glyph->y + buffer->line_height;
				*x = buffer->left_margin;
			} else {
				*y = glyph->y;
				*x = glyph->x + glyph->x_advance;
			}
		}

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

static void buffer_get_selection_pointers(buffer_t *buffer, int **start, int **end) {
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

void buffer_extend_selection_by_select_type(buffer_t *buffer) {
	if (buffer->select_type == BST_NORMAL) return;
	if (buffer->mark < 0) return;
	if (buffer->savedmark < 0) return;
	if (buffer->cursor < 0) return;

	buffer->mark = buffer->savedmark;

	int *start, *end;
	buffer_get_selection_pointers(buffer, &start, &end);

	switch (buffer->select_type) {
	case BST_LINES:
		buffer_move_point_glyph(buffer, start, MT_ABS, 1);
		buffer_move_point_glyph(buffer, end, MT_END, 0);
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

void buffer_typeset_maybe(buffer_t *buffer, double width, bool force) {
	if (buffer == NULL) return;

	if (!force) {
		if (fabs(width - buffer->rendered_width) < 0.001) {
			return;
		}
		buffer->rendered_width = width;
	}

	buffer_typeset_from(buffer, -1);
}

static bool buffer_aux_findchar(buffer_t *buffer, int *p, uint32_t k, int dir) {
	for (int i = *p; (i >= 0) && (i < BSIZE(buffer)); i += dir) {
		if (bat(buffer, i)->code == k) {
			*p = i;
			return true;
		}
	}

	return false;
}

bool buffer_move_point_line(buffer_t *buffer, int *p, enum movement_type_t type, int arg) {
	//printf("Move point line: %d (%d)\n", arg, type);

	bool r = true;

	switch (type) {
	case MT_REL:
		if (arg < 0) {
			--arg;
			for (; r && (arg < 0); ++arg) {
				--(*p);
				r = buffer_aux_findchar(buffer, p, '\n', -1);
			}
			if (!r) *p = -1;
			++(*p);

		} else if (arg > 0) {
			buffer_aux_findchar(buffer, p, '\n', +1);
			for (; r && (arg > 0); --arg) {
				r = buffer_aux_findchar(buffer, p, '\n', +1);
				++(*p);
			}
			if (!r) *p = BSIZE(buffer);
		}
		break;

	case MT_END:
		*p = BSIZE(buffer);
		--(*p);
		r = buffer_aux_findchar(buffer, p, '\n', -1);
		if (!r) *p = -1;
		++(*p);
		break;

	case MT_ABS:
		*p = 0;
		for (; r && (arg > 1); --arg) {
			r = buffer_aux_findchar(buffer, p, '\n', +1);
			++(*p);
		}
		if (!r) *p = BSIZE(buffer);
		break;

	default:
		quick_message("Internal error", "Internal error buffer_move_point_line");
		r = false;
	}

	if (*p < 0) *p = 0;
	if (*p > BSIZE(buffer)) *p = BSIZE(buffer);

	return r;
}

static UBool u_isalnum_or_underscore(uint32_t code) {
	return u_isalnum(code) || (code == 0x5f);
}

/*if it is at the beginning of a word (or inside a word) goes to the end of this word, if it is at the end of a word (or inside a non-word sequence) goes to the beginning of the next one*/
static bool buffer_aux_wnwa_next_ex(buffer_t *buffer, int *point) {
	if (*point >= BSIZE(buffer)) return false;
	if (*point < 0) return false;

	UBool searching_alnum = !u_isalnum_or_underscore(bat(buffer, *point)->code);

	for ( ; *point < BSIZE(buffer); ++(*point)) {
		uint32_t code = bat(buffer, *point)->code;
		if ((code == '\n') || (u_isalnum_or_underscore(bat(buffer, *point)->code) == searching_alnum)) return true;
	}

	return false;
}

/* If it is at the beginning of a word (or inside a non-word sequence) goes to the end of the previous word, if it is at the end of a word (or inside a word) goes to the beginning of the word) */
static bool buffer_aux_wnwa_prev_ex(buffer_t *buffer, int *point) {
	if (*point <= 0) return false;
	if (*point > BSIZE(buffer)) return false;

	--(*point);

	UBool searching_alnum = !u_isalnum_or_underscore(bat(buffer, *point)->code);

	for ( ; *point >= 0; --(*point)) {
		uint32_t code = bat(buffer, *point)->code;
		if ((code == '\n') || (u_isalnum_or_underscore(code) == searching_alnum)) {
			++(*point);
			return true;
		}
	}

	++(*point);
	return false;
}

static bool buffer_aux_go_first_nonws(buffer_t *buffer, int *p, bool alternate) {
	int startp = *p;
	--(*p);
	if (!buffer_aux_findchar(buffer, p, '\n', -1)) {
		*p = -1;
	}
	++(*p);
	int beginning = *p;
	while (*p < BSIZE(buffer) && u_isspace(bat(buffer, *p)->code) && (bat(buffer, *p)->code != '\n')) { ++(*p); }

	if (alternate && (*p == startp)) {
		*p = beginning;
	}

	return true;
}

bool buffer_move_point_glyph(buffer_t *buffer, int *p, enum movement_type_t type, int arg) {
	if (*p < 0) return false;

	bool r = true;

	switch (type) {
	case MT_REL:
		*p += arg;
		break;

	case MT_RELW:
		while (arg < 0) {
			r = buffer_aux_wnwa_prev_ex(buffer, p);
			if (!r) break;
			++arg;
		}

		while (arg > 0) {
			r = buffer_aux_wnwa_next_ex(buffer, p);
			if (!r) break;
			--arg;
		}
		break;

	case MT_END:
		if (!buffer_aux_findchar(buffer, p, '\n', +1)) {
			*p = BSIZE(buffer);
		}
		break;

	case MT_START:
		r = buffer_aux_go_first_nonws(buffer, p, false);
		break;

	case MT_HOME:
		r = buffer_aux_go_first_nonws(buffer, p, true);
		break;

	case MT_ABS:
		if (arg >= 1) {
			--(*p);
			if (!buffer_aux_findchar(buffer, p, '\n', -1)) *p = -1;
			*p += arg;
		}
		break;

	}

	if (*p < 0) *p = 0;
	if (*p > BSIZE(buffer)) *p = BSIZE(buffer);

	return r;
}

char *buffer_indent_newline(buffer_t *buffer) {
	int start = buffer->cursor;
	buffer_move_point_glyph(buffer, &start, MT_ABS, 1);
	int end = start;
	buffer_move_point_glyph(buffer, &end, MT_START, 0);
	char *r = buffer_lines_to_text(buffer, start, end);
	char *rr;
	asprintf(&rr, "\n%s", r);
	alloc_assert(rr);
	free(r);
	return rr;
}

int buffer_point_from_position(buffer_t *buffer, double x, double y) {
	int p = buffer->size - buffer->gapsz;

	for (int i = 0; i < buffer->size-buffer->gapsz; ++i) {
		my_glyph_info_t *g = bat(buffer, i);

		if (g->y < y) continue;
		if (g->y - buffer->line_height > y) {
			p = i;
			break;
		}

		//printf("\tin line: %g < %g < %g\n", g->y - buffer->line_height, y, g->y);

		double glyph_start = g->x;
		double glyph_end = glyph_start + g->x_advance;

		if (x < glyph_start) {
			p = i;
			break;
		}

		if (g->code == '\n') {
			p = i;
			break;
		}

		if ((x >= glyph_start) && (x <= glyph_end)) {
			double dist_start = x - glyph_start;
			double dist_end = glyph_end - x;
			if (dist_start < dist_end) {
				p = i;
			} else {
				p = i+1;
			}
			break;
		}
	}

	return p;
}

void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y) {
	buffer->cursor = buffer_point_from_position(buffer, x, y);
	//printf("Cursor on: (%d) %04x\n", buffer->cursor, bat(buffer, buffer->cursor) != NULL ? bat(buffer, buffer->cursor)->code : 0);
	buffer_extend_selection_by_select_type(buffer);
}

static uint32_t point_to_char_to_find(uint32_t code, const char *tomatch, const char *tofind) {
	if (code > 0x7f) return 0;

	for (int i = 0; i < strlen(tomatch); ++i) {
		if (tomatch[i] == (char)code) {
			return tofind[i];
		}
	}

	return 0;
}

static int parmatch_find_ex(buffer_t  *buffer, int start, const char *tomatch, const char *tofind, int direction, int nlines) {
#define PARMATCH_CHAR_LIMIT 1000
	if (start < 0) return -1;
	if (start >= BSIZE(buffer)) return -1;

	uint32_t cursor_code = bat(buffer, start)->code;
	uint32_t match_code = point_to_char_to_find(cursor_code, tomatch, tofind);
	if (match_code <= 0) return -1;

	//printf("\tcode: %c searching: %c direction: %d\n", (char)cursor_code, (char)match_code, direction);

	int depth = 1;
	int count = 0;

	for (int i = start+direction; (i >= 0) && (i < BSIZE(buffer)); i += direction) {
		my_glyph_info_t *cur = bat(buffer, i);
		if (cur == NULL) return -1;
		if (cur->code == '\n') --nlines;
		if (nlines < 0) return -1;
		if (count++ > PARMATCH_CHAR_LIMIT) return -1;

		if (cur->code == cursor_code) ++depth;
		if (cur->code == match_code) --depth;

		//printf("\%d tdepth: %d (%c)\n", i, depth, cur->code);

		if (depth == 0) return i;
	}

	return -1;
}

int parmatch_find(buffer_t *buffer, int nlines) {
#define OPENING_PARENTHESIS "([{<"
#define CLOSING_PARENTHESIS ")]}>"
	//printf("parmatch_find called\n");
	if (buffer->cursor < 0) return -1;

	int r = parmatch_find_ex(buffer, buffer->cursor, OPENING_PARENTHESIS, CLOSING_PARENTHESIS, +1, nlines);
	if (r >= 0) return r;

	r = parmatch_find_ex(buffer, buffer->cursor-1, CLOSING_PARENTHESIS, OPENING_PARENTHESIS, -1, nlines);
	return r;
}

void buffer_get_extremes(buffer_t *buffer, int *start, int *end) {
	*start = 0;
	*end = buffer->size - buffer->gapsz;
}

void buffer_aux_clear(buffer_t *buffer) {
	buffer_get_extremes(buffer, &(buffer->mark), &(buffer->cursor));
	buffer_replace_selection(buffer, "");
}

static void buffer_reload_glyph_info(buffer_t *buffer) {
	teddy_fontset_t *font = foundry_lookup(config_strval(&(buffer->config), CFG_MAIN_FONT), true);
	FT_UInt previous = 0;
	uint8_t previous_fontidx = 0;
	for (int i = 0; i < BSIZE(buffer); ++i) {
		my_glyph_info_t *g = bat(buffer, i);

		uint8_t fontidx = fontset_fontidx(font, g->code);
		g->glyph_index = fontset_glyph_index(font, fontidx, ((g->code != 0x09) && (g->code != 0x0a)) ? g->code : 0x20);
		g->kerning_correction = (previous_fontidx == fontidx) ? fontset_get_kerning(font, fontidx, previous, g->glyph_index) : 0.0;

		previous = g->glyph_index;
		previous_fontidx = g->fontidx = fontidx;

		g->x = 0.0;
		g->y = 0.0;

		g->x_advance = fontset_x_advance(font, fontidx, g->glyph_index);
	}
	foundry_release(font);
}

void buffer_config_changed(buffer_t *buffer) {
	buffer_reload_glyph_info(buffer);
	buffer_init_font_extents(buffer);
	buffer_typeset_maybe(buffer, 0.0, true);
}

char *buffer_all_lines_to_text(buffer_t *buffer) {
	int start, end;
	buffer_get_extremes(buffer, &start, &end);
	return buffer_lines_to_text(buffer, start, end);
}

char *buffer_historycompl_word_at_cursor(buffer_t *buffer) {
	int start = buffer->cursor;
	buffer_move_point_glyph(buffer, &start, MT_ABS, 1);
	return buffer_lines_to_text(buffer, start, buffer->cursor);
}

char *buffer_wordcompl_word_at_cursor(buffer_t *buffer) {
	if (buffer->cursor < 0) return NULL;

	int start;
	for (start = buffer->cursor-1; start >= 0; --start) {
		my_glyph_info_t *glyph = bat(buffer, start);
		if ((glyph == NULL) || (glyph->code >= 0x10000) || (!wordcompl_charset[glyph->code])) { break; }
	}

	++start;
	return buffer_lines_to_text(buffer, start, buffer->cursor);
}

void buffer_set_onchange(buffer_t *buffer, void (*fn)(buffer_t *buffer)) {
	buffer->onchange = fn;
}

int buffer_line_of(buffer_t *buffer, int p) {
	int c = 0;
	int line = 0;
	while (c <= p) {
		bool r = buffer_move_point_line(buffer, &c, MT_REL, +1);
		++line;
		if (!r) break;
	}
	//printf("line_of called: (%d) %d\n", p, line);
	return line;
}

int buffer_column_of(buffer_t *buffer, int p) {
	int c = p;
	buffer_move_point_glyph(buffer, &c, MT_ABS, 1);
	//printf("column of called: (%d) %d\n", p, (p - c + 1));
	return p - c + 1;
}
