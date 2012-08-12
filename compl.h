#ifndef __COMPL__
#define __COMPL__

#include <stdbool.h>
#include <stdint.h>

#include <gtk/gtk.h>

#include "buffer.h"
#include "critbit.h"

#define COMPL_COMPLETE(c, prefix) ((c)->complete((c)->this, (prefix)))
#define COMPL_WND_SHOW(c, prefix, x, y, alty, parent) ((c)->wnd_show((c)->this, (prefix), (x), (y), (alty), (parent)))
#define COMPL_WND_HIDE(c) ((c)->wnd_hide((c)->this))
#define COMPL_WND_UP(c) ((c)->wnd_up((c)->this))
#define COMPL_WND_DOWN(c) ((c)->wnd_down((c)->this))
#define COMPL_WND_GET(c, all) ((c)->wnd_get((c)->this, (all)))
#define COMPL_WND_VISIBLE(c) ((c)->wnd_visible((c)->this))
#define COMPL_COMMON_SUFFIX(c) ((c)->common_suffix((c)->this))

typedef char *complete_fn(void *this, const char *prefix);
typedef bool wnd_visible_fn(void *this);
typedef void wnd_show_fn(void *this, const char *prefix, double x, double y, double alty, GtkWidget *parent);
typedef char *wnd_get_fn(void *this, bool all);
typedef char *common_suffix_fn(void *this);
typedef void other_completer_fn(void *this);

typedef struct _generic_completer_t {
	void *this;
	complete_fn *complete;
	wnd_show_fn *wnd_show;
	other_completer_fn *wnd_up;
	other_completer_fn *wnd_down;
	wnd_get_fn *wnd_get;
	other_completer_fn *wnd_hide;
	wnd_visible_fn *wnd_visible;
	common_suffix_fn *common_suffix;

	uint16_t *(*prefix_from_buffer)(void *this, buffer_t *buffer, size_t *prefix_len);
} generic_completer_t;

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
void compl_add_to_list(struct completer *c, const char *text);
void compl_as_generic_completer(struct completer *c, generic_completer_t *gc);

#endif