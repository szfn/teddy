#ifndef __RESEARCH__
#define __RESEARCH__

#include <stdbool.h>
#include <gtk/gtk.h>
#include <tcl.h>

#include "editor.h"

enum research_mode_t {
	RM_INTERACTIVE = 0,
	RM_SELECT,
	RM_TOSTART,
	RM_TOEND
};

extern void research_init(GtkWidget *window);
extern int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
extern void start_regexp_search(editor_t *editor, const char *regexp, const char *subst, bool line_limit, enum research_mode_t research_mode, bool literal);

#endif
