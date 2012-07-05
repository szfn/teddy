#include "baux.h"

#include <unicode/uchar.h>

#include "global.h"
#include "cfg.h"
#include "buffers.h"

#define WORDCOMPL_UPDATE_RADIUS 1000
#define MINIMUM_WORDCOMPL_WORD_LEN 3

void buffer_aux_go_first_nonws_or_0(buffer_t *buffer) {
	int old_cursor_glyph = buffer->cursor.glyph;
	buffer_aux_go_first_nonws(buffer);
	if (old_cursor_glyph == buffer->cursor.glyph) {
		buffer->cursor.glyph = 0;
	}
}

void buffer_aux_go_first_nonws(buffer_t *buffer) {
	int i;
	for (i = 0; i < buffer->cursor.line->cap; ++i) {
		uint32_t code = buffer->cursor.line->glyph_info[i].code;
		if ((code != 0x20) && (code != 0x09)) break;
	}
	buffer->cursor.glyph = i;
}

void buffer_aux_go_end(buffer_t *buffer) {
	buffer->cursor.glyph = buffer->cursor.line->cap;
}

void buffer_aux_go_char(buffer_t *buffer, int n) {
	buffer->cursor.glyph = n;
	if (buffer->cursor.glyph > buffer->cursor.line->cap) buffer->cursor.glyph = buffer->cursor.line->cap;
	if (buffer->cursor.glyph < 0) buffer->cursor.glyph = 0;
}

void buffer_aux_go_line(buffer_t *buffer, int n) {
	real_line_t *cur, *prev;
	for (cur = buffer->real_line; cur != NULL; cur = cur->next) {
		if (cur->lineno+1 == n) {
			buffer->cursor.line = cur;
			buffer->cursor.glyph = 0;
			return;
		}
		prev = cur;
	}
	if (cur == NULL) {
		buffer->cursor.line = prev;
		buffer->cursor.glyph = 0;
	}
}

static UBool u_isalnum_or_underscore(uint32_t code) {
	return u_isalnum(code) || (code == 0x5f);
}

void buffer_aux_wnwa_next_ex(lpoint_t *point) {
	UBool searching_alnum;
	if (point->glyph >= point->line->cap) return;

	searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

	for ( ; point->glyph < point->line->cap; ++(point->glyph)) {
		if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) break;
	}
}

void buffer_aux_wnwa_next(buffer_t *buffer) {
	buffer_aux_wnwa_next_ex(&(buffer->cursor));
}

void buffer_aux_wnwa_prev_ex(lpoint_t *point) {
	UBool searching_alnum;
	if (point->glyph <= 0) return;

	--(point->glyph);

	searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

	for ( ; point->glyph >= 0; --(point->glyph)) {
		if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) break;
	}

	++(point->glyph);
}

void buffer_aux_wnwa_prev(buffer_t *buffer) {
	buffer_aux_wnwa_prev_ex(&(buffer->cursor));
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
	uint16_t *prefix = malloc(sizeof(uint16_t) * *prefix_len);
	alloc_assert(prefix);

	for (int i = 0; i < *prefix_len; ++i) prefix[i] = buffer->cursor.line->glyph_info[start+i].code;

	return prefix;
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
	if (buffer->name == NULL) return false;

	return (buffer->name[strlen(buffer->name) - 1] == '/');
}
