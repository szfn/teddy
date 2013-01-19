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
	char *regexpstr;
	char *cmd;
	bool line_limit;
	bool next_will_wrap_around;
	bool start_at_bol;
	bool return_on_failure;

	uint32_t *literal_text;
	int literal_text_cap, literal_text_allocated;

	buffer_t *buffer;
};

struct _editor_t editor;

extern void research_init(struct research_t *rs);

extern void quit_search_mode(struct _editor_t *editor, bool clear_selection);
extern void move_search(struct _editor_t *editor, bool ctrl_g_invoked, bool direction_forward, bool replace);

extern int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

extern void research_continue_replace_to_end(struct _editor_t *editor);

#endif
