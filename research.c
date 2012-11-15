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
			r->cmd = NULL;
		}
		if (r->regexpstr != NULL) {
			free(r->regexpstr);
			r->regexpstr = NULL;
		}
		if (r->literal_text != NULL) {
			free(r->literal_text);
			r->literal_text = NULL;
			r->literal_text_cap = r->literal_text_allocated = 0;
		}
	}
}

void quit_search_mode(editor_t *editor) {
	research_free_temp(&(editor->research));
	if (editor->research.mode == SM_LITERAL) {
		char *x = utf32_to_utf8_string(editor->research.literal_text, editor->research.literal_text_cap);
		history_add(&search_history, time(NULL), NULL, x, true);
		free(x);
	}
	editor->research.mode = SM_NONE;
	editor->ignore_next_entry_keyrelease = TRUE;

	editor->buffer->mark = -1;

	gtk_widget_grab_focus(editor->drar);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

static void search_start_point(editor_t *editor, bool ctrl_g_invoked, bool start_at_top, int *start_point) {
	if ((editor->buffer->mark < 0) || editor->research.search_failed) {
		if (start_at_top) {
			*start_point = 0;
		} else {
			*start_point = BSIZE(editor->buffer)-1;
		}
	} else if (ctrl_g_invoked) {
		*start_point = editor->buffer->cursor;
	} else {
		*start_point = editor->buffer->mark;
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

static void move_incremental_search(editor_t *editor, bool ctrl_g_invoked, bool direction_forward, bool restart) {
	int dst = editor->research.literal_text_cap;
	uint32_t *needle = editor->research.literal_text;

	uchar_match_fn *match_fn = should_be_case_sensitive(editor->buffer, needle, dst);

	int search_point;
	search_start_point(editor, ctrl_g_invoked, direction_forward, &search_point);

	int direction = direction_forward ? +1 : -1;

#define OS(start, offset) (start + direction * offset)

	int i = 0, j = 0;
	int needle_start = direction_forward ? 0 : dst-1;

	for (; (OS(search_point, i) >= 0) && (OS(search_point, i) < BSIZE(editor->buffer)); ++i) {
		if (j >= dst) break;
		if (match_fn(bat(editor->buffer, OS(search_point, i))->code, needle[OS(needle_start, j)])) {
			++j;
		} else {
			i -= j;
			j = 0;
		}
	}

	if (j >= dst) {
		if (!direction_forward) --i; // correction, because of selection semantics

		// search was successful
		editor->buffer->mark = OS(OS(search_point, i), -j);
		editor->buffer->cursor = OS(search_point, i);
	} else if (restart) {
		int mark = editor->buffer->mark;
		editor->buffer->mark = -1;
		move_incremental_search(editor, ctrl_g_invoked, direction_forward, false);
		if (editor->buffer->mark < 0) {
			editor->buffer->mark = mark;
			if (editor->research.literal_text_cap > 0) --(editor->research.literal_text_cap);
		}
	} else {
		editor->buffer->mark = -1;
	}
}

static bool regmatch_to_lpoints(struct augmented_lpoint_t *search_point, regmatch_t *m, int *s, int *e) {
	*s = m->rm_so + search_point->start_glyph;
	*e = m->rm_eo + search_point->start_glyph;

	return (*s >= 0) && (*e >= 0);
}

static bool move_regexp_search_forward(struct research_t *research, bool execute, int *mark, int *cursor) {
	//printf("Searching %p %d %d\n", research->buffer, *mark, *cursor);
	if (execute && (research->cmd != NULL) && (mark >= 0)) {
		editor_t *editor;
		find_editor_for_buffer(research->buffer, NULL, NULL, &editor);

		int r = interp_eval(editor, research->buffer, research->cmd, false);

		if ((r == TCL_ERROR) || (r == TCL_BREAK)) return false;
	}

	struct augmented_lpoint_t search_point;

	search_point.buffer = research->buffer;
	search_point.offset = 0;
	search_point.endatnewline = false;
	search_point.endatspace = false;

	if (research->search_failed) {
		search_point.start_glyph = 0;
		research->search_failed = false;
	} else {
		search_point.start_glyph = *cursor;
		my_glyph_info_t *glyph = bat(research->buffer, *cursor);
		if ((glyph != NULL) && (glyph->code == '\n')) ++(search_point.start_glyph);
		//printf("cursor %d mark %d start_glyph: %d\n", *cursor, *mark, search_point.start_glyph);
	}

	if (search_point.start_glyph >= BSIZE(research->buffer)) {
		research->search_failed = true;
		return false;
	}

	//printf("Starting search at: %d\n", search_point.start_glyph);

	int limit = -1;
	if (research->line_limit) {
		limit = search_point.start_glyph;
		buffer_move_point_glyph(research->buffer, &limit, MT_END, 0);
	}
	//printf("Limit: %d\n", limit);

#define OVECTOR_SIZE 10
	regmatch_t ovector[OVECTOR_SIZE];

	tre_str_source tss;
	tre_bridge_init(&search_point, &tss);

	int flags = 0;
	if (search_point.start_glyph > 0) {
		my_glyph_info_t *glyph = bat(research->buffer, search_point.start_glyph-1);
		if (glyph != NULL) {
			if (glyph->code != '\n') flags = REG_NOTBOL;
		}
	}

	int r = tre_reguexec(&(research->regexp), &tss, OVECTOR_SIZE, ovector, flags);

	if (r == REG_NOMATCH) {
		research->search_failed = true;
		return false;
	} else {
		//printf("\tMatch on line <%s> at %d %d <%d>\n", "", ovector[0].rm_so, ovector[0].rm_eo, r);

		if (regmatch_to_lpoints(&search_point, ovector+0, mark, cursor)) {
			//printf("Match at %d %d (%d)\n", *mark, *cursor, limit);
			if (research->line_limit) {
				if ((*mark > limit) || (*cursor > limit)) {
					research->search_failed = true;
					return false;
				}
			}

			char name[3] = "g.";
			for (int i = 1; i < OVECTOR_SIZE; ++i) {
				int start, end;
				if (!regmatch_to_lpoints(&search_point, ovector+i, &start, &end)) continue;
				char *text = buffer_lines_to_text(research->buffer, start, end);
				name[1] = '0' + i;
				Tcl_SetVar(interp, name, text, TCL_GLOBAL_ONLY);
				free(text);
			}

			return true;
		} else {
			return false;
		}
	}
}


void move_search(editor_t *editor, bool ctrl_g_invoked, bool direction_forward, bool replace) {
	switch(editor->research.mode) {
	case SM_LITERAL:
		move_incremental_search(editor, ctrl_g_invoked, direction_forward, !ctrl_g_invoked);
		break;
	case SM_REGEXP:
		if (ctrl_g_invoked) {
			move_regexp_search_forward(&(editor->research), replace, &(editor->buffer->mark), &(editor->buffer->cursor));
		}
		break;
	case SM_NONE:
	default:
		//nothing
		break;
	}

	editor_include_cursor(editor, ICM_MID, ICM_MID);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

void research_init(struct research_t *r) {
	r->mode = SM_NONE;
	r->search_failed = false;
	r->cmd = NULL;
	r->regexpstr = NULL;
	r->literal_text = NULL;
}

void do_regex_noninteractive_search(struct research_t *research) {
	int mark = research->buffer->mark;
	int cursor = research->buffer->cursor;

	move_regexp_search_forward(research, false, &mark, &cursor);
	interp_return_point_pair(research->buffer, mark, cursor);
	research_free_temp(research);
}


static bool do_regex_noninteractive_replace(struct research_t *research) {
	bool execute = false;
	bool r = true;

	int count = 0;
	int prevpoint = research->buffer->cursor;

	do {
		r = move_regexp_search_forward(research, execute, &(research->buffer->mark), &(research->buffer->cursor));
		execute = true;

		if (research->buffer->cursor == prevpoint) {
			if (++count > 100) {
				research->buffer->mark = -1;
				research_free_temp(research);
				return false;
			}
		} else {
			count = 0;
			prevpoint = research->buffer->cursor;
		}
	} while (r);

	research->buffer->mark = -1;
	research_free_temp(research);
	return true;
}

void start_regex_interactive(struct research_t *research, const char *regexp) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "Can not execute interactive 's' without a selection");
		return;
	}

	editor_t *editor = interp_context_editor();
	editor->research = *research;

	char *msg;
	asprintf(&msg, "Regexp {%s} {%s}", regexp, (editor->research.cmd != NULL) ? editor->research.cmd : "");
	alloc_assert(msg);
	editor_start_search(editor, SM_REGEXP, msg);
	move_regexp_search_forward(&(editor->research), false, &(research->buffer->mark), &(research->buffer->cursor));
	free(msg);
}

int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
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
	research.search_failed = false;
	research.mode = SM_REGEXP;
	research.literal_text = NULL;
	research.literal_text_allocated = research.literal_text_cap = 0;

	int flags = 0;
	bool get = false, literal = false;

	int i;
	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "--") == 0) { ++i; break; }
		else if (strcmp(argv[i], "-get") == 0) { get = true; }
		else if (strcmp(argv[i], "-line") == 0) { research.line_limit = true; }
		else if (strcmp(argv[i], "-literal") == 0) { literal = true; }
		else if (strcmp(argv[i], "-nocase") == 0) { flags = flags | REG_ICASE; }
		else if (strcmp(argv[i], "-right-assoc") == 0) { flags = flags | REG_RIGHT_ASSOC; }
		else if (strcmp(argv[i], "-ungreedy") == 0) { flags = flags | REG_UNGREEDY; }
	}

	if ((i >= argc) || (i+3 < argc)) {
		Tcl_AddErrorInfo(interp, "Malformed arguments to 's' command");
		return TCL_ERROR;
	}

	research.regexpstr = strdup(argv[i]);
	alloc_assert(research.regexpstr);

	int r = tre_regcomp(&(research.regexp), argv[i], flags | REG_NEWLINE | (literal ? REG_LITERAL : REG_EXTENDED));
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
		int start, end;
		buffer_get_selection(research.buffer, &start, &end);

		if ((start >= 0) && (end >= 0)) {
			buffer_t *mainbuf = interp_context_buffer();
			buffer_t *tempbuf = buffer_create();
			load_empty(tempbuf);

			char *selection_text = buffer_lines_to_text(mainbuf, start, end);
			buffer_replace_selection(tempbuf, selection_text);
			free(selection_text);

			tempbuf->cursor = 0;

			research.buffer = tempbuf;
			if (do_regex_noninteractive_replace(&research)) {
				char *replaced_text = buffer_all_lines_to_text(tempbuf);
				buffer_replace_selection(mainbuf, replaced_text);
				if (interp_context_editor() != NULL)
					gtk_widget_queue_draw(GTK_WIDGET(interp_context_editor()));
				free(replaced_text);

				buffer_free(tempbuf, false);
				return TCL_OK;
			} else {
				Tcl_AddErrorInfo(interp, "Automatic search and replace seemed stuck on the same line, and it was aborted");
				buffer_free(tempbuf, false);
				return TCL_ERROR;
			}

		}

		if (!interp_toplevel_frame()) {
			if (!do_regex_noninteractive_replace(&research)) {
				Tcl_AddErrorInfo(interp, "Automatic search and replace seemed stuck on the same line, and it was aborted");
				return TCL_ERROR;
			}
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
	return TCL_OK;
}

extern void research_continue_replace_to_end(editor_t *editor) {
	if (!do_regex_noninteractive_replace(&(editor->research))) {
		quick_message("Search and replace error", "Automatic search and replace seemed stuck on the same line, and it was aborted");
	}
}
