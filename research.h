#ifndef __RESEARCH__
#define __RESEARCH__

#include <stdbool.h>
#include <gtk/gtk.h>
#include <tcl.h>
#include <tre/tre.h>

#include "buffer.h"

enum search_mode_t {
	SM_NONE = 0,
	SM_LITERAL = 1,
	SM_REGEXP = 2
};

struct research_t {
	enum search_mode_t mode;
	bool search_failed;

	regex_t regexp;
	char *cmd;
	bool line_limit;
	bool next_will_wrap_around;

	lpoint_t regex_endpoint;
};

struct _editor_t editor;

extern void research_init(struct research_t *rs);

extern void quit_search_mode(struct _editor_t *editor);
void move_search(struct _editor_t *editor, bool ctrl_g_invoked, bool direction_forward, bool replace);

extern int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif
