#ifndef __COMPL__
#define __COMPL__

#include <stdbool.h>
#include <gtk/gtk.h>
#include "critbit.h"

struct completer {
	critbit0_tree cbt;

	GtkListStore *list;
	GtkWidget *tree;
	GtkWidget *window;

	bool visible;
	size_t size;
	size_t prefix_len;
	double alty;
};

void compl_init(struct completer *c);
void compl_reset(struct completer *c);
void compl_add(struct completer *c, const char *text);
char *compl_complete(struct completer *c, const char *prefix);
void compl_wnd_show(struct completer *c, const char *prefix, double x, double y, double alty, GtkWidget *parent, bool show_empty, bool show_empty_prefix);
void compl_wnd_up(struct completer *c);
void compl_wnd_down(struct completer *c);
char *compl_wnd_get(struct completer *c, bool all);
void compl_wnd_hide(struct completer *c);
bool compl_wnd_visible(struct completer *c);
void compl_free(struct completer *c);
void compl_add_to_list(struct completer *c, const char *text);

#endif