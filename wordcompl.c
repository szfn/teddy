#include "wordcompl.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <tcl.h>

#include "global.h"
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
bool wordcompl_visible = false;

GtkListStore *wordcompl_list;
GtkWidget *wordcompl_tree;
GtkWidget *wordcompl_window;

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

	wordcompl_list = gtk_list_store_new(1, G_TYPE_STRING);
	wordcompl_tree = gtk_tree_view_new();

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(wordcompl_tree), -1, "Completion", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(wordcompl_tree), GTK_TREE_MODEL(wordcompl_list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(wordcompl_tree), FALSE);

	wordcompl_window = gtk_window_new(GTK_WINDOW_POPUP);

	gtk_window_set_decorated(GTK_WINDOW(wordcompl_window), FALSE);

	g_signal_connect(G_OBJECT(wordcompl_window), "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

	{
		GtkWidget *frame = gtk_table_new(0, 0, FALSE);

		gtk_container_add(GTK_CONTAINER(wordcompl_window), frame);

		place_frame_piece(frame, TRUE, 0, 3); // top frame
		place_frame_piece(frame, FALSE, 0, 3); // left frame
		place_frame_piece(frame, FALSE, 2, 3); // right frame
		place_frame_piece(frame, TRUE, 2, 3); // bottom frame

		GtkWidget *scroll_view = gtk_scrolled_window_new(NULL, NULL);

		gtk_container_add(GTK_CONTAINER(scroll_view), wordcompl_tree);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_view), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		gtk_table_attach(GTK_TABLE(frame), scroll_view, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	}

	gtk_window_set_default_size(GTK_WINDOW(wordcompl_window), -1, 150);
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
	for (start = buffer->cursor.glyph-1; start >= 0; --start) {
		uint32_t code = buffer->cursor.line->glyph_info[start].code;
		if ((code >= 0x10000) || (!wordcompl_charset[code])) { break; }
	}

	++start;

	if (start ==  buffer->cursor.glyph) return NULL;

	*prefix_len = buffer->cursor.glyph - start;
	uint16_t *prefix = malloc(sizeof(uint16_t) * *prefix_len);
	if (prefix == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < *prefix_len; ++i) prefix[i] = buffer->cursor.line->glyph_info[start+i].code;

	return prefix;
}

static void dbg_print_u16(uint16_t *word, size_t len) {
	for (int j = 0; j < len; ++j) {
		fprintf(stderr, "%c", (word[j] < 0x7f) ? word[j] : '?');
	}
}

static int wordcompl_wordset_prefixcmp(uint16_t *prefix, size_t prefix_len, wc_entry_t *entry) {
	for (int i = 0; ; ++i) {
		if (i >= prefix_len) return 0;
		if (i >= entry->len) return +1;

		//fprintf(stderr, "\tComparing %c %c\n", (char)prefix[i], (char)(entry->word[i]));

		if (prefix[i] < entry->word[i]) {
			return -1;
		} else if (prefix[i] > entry->word[i]) {
			return +1;
		}
	}
}

static int wordcompl_search_prefix_bisect(uint16_t *prefix, size_t prefix_len, wc_entry_t **wordset, size_t wordset_size) {
	if (wordset_size <= 0) return -1;
	size_t mididx = wordset_size / 2;
	wc_entry_t *mid = wordset[mididx];

	//fprintf(stderr, "Wordcompl bisect size: %zd\n", wordset_size);

	int r = wordcompl_wordset_prefixcmp(prefix, prefix_len, mid);

	if (r == 0) return (wordset + mididx - wordcompl_wordset);
	else if (r < 0) return wordcompl_search_prefix_bisect(prefix, prefix_len, wordset, mididx);
	else if (r > 0) return wordcompl_search_prefix_bisect(prefix, prefix_len, wordset+mididx+1, wordset_size-(mididx+1));

	return -1; // this is impossible
}

static wc_entry_t **wordcompl_search_prefix(uint16_t *prefix, size_t prefix_len, size_t *num_entries) {
	*num_entries = 0;

	int idx = wordcompl_search_prefix_bisect(prefix, prefix_len, wordcompl_wordset, wordcompl_wordset_cap);
	if (idx < 0) return NULL;

	/*fprintf(stderr, "Seed match is at len %zd score %d [", wordcompl_wordset[idx]->len, wordcompl_wordset[idx]->score);
	dbg_print_u16(wordcompl_wordset[idx]->word, wordcompl_wordset[idx]->len);
	fprintf(stderr, "]\n");*/

	int start;
	for (start = idx-1; start >= 0; --start) {
		if (wordcompl_wordset_prefixcmp(prefix, prefix_len, wordcompl_wordset[start]) != 0) break;
	}
	++start;

	int end;
	for (end = idx+1; end < wordcompl_wordset_cap; ++end) {
		if (wordcompl_wordset_prefixcmp(prefix, prefix_len, wordcompl_wordset[end]) != 0) break;
	}

	*num_entries = end - start;
	wc_entry_t **r = malloc(sizeof(wc_entry_t *) * *num_entries);
	if (!r) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < *num_entries; ++i) {
		r[i] = wordcompl_wordset[i + start];
	}

	return r;
}

static int wordcompl_wordset_scorecmp(wc_entry_t **ppa, wc_entry_t **ppb) {
	wc_entry_t *a = *ppa, *b = *ppb;
	return a->score - b->score;
}

bool wordcompl_complete(editor_t *editor) {
	size_t prefix_len;

	uint16_t *prefix = wordcompl_get_word_at_cursor(editor->buffer, &prefix_len);
	if (prefix_len == 0) return false;

	fprintf(stderr, "Searching prefix [");
	dbg_print_u16(prefix, prefix_len);
	fprintf(stderr, "]\n");

	size_t num_entries;
	wc_entry_t **entries = wordcompl_search_prefix(prefix, prefix_len, &num_entries);

	if (num_entries == 0) return true;

	qsort(entries, num_entries, sizeof(wc_entry_t *), (int (*)(const void *, const void *))wordcompl_wordset_scorecmp);

	/*
	fprintf(stderr, "Completions for [");
	dbg_print_u16(prefix, prefix_len);
	fprintf(stderr, "]:\n");
	for (int i = 0; i < num_entries; ++i) {
		fprintf(stderr, "\tlen: %zd score %d [", entries[i]->len, entries[i]->score);
		dbg_print_u16(entries[i]->word, entries[i]->len);
		fprintf(stderr, "]\n");
	}*/

	gtk_list_store_clear(wordcompl_list);

	{
		int allocated = 10;
		int cap = 0;
		char *r = malloc(allocated * sizeof(char));
		if (!r) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < num_entries; ++i) {
			for (int j = 0; j < entries[i]->len; ++j) {
				utf32_to_utf8(entries[i]->word[j], &r, &cap, &allocated);
			}

			if (cap >= allocated) {
				allocated *= 2;
				r = realloc(r, sizeof(char) * allocated);
				if (!r) {
					perror("Out of memory");
					exit(EXIT_FAILURE);
				}
			}
			r[cap++] = '\0';

			GtkTreeIter mah;
			gtk_list_store_append(wordcompl_list, &mah);
			gtk_list_store_set(wordcompl_list, &mah, 0, r, -1);
		}

		free(r);
	}

	gtk_window_set_transient_for(GTK_WINDOW(wordcompl_window), GTK_WINDOW(editor->window));

	{
		double x, y;
		buffer_cursor_position(editor->buffer, &x, &y);
		y -= gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

		printf("x = %g y = %g\n", x, y);

		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		x += allocation.x; y += allocation.y;

		gint wpos_x, wpos_y;
		gdk_window_get_position(gtk_widget_get_window(editor->window), &wpos_x, &wpos_y);
		x += wpos_x; y += wpos_y;

		x += 2; y += 2;

		gtk_widget_set_uposition(wordcompl_window, x, y);
	}

	{
		GtkTreePath *path_to_first = gtk_tree_path_new_first();
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(wordcompl_tree), path_to_first, gtk_tree_view_get_column(GTK_TREE_VIEW(wordcompl_tree), 0), FALSE);
		gtk_tree_path_free(path_to_first);
	}

	gtk_widget_show_all(wordcompl_window);
	wordcompl_visible = true;

	free(prefix);
	free(entries);

	return true;
}

void wordcompl_stop(void) {
	gtk_widget_hide(wordcompl_window);
	wordcompl_visible = false;
}

bool wordcompl_iscompleting(void) {
	return wordcompl_visible;
}

void wordcompl_up(void) {
	GtkTreePath *path;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(wordcompl_tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	} else {
		gtk_tree_path_prev(path);
	}

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(wordcompl_tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(wordcompl_tree), 0), FALSE);
	gtk_tree_path_free(path);
}

void wordcompl_down(void) {
	GtkTreePath *path;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(wordcompl_tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	} else {
		gtk_tree_path_next(path);
	}

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(wordcompl_tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(wordcompl_tree), 0), FALSE);
	gtk_tree_path_free(path);
}

void wordcompl_complete_finish(editor_t *editor) {
	//TODO: to implement
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

