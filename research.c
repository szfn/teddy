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
#include "baux.h"

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
	if (editor->research.mode == SM_LITERAL)
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

static bool move_regexp_search_forward(struct research_t *research, bool execute) {
	lpoint_t *cursor = &(research->buffer->cursor);
	lpoint_t *mark = &(research->buffer->mark);

	bool cursor_on_last_line_before = (cursor->line == research->regex_endpoint.line);

	//printf("Moving forward search <%s> limit %p/%d (%p/%d) %d\n", research->cmd, research->regex_endpoint.line, (research->regex_endpoint.line != NULL) ? research->regex_endpoint.line->lineno : -1, cursor->line, cursor->line->lineno, cursor_on_last_line_before);
	if (execute && (research->cmd != NULL) && (mark->line != NULL)) {
		editor_t *editor;
		find_editor_for_buffer(research->buffer, NULL, NULL, &editor);

		if (editor != NULL) interp_context_editor_set(editor);
		else interp_context_buffer_set(research->buffer);

		int r = Tcl_Eval(interp, research->cmd);

		switch (r) {
		case TCL_ERROR: {
			Tcl_Obj *options = Tcl_GetReturnOptions(interp, r);
			Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
			Tcl_Obj *stackTrace;
			Tcl_IncrRefCount(key);
			Tcl_DictObjGet(NULL, options, key, &stackTrace);
			Tcl_DecrRefCount(key);
			quick_message("TCL Error", Tcl_GetString(stackTrace));
			Tcl_ResetResult(interp);
			return false;
		}

		case TCL_BREAK: return false;

		case TCL_OK:
		case TCL_CONTINUE:
		default:
			break;
		}
	}

	struct augmented_lpoint_t search_point;

	if (research->search_failed) {
		search_point.line = research->buffer->real_line;
		search_point.start_glyph = 0;
		search_point.offset = 0;
		research->search_failed = false;
	} else {
		if (cursor_on_last_line_before) {
			if (cursor->line != research->regex_endpoint.line) return false;
		}

		search_point.line = cursor->line;
		search_point.start_glyph = cursor->glyph;

		if (search_point.start_glyph == (search_point.line->cap)) {
			//printf("\tnext line %d %d\n", search_point.start_glyph, search_point.line->cap);

			if (search_point.line == research->regex_endpoint.line) return false;

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

		int r = tre_reguexec(&(research->regexp), &tss, OVECTOR_SIZE, ovector, (search_point.start_glyph == 0) ? 0 : REG_NOTBOL);

		if (r == REG_NOMATCH) {
			// no other match on this line, end search if there is a line limit or this was the last search line.
			if (research->line_limit) return false;
			if (search_point.line == research->regex_endpoint.line) return false;

			search_point.line = search_point.line->next;
			search_point.start_glyph = 0;
			search_point.offset = 0;
		} else {
			//printf("\tMatch on line <%s> at %d %d <%d>\n", "", ovector[0].rm_so, ovector[0].rm_eo, r);

			if (regmatch_to_lpoints(&search_point, ovector+0, mark, cursor)) {
				lexy_update_for_move(research->buffer, cursor->line);

				char name[3] = "g.";
				for (int i = 1; i < OVECTOR_SIZE; ++i) {
					lpoint_t start, end;
					if (!regmatch_to_lpoints(&search_point, ovector+i, &start, &end)) continue;
					char *text = buffer_lines_to_text(research->buffer, &start, &end);
					name[1] = '0' + i;
					Tcl_SetVar(interp, name, text, TCL_GLOBAL_ONLY);
					free(text);
				}

				//printf("\tBail check (%p == %p) (%d > %d)\n", cursor->line, research->regex_endpoint.line, cursor->glyph, research->regex_endpoint.glyph);
				// end the search if cursor is now on the last line of the search after the ending glyph
				if ((cursor->line == research->regex_endpoint.line)
					&& (cursor->glyph > research->regex_endpoint.glyph))
					return false;

				return true;
			} else {
				return false;
			}
		}
	}

	// if we get here no match was found and we are at the end of the buffer

	research->search_failed = true;

	return false;
}

void move_search(editor_t *editor, bool ctrl_g_invoked, bool direction_forward, bool replace) {
	switch(editor->research.mode) {
	case SM_LITERAL:
		move_incremental_search(editor, ctrl_g_invoked, direction_forward);
		break;
	case SM_REGEXP:
		if (ctrl_g_invoked) {
			move_regexp_search_forward(&(editor->research), replace);
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

void do_regex_noninteractive_search(struct research_t *research) {
	move_regexp_search_forward(research, false);
	interp_return_point_pair(&(research->buffer->mark), &(research->buffer->cursor));
	research_free_temp(research);
}

void do_regex_noninteractive_replace(struct research_t *research) {
	bool execute = false;
	bool r = true;
	do {
		r = move_regexp_search_forward(research, execute);
		execute = true;
	} while (r);

	research->buffer->mark.line = NULL;
	research->buffer->mark.glyph = 0;
	copy_lpoint(&(research->buffer->cursor), &(research->regex_endpoint));

	research_free_temp(research);
}

void start_regex_interactive(struct research_t *research, const char *regexp) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "Can not execute interactive 's' without a selection");
	}

	research->regex_endpoint.line = NULL;
	research->regex_endpoint.glyph = 0;

	editor_t *editor = interp_context_editor();
	editor->research = *research;

	char *msg;
	asprintf(&msg, "Regexp {%s} {%s}", regexp, (editor->research.cmd != NULL) ? editor->research.cmd : "");
	alloc_assert(msg);
	editor_start_search(editor, SM_REGEXP, msg);
	move_regexp_search_forward(&(editor->research), false);
	free(msg);
}

int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	bool get = false, literal = false;
	struct research_t research;

	if (interp_context_buffer() == NULL) {
		Tcl_AddErrorInfo(interp, "No buffer selected, can not execute 's' command");
		return TCL_ERROR;
	}

	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 's' command");
		return TCL_ERROR;
	}

	research.buffer = interp_context_buffer();
	research.line_limit = false;

	int i;
	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "--") == 0) { ++i; break; }
		if (strcmp(argv[i], "-get") == 0) { get = true; }
		if (strcmp(argv[i], "-line") == 0) { research.line_limit = true; }
		if (strcmp(argv[i], "-literal") == 0) { literal = true; }
	}

	if ((i >= argc) || (i+3 < argc)) {
		Tcl_AddErrorInfo(interp, "Malformed arguments to 's' command");
		return TCL_ERROR;
	}

	int r = tre_regcomp(&(research.regexp), argv[i], literal ? REG_LITERAL : REG_EXTENDED);
	if (r != REG_OK) {
#define REGERROR_BUF_SIZE 512
		char buf[REGERROR_BUF_SIZE];
		tre_regerror(r, &(research.regexp), buf, REGERROR_BUF_SIZE);
		char *msg;
		asprintf(&msg, "Sytanx error in regular expression [%s]: %s\n", argv[i], buf);
		alloc_assert(msg);
		Tcl_AddErrorInfo(interp, msg);
		free(msg);
		return TCL_ERROR;
	}

	if (i+2 < argc) {
		research.cmd = Tcl_Merge(2, argv + i + 1);
		alloc_assert(research.cmd);
	} else if (i + 1 < argc) {
		char *x = Tcl_Alloc(strlen(argv[i+1])+1);
		alloc_assert(x);
		strcpy(x, argv[i+1]);
		research.cmd = x;
	} else {
		research.cmd = NULL;
	}

	//teddy_frame_debug();

	if (get && (research.cmd != NULL)) {
		Tcl_AddErrorInfo(interp, "-get flag not supported when a command is specified");
		research_free_temp(&research);
		return TCL_ERROR;
	}

	if (get) {
		do_regex_noninteractive_search(&research);
		return TCL_OK;
	}

	if (research.cmd != NULL) {
		lpoint_t start, end;
		buffer_get_selection(research.buffer, &start, &end);

		if (!interp_toplevel_frame() && (start.line == NULL)) {
			// if we are not at toplevel this command will be non-interactive anyway
			buffer_get_extremes(research.buffer, &start, &end);
			copy_lpoint(&start, &(research.buffer->cursor));
		}

		if ((start.line != NULL) && (end.line != NULL)) {
			// found a selection, go with non-interactive search and replace
			copy_lpoint(&(research.buffer->cursor), &start);
			copy_lpoint(&(research.regex_endpoint), &end);

			do_regex_noninteractive_replace(&research);
			return TCL_OK;
		} else {
			start_regex_interactive(&research, argv[i]);
			return TCL_OK;
		}
	} else {
		if (interp_toplevel_frame()) {
			start_regex_interactive(&research, argv[i]);
			return TCL_OK;
		} else {
			do_regex_noninteractive_search(&research);
			return TCL_OK;
		}
	}
}

extern void research_continue_replace_to_end(editor_t *editor) {
	lpoint_t start, end;
	buffer_get_extremes(editor->buffer, &start, &end);
	copy_lpoint(&(editor->research.regex_endpoint), &end);
	do_regex_noninteractive_replace(&(editor->research));
}
