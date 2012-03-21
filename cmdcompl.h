#ifndef __CMDCOMPL__
#define __CMDCOMPL__

#include "critbit.h"
#include "compl.h"

struct clcompleter {
	struct completer c; /* completer for commands */

	critbit0_tree cbt;
	char *absdir;
};

void cmdcompl_init(struct clcompleter *c);
void cmdcompl_free(struct clcompleter *c);
char *cmdcompl_complete(struct clcompleter *c, const char *text, const char *working_directory);
void cmdcompl_wnd_show(struct clcompleter *c, const char *text, const char *working_directory, double x, double y, double alty, GtkWidget *parent);

#endif