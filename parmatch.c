#include "parmatch.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "point.h"
#include "buffer.h"

const char *OPENING_PARENTHESIS = "([{<";
const char *CLOSING_PARENTHESIS = ")]}>";

#define LINES_TO_CHECK_FOR_PARMATCH 50

void parmatch_init(parmatch_t *parmatch) {
	parmatch->cursor_cache.line = NULL;
	parmatch->cursor_cache.glyph = -1;

	parmatch->matched.line = NULL;
	parmatch->matched.glyph = -1;
}

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

static bool parmatch_find_ex(parmatch_t *parmatch, lpoint_t *start, const char *tomatch, const char *tofind, int direction) {
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
		if (checked_lines >= LINES_TO_CHECK_FOR_PARMATCH) break;
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
				copy_lpoint(&(parmatch->matched), &cur);
				return true;
			}
		}
	}

	return false;
}

void parmatch_find(parmatch_t *parmatch, lpoint_t *cursor) {
	copy_lpoint(&(parmatch->cursor_cache), cursor);

	if ((cursor->line != NULL) && (cursor->glyph >= 0)) {
		if (parmatch_find_ex(parmatch, cursor, OPENING_PARENTHESIS, CLOSING_PARENTHESIS, +1)) return;

		lpoint_t preceding_cursor;
		copy_lpoint(&preceding_cursor, cursor);
		--(preceding_cursor.glyph);

		if (parmatch_find_ex(parmatch, &preceding_cursor, CLOSING_PARENTHESIS, OPENING_PARENTHESIS, -1)) return;
	}

	// Nothing was found
	parmatch->matched.line = NULL;
	parmatch->matched.glyph = -1;
}

void parmatch_invalidate(parmatch_t *parmatch) {
	parmatch->cursor_cache.line = NULL;
	parmatch->matched.line = NULL;
}
