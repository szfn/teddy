#ifndef __HISTORY_H__
#define __HISTORY_H__

#include <gtk/gtk.h>
#include <tcl.h>

#include "editor.h"

typedef struct _history_t {
    GtkWidget *history_window;
    GtkWidget *history_tree;
    GtkListStore *history_list;
} history_t;

/*struct _editor_t;*/

history_t *history_new(void);
void history_add(history_t *history, const char *text);
void history_pick(history_t *history, struct _editor_t *editor);
int teddy_history_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

/*
void history_reset(history_t *history);
const char *history_next(history_t *history);
const char *history_prev(history_t *history);*/


#endif
