#include "history.h"

#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "interp.h"

void history_init(struct history *h) {
	compl_init(&(h->c));
	memset(h->items, 0, sizeof(struct history_item) * HISTORY_SIZE);
	h->index = 0;
	h->cap = 0;
	h->unsaved = 0;
}

static void history_item_free(struct history_item *it) {
	if (it->wd != NULL) free(it->wd);
	if (it->entry != NULL) free(it->entry);

	it->wd = NULL;
	it->entry = NULL;
}

void history_free(struct history *h) {
	compl_free(&(h->c));

	for (int i = 0; i < HISTORY_SIZE; ++i) {
		history_item_free(h->items + i);
	}
}

void history_add(struct history *h, time_t timestamp, const char *wd, const char *entry, bool counted) {
	compl_add(&(h->c), entry);

	struct history_item *prev = h->items + ((h->cap-1 < 0) ? (HISTORY_SIZE - 1) : h->cap-1);

	if (prev->entry != NULL) {
		if ((strcmp(prev->wd, wd) == 0) && (strcmp(prev->entry, entry) == 0)) return;
	}

	h->index = (h->cap)++;

	struct history_item *it = h->items + h->index;

	history_item_free(it);

	it->timestamp = timestamp;

	if (wd != NULL) {
		alloc_assert(it->wd = strdup(wd));
	} else {
		it->wd = NULL;
	}

	if (entry != NULL) {
		alloc_assert(it->entry = strdup(entry));
	} else {
		it->entry = NULL;
	}

	h->index = h->cap;

	if (counted) {
		++(h->unsaved);
	}

	if (h->unsaved >= 10) {
		char *dst;
		asprintf(&dst, "%s/.teddy_history", getenv("HOME"));
		alloc_assert(dst);
		FILE *out = fopen(dst, "a+");
		if (!out) {
			perror("Can't output history");
			return;
		}
		free(dst);

		for (int i = 10; i > 0; --i) {
			it = h->items + ((h->cap - i < 0) ? (HISTORY_SIZE - h->cap + i) : (h->cap - i));
			if (it->entry == NULL) continue;
			fprintf(out, "%ld\t%s\t%s\n", it->timestamp, it->wd, it->entry);
		}

		h->unsaved = 0;

		fclose(out);
	}
}

int teddy_history_command(ClientData client_data, Tcl_Interp*interp, int argc, const char *argv[]) {
	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'teddyhistory' command");
		return TCL_ERROR;
	}

	struct history *h;
	if (strcmp(argv[1], "cmd") == 0) {
		h = &command_history;
	} else if (strcmp(argv[1], "search") == 0) {
		h = &search_history;
	} else {
		Tcl_AddErrorInfo(interp, "Wrong first argument for 'teddyhistory' command, must be 'cmd' or 'search'");
		return TCL_ERROR;
	}

	if (strcmp(argv[2], "add") == 0) {
		if (argc < 6) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'teddyhistory' add command");
			return TCL_ERROR;
		}

		const char *timestamp_str = argv[3];
		const char *wd = argv[4];
		const char *entry  = argv[5];

		history_add(h, atol(timestamp_str), wd, entry, false);
		return TCL_OK;
	} else {
		int idx = atoi(argv[2]);

		struct history_item *it = h->items + idx;

		Tcl_SetResult(interp, (it->entry != NULL) ? it->entry : "", TCL_VOLATILE);
		return TCL_OK;
	}
}

void history_index_next(struct history *h) {
	--(h->index);
	if (h->index < 0) h->index = HISTORY_SIZE-1;
	if (h->items[h->index].entry == NULL) h->index = h->cap;
}

void history_index_prev(struct history *h) {
	if (h->index == h->cap) return;
	h->index = (h->index + 1) % HISTORY_SIZE;
}

void history_index_reset(struct history *h) {
	h->index = h->cap;
}

char *history_index_get(struct history *h) {
	return h->items[h->index].entry;
}
