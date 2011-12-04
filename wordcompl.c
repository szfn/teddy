#include "wordcompl.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <tcl.h>

#include <unicode/uchar.h>

bool wordcompl_charset[0x10000];

typedef struct _wc_entry_t {
	int score;
	size_t len;
	uint16_t word[];
} wc_entry_t;

wc_entry_t **wordcompl_wordset;
size_t wordcompl_wordset_cap;
size_t wordcompl_wordset_allocated;
int wordcompl_callcount;

void wordcompl_init(void) {
	for (uint32_t i = 0; i < 0x10000; ++i) {
		if (u_isalnum(i)) {
			wordcompl_charset[i] = true;
		} else if (i == 0x5f) { // underscore
			wordcompl_charset[i] = true;
		} else {
			wordcompl_charset[i] = false;
		}
	}

	wordcompl_wordset_cap = 0;
	wordcompl_wordset_allocated = 10;
	wordcompl_wordset = malloc(wordcompl_wordset_allocated * sizeof(wc_entry_t *));
	if (wordcompl_wordset == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
	wordcompl_callcount = 0;
}

static void wordcompl_wordset_grow() {
	wordcompl_wordset_allocated *= 2;
	wordcompl_wordset = realloc(wordcompl_wordset, wordcompl_wordset_allocated * sizeof(wc_entry_t *));
}

static int wordcompl_wordset_cmp(wc_entry_t **ppa, wc_entry_t **ppb) {
	wc_entry_t *a = *ppa;
	wc_entry_t *b = *ppb;

	for (int i = 0; ; ++i) {
		if (i >= a->len) {
			if (i >= b->len) return 0;
			else return -1;
		}
		if (i >= b->len) return +1;

		int16_t d = a->word[i] - b->word[i];

		if (d != 0) return d;
	}
}

static void wordcompl_update_line(real_line_t *line) {
	int start = -1;
	for (int i = 0; i < line->cap; ++i) {
		if (start < 0) {
			if (line->glyph_info[i].code > 0x10000) continue;
			if (wordcompl_charset[line->glyph_info[i].code]) start = i;
		} else {
			if ((line->glyph_info[i].code >= 0x10000) || !wordcompl_charset[line->glyph_info[i].code]) {
				if (i - start >= MINIMUM_WORDCOMPL_WORD_LEN) {
					wc_entry_t *newentry = malloc(sizeof(wc_entry_t) + (sizeof(uint16_t) * (i - start)));
					newentry->score = 1;
					newentry->len = (i - start);
					for (int j = 0; j < newentry->len; ++j) newentry->word[j] = line->glyph_info[j+start].code;
					if (wordcompl_wordset_cap >= wordcompl_wordset_allocated) {
						wordcompl_wordset_grow();
					}
					wordcompl_wordset[wordcompl_wordset_cap] = newentry;
					wordcompl_wordset_cap++;
				}
				start = -1;
			}
		}
	}
}

void wordcompl_update(buffer_t *buffer) {
	if (buffer->cursor.line == NULL) return;
	++wordcompl_callcount;

	/* MAP */
	int count = WORDCOMPL_UPDATE_RADIUS;
	for (real_line_t *line = buffer->cursor.line; line != NULL; line = line->prev) {
		--count;
		if (count <= 0) break;

		wordcompl_update_line(line);
	}

	count = WORDCOMPL_UPDATE_RADIUS;
	for (real_line_t *line = buffer->cursor.line->next; line != NULL; line = line->next) {
		--count;
		if (count <= 0) break;

		wordcompl_update_line(line);
	}

	/* SORT */
	qsort(wordcompl_wordset, wordcompl_wordset_cap, sizeof(wc_entry_t *), (int(*)(const void *, const void *))wordcompl_wordset_cmp);

	/* REDUCE */
	int cur_word = 0;
	for (int src = 1; src < wordcompl_wordset_cap; ++src) {
		if (wordcompl_wordset_cmp(wordcompl_wordset+cur_word, wordcompl_wordset+src) == 0) {
			wordcompl_wordset[cur_word]->score += wordcompl_wordset[src]->score;
			free(wordcompl_wordset[src]);
			wordcompl_wordset[src]  = NULL;
		} else {
			if (wordcompl_callcount % WORDCOMPL_CALLCOUNT_TRIGGER == 0) {
				if (wordcompl_wordset[cur_word]->score > WORDCOMPL_CLEANUP_MAX) {
					++cur_word;
					wordcompl_wordset[cur_word] = wordcompl_wordset[src];
				} else {
					// time to cleanup and this word doesn't have a high enough score
					free(wordcompl_wordset[cur_word]);
					wordcompl_wordset[cur_word] = wordcompl_wordset[src];
				}
			} else {
				++cur_word;
				wordcompl_wordset[cur_word] = wordcompl_wordset[src];
			}
		}
	}

	wordcompl_wordset_cap = cur_word+1;
}

void wordcompl_complete(buffer_t *buffer) {
	//TODO:
	// - search possible completions
	// - order them by their counter
	// - show with window
}

int teddy_wordcompl_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	fprintf(stderr, "Wordset allocated: %zd cap: %zd\n", wordcompl_wordset_allocated, wordcompl_wordset_cap);
	for (int i = 0; i < wordcompl_wordset_cap; ++i) {
		wc_entry_t *e = wordcompl_wordset[i];
		fprintf(stderr, "\tscore %d, len %zd [", e->score, e->len);
		for (int j = 0; j < e->len; ++j) {
			fprintf(stderr, "%c", (e->word[j] < 0x7f) ? e->word[j] : '?');
		}
		fprintf(stderr, "]\n");
	}

	return TCL_OK;
}
