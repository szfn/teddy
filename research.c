#include "research.h"

#include <string.h>
#include <tre/tre.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <unicode/uchar.h>

#include "editor.h"
#include "interp.h"
#include "global.h"
#include "treint.h"
#include "lexy.h"

static void research_free_temp(struct research_t *r) {
	if (r->mode == SM_REGEXP) {
		tre_regfree(&(r->regexp));
		if (r->cmd != NULL) {
			Tcl_Free(r->cmd);
		}
	}
}

void quit_search_mode(editor_t *editor) {
	research_free_temp(&(editor->research));
	history_add(&search_history, time(NULL), NULL, gtk_entry_get_text(GTK_ENTRY(editor->search_entry)), true);
	editor->research.mode = SM_NONE;
	editor->ignore_next_entry_keyrelease = TRUE;
	gtk_widget_grab_focus(editor->drar);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

static void search_start_point(editor_t *editor, bool ctrl_g_invoked, bool start_at_top, real_line_t **search_line, int *search_glyph) {
	if ((editor->buffer->mark.line == NULL) || editor->research.search_failed) {
		if (start_at_top) {
			*search_line = editor->buffer->real_line;
			*search_glyph = 0;
		} else {
			for (*search_line = editor->buffer->real_line; (*search_line != NULL) && ((*search_line)->next != NULL); *search_line = (*search_line)->next);
			*search_glyph = (*search_line)->cap-1;
		}
	} else if (ctrl_g_invoked) {
		*search_line = editor->buffer->cursor.line;
		*search_glyph = editor->buffer->cursor.glyph;
	} else {
		*search_line = editor->buffer->mark.line;
		*search_glyph = editor->buffer->mark.glyph;
	}
}

typedef bool uchar_match_fn(uint32_t a, uint32_t b);

static bool uchar_match_case_sensitive(uint32_t a, uint32_t b) {
	return a == b;
}

static bool uchar_match_case_insensitive(uint32_t a, uint32_t b) {
	return u_tolower(a) == u_tolower(b);
}

static uchar_match_fn *should_be_case_sensitive(buffer_t *buffer, uint32_t *needle, int len) {
	if (config_intval(&(buffer->config), CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE) == 0) return uchar_match_case_insensitive;
	if (config_intval(&(buffer->config), CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE) == 1) return uchar_match_case_sensitive;

	// smart case sensitiveness set up here

	for (int i = 0; i < len; ++i) {
		if (u_isupper(needle[i])) return uchar_match_case_sensitive;
	}

	return uchar_match_case_insensitive;
}

static void move_incremental_search(editor_t *editor, bool ctrl_g_invoked, bool direction_forward) {
	int dst;
	uint32_t *needle = utf8_to_utf32_string(gtk_entry_get_text(GTK_ENTRY(editor->search_entry)), &dst);

	uchar_match_fn *match_fn = should_be_case_sensitive(editor->buffer, needle, dst);

	real_line_t *search_line;
	int search_glyph;
	search_start_point(editor, ctrl_g_invoked, direction_forward, &search_line, &search_glyph);

	int direction = direction_forward ? +1 : -1;

#define OS(start, offset) (start + direction * offset)

	while (search_line != NULL) {
		int i = 0, j = 0;
		int needle_start = direction_forward ? 0 : dst-1;

		for ( ; i < search_line->cap; ++i) {
			if (j >= dst) break;
			if (match_fn(search_line->glyph_info[OS(search_glyph, i)].code, needle[OS(needle_start, j)])) {
				++j;
			} else {
				i -= j;
				j = 0;
			}
		}

		if (j >= dst) {
			if (!direction_forward) --i; // correction, because of selection semantics

			// search was successful
			editor->buffer->mark.line = search_line;
			editor->buffer->mark.glyph = OS(OS(search_glyph, i), -j);
			editor->buffer->cursor.line = search_line;
			editor->buffer->cursor.glyph = OS(search_glyph, i);
			lexy_update_for_move(editor->buffer, editor->buffer->cursor.line);
			break;
		}

		// every line searched after the first line will start from the beginning
		search_line = direction_forward ? search_line->next : search_line->prev;
		search_glyph = direction_forward ? 0 : ((search_line != NULL) ? search_line->cap-1 : 0);
	}

	if (search_line == NULL) {
		editor->research.search_failed = false;
		buffer_unset_mark(editor->buffer);
	}

	free(needle);
}

static bool regmatch_to_lpoints(struct augmented_lpoint_t *search_point, regmatch_t *m, lpoint_t *s, lpoint_t *e) {
	int start_glyph = m->rm_so + search_point->start_glyph;
	int end_glyph = m->rm_eo + search_point->start_glyph;

	if ((start_glyph >= 0) && (end_glyph >= 0)) {
		s->line = e->line = search_point->line;
		s->glyph = start_glyph;
		e->glyph = end_glyph;
		return true;
	} else {
		return false;
	}
}

static bool move_regexp_search_forward(editor_t *editor, bool execute, lpoint_t *mark, lpoint_t *cursor) {
	if (execute && (editor->research.cmd != NULL) && (editor->buffer->mark.line != NULL)) {
		bool r = interp_eval(editor, editor->research.cmd, false);

		if (r == TCL_ERROR) return false;
		if (r == TCL_BREAK) return false;

	}

	struct augmented_lpoint_t search_point;

	if (editor->research.search_failed) {
		search_point.line = editor->buffer->real_line;
		search_point.start_glyph = 0;
		search_point.offset = 0;
		editor->research.search_failed = false;
	} else {
		search_point.line = editor->buffer->cursor.line;
		search_point.start_glyph = editor->buffer->cursor.glyph;

		if (search_point.start_glyph == (search_point.line->cap)) {
			// if the cursor is after the last character of the line just move to the next line
			// this isn't just an optimization
			// it allows s {^.*$} to work correctly even with empty lines (it would otherwise get stuck on them)
			search_point.line = search_point.line->next;
			search_point.start_glyph = 0;
		}

		search_point.offset = 0;
	}

#define OVECTOR_SIZE 10
	regmatch_t ovector[OVECTOR_SIZE];

	while (search_point.line != NULL) {
		tre_str_source tss;
		tre_bridge_init(&search_point, &tss);

		//printf("Searching at: %d,%d+%d\n", search_point.line->lineno, search_point.start_glyph, search_point.offset);

		int r = tre_reguexec(&(editor->research.regexp), &tss, OVECTOR_SIZE, ovector, (search_point.start_glyph == 0) ? 0 : REG_NOTBOL);

		if (r == REG_NOMATCH) {
			// no other match on this line, end search if there is a line limit or this was the last search line.
			if (editor->research.line_limit) return false;
			if (search_point.line == editor->research.regex_endpoint.line) return false;

			search_point.line = search_point.line->next;
			search_point.start_glyph = 0;
			search_point.offset = 0;
		} else {
			//printf("Match on line <%s> at %d %d <%d>\n", "", ovector[0].rm_so, ovector[0].rm_eo, r);

			if (regmatch_to_lpoints(&search_point, ovector+0, &(editor->buffer->mark), &(editor->buffer->cursor))) {
				lexy_update_for_move(editor->buffer, cursor->line);

				char name[3] = "g.";
				for (int i = 1; i < OVECTOR_SIZE; ++i) {
					lpoint_t start, end;
					if (!regmatch_to_lpoints(&search_point, ovector+i, &start, &end)) continue;
					char *text = buffer_lines_to_text(editor->buffer, &start, &end);
					name[1] = '0' + i;
					Tcl_SetVar(interp, name, text, TCL_GLOBAL_ONLY);
					free(text);
				}

				// end the search if cursor is now on the last line of the search after the ending glyph
				if ((editor->buffer->cursor.line == editor->research.regex_endpoint.line)
					&& (editor->buffer->cursor.glyph > editor->research.regex_endpoint.glyph))
					return false;

				return true;
			} else {
				return false;
			}
		}
	}

	// if we get here no match was found and we are at the end of the buffer

	editor->research.search_failed = true;

	return false;
}

void move_search(editor_t *editor, bool ctrl_g_invoked, bool direction_forward, bool replace) {
	switch(editor->research.mode) {
	case SM_LITERAL:
		move_incremental_search(editor, ctrl_g_invoked, direction_forward);
		break;
	case SM_REGEXP:
		if (ctrl_g_invoked) {
			move_regexp_search_forward(editor, replace, &(editor->buffer->mark), &(editor->buffer->cursor));
		}
		break;
	case SM_NONE:
	default:
		//nothing
		break;
	}

	editor_center_on_cursor(editor);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

void research_init(struct research_t *r) {
	r->mode = SM_NONE;
	r->search_failed = false;
	r->cmd = NULL;
}

int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	bool line_limit = false, get = false, literal = false;

	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 's' command");
		return TCL_ERROR;
	}

	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 's' command");
		return TCL_ERROR;
	}

	int i;
	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "--") == 0) { ++i; break; }
		if (strcmp(argv[i], "-get") == 0) { get = true; }
		if (strcmp(argv[i], "-line") == 0) { line_limit = true; }
		if (strcmp(argv[i], "-literal") == 0) { literal = true; }
	}

	editor_t *editor = interp_context_editor();

	editor->research.line_limit = line_limit;

	if ((i >= argc) || (i+3 < argc)) {
		Tcl_AddErrorInfo(interp, "Malformed arguments to 's' command");
		return TCL_ERROR;
	}

	int r = tre_regcomp(&(editor->research.regexp), argv[i], literal ? REG_LITERAL : REG_EXTENDED);
	if (r != REG_OK) {
#define REGERROR_BUF_SIZE 512
		char buf[REGERROR_BUF_SIZE];
		tre_regerror(r, &(editor->research.regexp), buf, REGERROR_BUF_SIZE);
		char *msg;
		asprintf(&msg, "Sytanx error in regular expression [%s]: %s\n", argv[i], buf);
		alloc_assert(msg);
		Tcl_AddErrorInfo(interp, msg);
		free(msg);
		return TCL_ERROR;
	}

	if (i+2 < argc) {
		editor->research.cmd = Tcl_Merge(2, argv + i + 1);
		alloc_assert(editor->research.cmd);
	} else if (i + 1 < argc) {
		char *x = Tcl_Alloc(strlen(argv[i+1])+1);
		alloc_assert(x);
		strcpy(x, argv[i+1]);
		editor->research.cmd = x;
	} else {
		editor->research.cmd = NULL;
	}

	if (get && (editor->research.cmd != NULL)) {
		Tcl_AddErrorInfo(interp, "-get flag not supported when a command is specified");
		Tcl_Free(editor->research.cmd);
		return TCL_ERROR;
	}

	if (get) {
		lpoint_t mark, cursor;
		move_regexp_search_forward(editor, false, &mark, &cursor);
		interp_return_point_pair(&mark, &cursor);
		return TCL_OK;
	}

	lpoint_t start, end;
	buffer_get_selection(editor->buffer, &start, &end);

	if ((start.line != NULL) && (end.line != NULL) && (editor->research.cmd != NULL)) {
		// there is an active selection and we specified a substitution, replace all occourence

		copy_lpoint(&(editor->buffer->cursor), &start);
		copy_lpoint(&(editor->research.regex_endpoint), &end);

		bool execute = false;
		bool r = true;
		do {
			r = move_regexp_search_forward(editor, execute, &(editor->buffer->mark), &(editor->buffer->cursor));
			execute = true;
		} while (r);
	} else {
		editor->research.regex_endpoint.line = NULL;
		editor->research.regex_endpoint.glyph = 0;

		char *msg;
		asprintf(&msg, "Regexp {%s} {%s}", argv[i], (editor->research.cmd != NULL) ? editor->research.cmd : "");
		alloc_assert(msg);
		editor_start_search(editor, SM_REGEXP, msg);
		move_regexp_search_forward(editor, false, &(editor->buffer->mark), &(editor->buffer->cursor));
		free(msg);
	}

	return TCL_OK;
}
