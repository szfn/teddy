#include "baux.h"

#include <unicode/uchar.h>

#include "global.h"
#include "cfg.h"
#include "buffers.h"

#define WORDCOMPL_UPDATE_RADIUS 1000
#define MINIMUM_WORDCOMPL_WORD_LEN 3

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
static void buffer_aux_wnwa_next_ex(lpoint_t *point) {
	UBool searching_alnum;
	if (point->glyph >= point->line->cap) return;

	searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

	for ( ; point->glyph < point->line->cap; ++(point->glyph)) {
		if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) break;
	}
}

/* If it is at the beginning of a word (or inside a non-word sequence) goes to the end of the previous word, if it is at the end of a word (or inside a word) goes to the beginning of the word) */
static void buffer_aux_wnwa_prev_ex(lpoint_t *point) {
	UBool searching_alnum;
	if (point->glyph <= 0) return;

	--(point->glyph);

	searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

	for ( ; point->glyph >= 0; --(point->glyph)) {
		if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) break;
	}

	++(point->glyph);
}

void buffer_move_point_line(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg) {
	//printf("Move point line: %d (%d)\n", arg, type);

	switch (type) {
	case MT_REL:
		if (p->line == NULL) return;

		while (arg < 0) {
			real_line_t *to = p->line->prev;
			if (to == NULL) break;
			p->line = to;
			++arg;
		}

		while (arg > 0) {
			real_line_t *to = p->line->next;
			if (to == NULL) break;
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
			if (p->line->lineno+1 == arg) return;
			prev = p->line;
		}
		if (p->line == NULL) p->line = prev;
		break;
	}

	default:
		quick_message("Internal error", "Internal error buffer_move_point_line");
		return;
	}

	if (p->glyph > p->line->cap) p->glyph = p->line->cap;
	if (p->glyph < 0) p->glyph = 0;
}

void buffer_move_point_glyph(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg) {
	if (p->line == NULL) return;

	//printf("Move point glyph: %d (%d)\n", arg, type);

	switch (type) {
	case MT_REL:
		p->glyph += arg;

		while (p->glyph > p->line->cap) {
			if (p->line->next == NULL) break;
			p->glyph = p->glyph - p->line->cap - 1;
			p->line = p->line->next;
		}

		while (p->glyph < 0) {
			if (p->line->prev == NULL) break;
			p->line = p->line->prev;
			p->glyph = p->line->cap + (p->glyph + 1);
		}
		break;

	case MT_RELW:
		while (arg < 0) {
			buffer_aux_wnwa_prev_ex(p);
			++arg;
		}

		while (arg > 0) {
			buffer_aux_wnwa_next_ex(p);
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
		if (arg < 0) return;
		p->glyph = arg-1;
		break;

	default:
		quick_message("Internal error", "Internal error buffer_move_point_glyph");
	}

	if (p->glyph > p->line->cap) p->glyph = p->line->cap;
	if (p->glyph < 0) p->glyph = 0;
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

void buffer_append(buffer_t *buffer, const char *msg, int length, int on_new_line) {
	char *text;

	buffer_unset_mark(buffer);

	for (; buffer->cursor.line->next != NULL; buffer->cursor.line = buffer->cursor.line->next);
	buffer->cursor.glyph = buffer->cursor.line->cap;
	//printf("buffer_append %d %d\n", buffer->cursor.glyph, buffer->cursor.line->cap);

	if (on_new_line) {
		if (buffer->cursor.glyph != 0) {
			buffer_replace_selection(buffer, "\n");
		}
	}

	text = malloc(sizeof(char) * (length + 1));
	alloc_assert(text);
	strncpy(text, msg, length);
	text[length] = '\0';

	buffer_replace_selection(buffer, text);

	free(text);
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

uint16_t *buffer_to_utf16(buffer_t *buffer, int start, size_t len) {
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

static void buffer_wordcompl_update_line(real_line_t *line, critbit0_tree *c) {
	int start = -1;
	for (int i = 0; i < line->cap; ++i) {
		if (start < 0) {
			if (line->glyph_info[i].code > 0x10000) continue;
			if (wordcompl_charset[line->glyph_info[i].code]) start = i;
		} else {
			if ((line->glyph_info[i].code >= 0x10000) || !wordcompl_charset[line->glyph_info[i].code]) {
				if (i - start >= MINIMUM_WORDCOMPL_WORD_LEN) {
					int allocated = i-start, cap = 0;
					char *r = malloc(allocated * sizeof(char));
					alloc_assert(r);

					for (int j = 0; j < i-start; ++j) {
						utf32_to_utf8(line->glyph_info[j+start].code, &r, &cap, &allocated);
					}

					utf32_to_utf8(0, &r, &cap, &allocated);

					critbit0_insert(c, r);
					free(r);
				}
				start = -1;
			}
		}
	}
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

void buffer_aux_clear(buffer_t *buffer) {
	if (buffer->real_line == NULL) return;
	buffer->mark.line = buffer->real_line;
	buffer->mark.glyph = 0;
	for (buffer->cursor.line = buffer->real_line; buffer->cursor.line->next != NULL; buffer->cursor.line = buffer->cursor.line->next);
	buffer->cursor.glyph = buffer->cursor.line->cap;
	buffer_replace_selection(buffer, "");
}

bool buffer_aux_is_directory(buffer_t *buffer) {
	if (buffer == NULL) return false;
	if (buffer->path == NULL) return false;

	return (buffer->path[strlen(buffer->path) - 1] == '/');
}

void buffer_get_extremes(buffer_t *buffer, lpoint_t *start, lpoint_t *end) {
	start->line = buffer->real_line;
	start->glyph = 0;

	for (end->line = start->line; end->line->next != NULL; end->line = end->line->next);
	end->glyph = end->line->cap;
}

void buffer_select_all(buffer_t *buffer) {
	lpoint_t start, end;
	buffer_get_extremes(buffer, &start, &end);
	copy_lpoint(&(buffer->mark), &start);
	copy_lpoint(&(buffer->cursor), &end);
}

char *buffer_all_lines_to_text(buffer_t *buffer) {
	lpoint_t start, end, savedcursor;
	buffer_get_extremes(buffer, &start, &end);

	copy_lpoint(&savedcursor, &(buffer->cursor));
	copy_lpoint(&(buffer->mark), &start);
	copy_lpoint(&(buffer->cursor), &end);
	char *buffer_text = buffer_lines_to_text(buffer, &start, &end);
	buffer->mark.line = NULL;
	copy_lpoint(&(buffer->cursor), &savedcursor);

	return buffer_text;
}
