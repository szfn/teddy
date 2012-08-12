#ifndef __CMDCOMPL__
#define __CMDCOMPL__

#include "critbit.h"
#include "compl.h"

struct clcompleter {
	struct completer c; /* completer for commands */

	critbit0_tree cbt;
	char *absdir;

	struct history *h;
};

void cmdcompl_init(struct clcompleter *c, struct history *h);
void cmdcompl_free(struct clcompleter *c);
char *cmdcompl_complete(struct clcompleter *c, const char *text);
void cmdcompl_wnd_show(struct clcompleter *c, const char *text, double x, double y, double alty, GtkWidget *parent);

void cmdcompl_as_generic_completer(struct clcompleter *c, generic_completer_t *gc);

#endif