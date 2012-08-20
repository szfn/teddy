#include "research.h"

#include <string.h>
#include <tre/tre.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "editor.h"
#include "interp.h"
#include "global.h"
#include "treint.h"
#include "lexy.h"

GtkWidget *research_window;
GtkWidget *research_button_replace;
editor_t *research_editor;
regex_t research_regexp;
const char *research_subst;
bool research_line_limit;
bool research_next_will_wrap_around;

typedef enum _research_window_action_t {
	RESEARCH_REPLACE = 0,
	RESEARCH_NEXT = 1,
	RESEARCH_STOP = 2,
} research_window_action_t;

research_window_action_t research_window_results[] = { RESEARCH_REPLACE, RESEARCH_NEXT, RESEARCH_STOP };

static void move_regexp_search_forward(bool start_at_top, enum research_mode_t research_mode) {
#define OVECTOR_SIZE 10
	regmatch_t ovector[OVECTOR_SIZE];

	struct augmented_lpoint_t search_point;

	if (start_at_top) {
		search_point.line = research_editor->buffer->real_line;
		search_point.start_glyph = 0;
		search_point.offset = 0;
	} else {
		search_point.line = research_editor->buffer->cursor.line;
		search_point.start_glyph = research_editor->buffer->cursor.glyph;
		search_point.offset = 0;
	}

	while (search_point.line != NULL) {
		tre_str_source tss;
		tre_bridge_init(&search_point, &tss);

		//printf("Searching at: %d,%d+%d\n", search_point.line->lineno, search_point.start_glyph, search_point.offset);

		int r = tre_reguexec(&research_regexp, &tss, OVECTOR_SIZE, ovector, 0);

		if (r == REG_NOMATCH) {
			if (research_line_limit) {
				if (research_mode != RM_INTERACTIVE) Tcl_SetResult(interp, "false", TCL_VOLATILE);
				return;
			}
			search_point.line = search_point.line->next;
			search_point.start_glyph = 0;
			search_point.offset = 0;
		} else {
			// there is a match in ovector[0], ovector[1] mark it

			//printf("Match on line <%s> at %d %d\n", text, ovector[0], ovector[1]);

			int start_glyph = ovector[0].rm_so + search_point.start_glyph;
			int end_glyph = ovector[0].rm_eo + search_point.start_glyph;

			if ((start_glyph >= 0) && (end_glyph >= 0)) {
				switch (research_mode) {
				case RM_INTERACTIVE:
				case RM_SELECT:
					research_editor->buffer->cursor.line = research_editor->buffer->mark.line = search_point.line;
					research_editor->buffer->mark.glyph = start_glyph;
					research_editor->buffer->cursor.glyph = end_glyph;
					break;
				case RM_TOSTART:
					research_editor->buffer->cursor.line = search_point.line;
					research_editor->buffer->cursor.glyph = start_glyph;
					break;
				case RM_TOEND:
					research_editor->buffer->cursor.line = search_point.line;
					research_editor->buffer->cursor.glyph = end_glyph;
					break;
				}

				lexy_update_for_move(research_editor->buffer, research_editor->buffer->cursor.line);
				editor_center_on_cursor(research_editor);
				gtk_widget_queue_draw(GTK_WIDGET(research_editor));

				if (research_mode != RM_INTERACTIVE) Tcl_SetResult(interp, "true", TCL_VOLATILE);
			} else { // if we end up here it's probably a bug
				if (research_mode != RM_INTERACTIVE) Tcl_SetResult(interp, "false", TCL_VOLATILE);
			}
			return;
		}
	}

	// if we get here no match was found and we are at the end of the buffer

	if (research_mode != RM_INTERACTIVE) {
		Tcl_SetResult(interp, "false", TCL_VOLATILE);
		return;
	}

	if (research_next_will_wrap_around) {
		research_next_will_wrap_around = false;
		move_regexp_search_forward(true, RM_INTERACTIVE);
	} else {
		research_next_will_wrap_around = true;
	}
}

static void research_stop_search(void) {
	tre_regfree(&research_regexp);
	gtk_widget_hide(research_window);
	editor_grab_focus(research_editor, false);
}

static void research_replace_selection(void) {
	if (research_subst != NULL) {
		//TODO: check that there is a selection active
		editor_replace_selection(research_editor, research_subst);
	}
	move_regexp_search_forward(false, RM_INTERACTIVE);
}

static void research_perform_action(research_window_action_t *action) {
	switch (*action) {
	case RESEARCH_REPLACE:
		research_replace_selection();
		break;
	case RESEARCH_NEXT:
		move_regexp_search_forward(false, RM_INTERACTIVE);
		break;
	case RESEARCH_STOP:
		research_stop_search();
		break;
	}
}

static gboolean research_button_clicked(GtkButton *button, research_window_action_t *action) {
	research_perform_action(action);
	return TRUE;
}

static gboolean research_key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	switch(event->keyval) {
	case GDK_KEY_r:
		research_replace_selection();
		break;
	case GDK_KEY_n:
		move_regexp_search_forward(false, RM_INTERACTIVE);
		break;
	case GDK_KEY_Escape:
	case GDK_KEY_s:
		research_stop_search();
		break;
	}

	return TRUE;
}

static gboolean research_window_close_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	research_stop_search();
	return TRUE;
}

void research_init(GtkWidget *window) {
	research_window = gtk_dialog_new_with_buttons("Regexp Search", GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, NULL);

	research_button_replace = gtk_button_new_with_mnemonic("_Replace");
	GtkWidget *button_next = gtk_button_new_with_mnemonic("_Next");
	GtkWidget *button_stop = gtk_button_new_with_mnemonic("_Stop");

	gtk_button_set_use_underline(GTK_BUTTON(research_button_replace), TRUE);
	gtk_button_set_use_underline(GTK_BUTTON(button_next), TRUE);
	gtk_button_set_use_underline(GTK_BUTTON(button_stop), TRUE);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);

	gtk_container_add(GTK_CONTAINER(vbox), research_button_replace);
	gtk_container_add(GTK_CONTAINER(vbox), button_next);
	gtk_container_add(GTK_CONTAINER(vbox), button_stop);

	g_signal_connect(G_OBJECT(research_button_replace), "clicked", G_CALLBACK(research_button_clicked), research_window_results + RESEARCH_REPLACE);
	g_signal_connect(G_OBJECT(button_next), "clicked", G_CALLBACK(research_button_clicked), research_window_results + RESEARCH_NEXT);
	g_signal_connect(G_OBJECT(button_stop), "clicked", G_CALLBACK(research_button_clicked), research_window_results + RESEARCH_STOP);

	g_signal_connect(G_OBJECT(research_window), "key-press-event", G_CALLBACK(research_key_press_callback), NULL);
	g_signal_connect(G_OBJECT(research_window), "delete_event", G_CALLBACK(research_window_close_callback), NULL);

	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(research_window))), vbox);

	gtk_window_set_default_size(GTK_WINDOW(research_window), 200, -1);
}

static char *automatic_search_and_replace(char *text, regex_t *re, const char *subst) {
	int allocation = 10;
	char *r = malloc(allocation * sizeof(char));
	regmatch_t ovector[OVECTOR_SIZE];
	int start = 0;

	r[0] = '\0';

	while (tre_regexec(re, text+start, OVECTOR_SIZE, ovector, 0) == 0) {
		while (strlen(r) + 1 + ovector[0].rm_so + strlen(subst) >= allocation) {
			allocation *= 2;
			r = realloc(r, allocation * sizeof(char));
			alloc_assert(r);
		}

		strncat(r, text+start, ovector[0].rm_so);
		strcat(r, subst);
		start += ovector[0].rm_eo;
	}

	while (strlen(r) + 1 + strlen(text+start) >= allocation) {
		allocation *= 2;
		r = realloc(r, allocation * sizeof(char));
		alloc_assert(r);
	}
	strcat(r, text+start);

	return r;
}

void start_regexp_search(editor_t *editor, const char *regexp, const char *subst, bool line_limit, enum research_mode_t research_mode, bool literal) {
	int r = tre_regcomp(&research_regexp, regexp, literal ? REG_LITERAL : REG_EXTENDED);

	if (r != REG_OK) {
#define REGERROR_BUF_SIZE 512
		char buf[REGERROR_BUF_SIZE];
		tre_regerror(r, &research_regexp, buf, REGERROR_BUF_SIZE);
		char *msg;
		asprintf(&msg, "Sytanx error in regular expression [%s]: %s\n", regexp, buf);
		alloc_assert(msg);
		quick_message("Regex Syntax Error", msg);
		free(msg);
		return;
	}

	lpoint_t start, end;
	buffer_get_selection(editor->buffer, &start, &end);

	if ((start.line != NULL) && (end.line != NULL) && (subst != NULL)) {
		// there is an active selection and we specified a substitution, replace all occourence
		char *text = buffer_lines_to_text(editor->buffer, &start, &end);
		char *newtext = automatic_search_and_replace(text, &research_regexp, subst);

		editor_replace_selection(editor, newtext);

		free(text);
		free(newtext);

		tre_regfree(&research_regexp);
	} else {
		// if we get here there is no selection in the current editor's buffer and we should start an interactive search/replace

		research_editor = editor;
		research_subst = subst;
		research_line_limit = line_limit;
		research_next_will_wrap_around = false;

		if (research_mode == RM_INTERACTIVE) {
			gtk_widget_show_all(research_window);
			gtk_widget_set_visible(research_button_replace, research_subst != NULL);
		}

		move_regexp_search_forward(false, research_mode);
	}
}

int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	enum research_mode_t research_mode = RM_INTERACTIVE;
	bool line_limit = false;

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
		if (strcmp(argv[i], "-0") == 0) { research_mode = RM_TOSTART; }
		if (strcmp(argv[i], "-$") == 0) { research_mode = RM_TOEND; }
		if (strcmp(argv[i], "-sel") == 0) { research_mode = RM_SELECT; }
		if (strcmp(argv[i], "-line") == 0) { line_limit = true; }
	}

	if ((i >= argc) || (i+2 < argc)) {
		Tcl_AddErrorInfo(interp, "Malformed arguments to 's' command");
		return TCL_ERROR;
	}

	const char *regexp = argv[i];
	const char *subst = NULL;

	if (i+1 < argc) {
		subst = argv[i+1];
	}

	start_regexp_search(interp_context_editor(), regexp, subst, line_limit, research_mode, false);

	return TCL_OK;
}
