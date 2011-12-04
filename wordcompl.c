#include "wordcompl.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <tcl.h>

#include "interp.h"

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

static uint16_t *wordcompl_get_word_at_cursor(buffer_t *buffer, size_t *prefix_len) {
	*prefix_len = 0;

	if (buffer->cursor.line == NULL) return NULL;

	int start;
	for (start = buffer->cursor.glyph-1; start > 0; --start) {
		uint32_t code = buffer->cursor.line->glyph_info[start].code;
		if ((code >= 0x10000) || (!wordcompl_charset[code])) { ++start; break; }
	}

	if (start ==  buffer->cursor.glyph) return NULL;

	*prefix_len = buffer->cursor.glyph - start;
	uint16_t *prefix = malloc(sizeof(uint16_t) * *prefix_len);
	if (prefix == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}

	return prefix;
}

static void dbg_print_u16(uint16_t *word, size_t len) {
	for (int j = 0; j < len; ++j) {
		fprintf(stderr, "%c", (word[j] < 0x7f) ? word[j] : '?');
	}
}

static int wordcompl_search_prefix_bisect(uint16_t *prefix, size_t prefix_len, wc_entry_t **wordset, size_t wordset_size) {
	if (wordset_size <= 0) return -1;
	size_t mididx = wordset_size / 2;
	wc_entry_t *mid = wordset[mididx];

	for (int i = 0; ; ++i) {
		if (i >= prefix_len) return mididx;
		if (i >= mid->len) return wordcompl_search_prefix_bisect(prefix, prefix_len, wordset+mididx+1, wordset_size-(mididx+1));

		if (prefix[i] < mid->word[i]) {
			return wordcompl_search_prefix_bisect(prefix, prefix_len, wordset, mididx);
		} else if (prefix[i] > mid->word[i]){
			return wordcompl_search_prefix_bisect(prefix, prefix_len, wordset+mididx+1, wordset_size-(mididx+1));
		}
	}
}

static wc_entry_t **wordcompl_search_prefix(uint16_t *prefix, size_t prefix_len, size_t *num_entries) {
	*num_entries = 0;

	int idx = wordcompl_search_prefix_bisect(prefix, prefix_len, wordcompl_wordset, wordcompl_wordset_cap);
	if (idx < 0) return NULL;

	fprintf(stderr, "Seed match is at len %zd score %d [", wordcompl_wordset[idx]->len, wordcompl_wordset[idx]->score);
	dbg_print_u16(wordcompl_wordset[idx]->word, wordcompl_wordset[idx]->len);

	return NULL;
}

static int wordcompl_wordset_scorecmp(wc_entry_t **ppa, wc_entry_t **ppb) {
	wc_entry_t *a = *ppa, *b = *ppb;
	return a->score - b->score;
}

void wordcompl_complete(buffer_t *buffer) {
	size_t prefix_len;
	uint16_t *prefix = wordcompl_get_word_at_cursor(buffer, &prefix_len);
	if (prefix_len == 0) return;

	size_t num_entries;
	wc_entry_t **entries = wordcompl_search_prefix(prefix, prefix_len, &num_entries);

	if (num_entries == 0) return;

	qsort(entries, num_entries, sizeof(wc_entry_t *), (int (*)(const void *, const void *))wordcompl_wordset_scorecmp);

	fprintf(stderr, "Completions for [");
	dbg_print_u16(prefix, prefix_len);
	fprintf(stderr, "]:\n");
	for (int i = 0; i < num_entries; ++i) {
		fprintf(stderr, "\tlen: %zd score %d [", entries[i]->len, entries[i]->score);
		dbg_print_u16(entries[i]->word, entries[i]->len);
		fprintf(stderr, "]\n");
	}
	//TODO: show with window

	free(prefix);
	free(entries);
}

int teddy_wordcompl_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	fprintf(stderr, "Wordset allocated: %zd cap: %zd\n", wordcompl_wordset_allocated, wordcompl_wordset_cap);
	for (int i = 0; i < wordcompl_wordset_cap; ++i) {
		wc_entry_t *e = wordcompl_wordset[i];
		fprintf(stderr, "\tscore %d, len %zd [", e->score, e->len);
		dbg_print_u16(e->word, e->len);
		fprintf(stderr, "]\n");
	}

	return TCL_OK;
}

int teddy_wordcompl_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'wordcompl' command");
		return TCL_ERROR;
	}

	wordcompl_complete(context_editor->buffer);

	return TCL_OK;
}

