#include "research.h"

#include <string.h>
#include <pcre.h>

#include "editor.h"
#include "interp.h"
#include "global.h"

void research_init(GtkWidget *window) {
}

static void start_regexp_search(editor_t *editor, const char *regexp, const char *subst) {
	const char *errptr;
	int erroffset;
	
	pcre *re = pcre_compile(regexp, 0, &errptr, &erroffset, NULL);
	
	if (re == NULL) {
		char *msg;
		asprintf(&msg, "Syntax error in regular expression [%s] at character %d: %s", regexp, erroffset, errptr);
		if (msg == NULL) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}
		quick_message(editor, "Regexp Syntax Error", msg);
		free(msg);
	}
	
	//TODO:
	// - save regexp and subst into the regexp search state
	// - save selection (or entire buffer) into the regexp search state
	// - open search window
	// - call move_regexp_search_forward
}

int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'research' command");
		return TCL_ERROR;
	}
	
	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'research' command");
		return TCL_ERROR;
	}
	
	int i;
	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "--")) break;
	}
	
	++i;
	
	if ((i >= argc) || (i+2 < argc)) {
		Tcl_AddErrorInfo(interp, "Malformed arguments to 'research' command");
		return TCL_ERROR;
	}
	
	const char *regexp = argv[i];
	const char *subst = NULL;
	
	if (i+1 < argc) {
		subst = argv[i+1];
	}
	
	start_regexp_search(context_editor, regexp, subst);
	
	return TCL_OK;
}