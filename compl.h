#ifndef __COMPL__
#define __COMPL__

#include <stdbool.h>
#include <stdint.h>

#include <gtk/gtk.h>

#include "buffer.h"
#include "critbit.h"

struct completer;

typedef const char *recalc_fn(struct completer *c, const char *prefix);
typedef char *prefix_from_buffer_fn(buffer_t *buffer);

struct completer {
	critbit0_tree cbt;

	GtkListStore *list;
	GtkWidget *tree;
	GtkWidget *window;

	bool visible;
	size_t size;
	size_t prefix_len;
	double alty;

	char *common_suffix;

	recalc_fn *recalc;
	prefix_from_buffer_fn *prefix_from_buffer;

	void *tmpdata;
};

void compl_init(struct completer *c);
void compl_reset(struct completer *c);
void compl_add(struct completer *c, const char *text);
char *compl_complete(struct completer *c, const char *prefix);
void compl_wnd_show(struct completer *c, const char *prefix, double x, double y, double alty, GtkWidget *parent, bool show_empty, bool show_empty_prefix);
void compl_wnd_up(struct completer *c);
void compl_wnd_down(struct completer *c);
char *compl_wnd_get(struct completer *c, bool all);
int compl_wnd_size(struct completer *c);
void compl_wnd_hide(struct completer *c);
bool compl_wnd_visible(struct completer *c);
void compl_free(struct completer *c);

void cmdcompl_init(void);
const char *cmdcompl_recalc(struct completer *c, const char *prefix);
bool in_external_commands(const char *arg);
void word_completer_full_update(void);

#endif