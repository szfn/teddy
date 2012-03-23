#ifndef __HISTORY__
#define __HISTORY__

#include <gtk/gtk.h>
#include <tcl.h>

#include "compl.h"

#define HISTORY_SIZE 512

struct history_item {
	time_t timestamp;
	char *wd;
	char *entry;
};

struct history {
	struct completer c;
	struct history_item items[HISTORY_SIZE];

	int index;
	int cap;

	int unsaved;
};

void history_init(struct history *h);
void history_free(struct history *h);

void history_add(struct history *h, time_t timestamp, const char *wd, const char *entry, bool counted);
int teddy_history_command(ClientData client_data, Tcl_Interp*interp, int argc, const char *argv[]);

void history_index_next(struct history *h);
void history_index_prev(struct history *h);
void history_index_reset(struct history *h);
char *history_index_get(struct history *h);

#endif