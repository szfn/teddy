#include "research.h"

#include <string.h>
#include <pcre.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "editor.h"
#include "interp.h"
#include "global.h"

GtkWidget *research_window;
editor_t *research_editor;
pcre *research_regexp;
pcre_extra *research_regexp_extra;
const char *research_subst;
bool research_next_will_wrap_around;

typedef enum _research_window_action_t {
	RESEARCH_REPLACE = 0,
	RESEARCH_NEXT = 1,
	RESEARCH_STOP = 2,
} research_window_action_t;

research_window_action_t research_window_results[] = { RESEARCH_REPLACE, RESEARCH_NEXT, RESEARCH_STOP };

static int glyph_pos_to_utf8_byte_pos(const char *text, int glyph) {
	int i = -1;
	while (glyph >= 0) {
		++i;
		if (i >= strlen(text)) return -1;
		if (text[i] <= 0x7F) --glyph;
	}
	return i;
}

static int utf8_byte_pos_to_glyph_pos(const char *text, int pos) {
	int glyph = -1;
	for (int i = 0; i <= pos; ++i) {
		if (text[i] <= 0x7F) ++glyph;
	}
	return glyph;
}

static void move_regexp_search_forward(void) {
#define OVECTOR_SIZE 10
	int ovector[OVECTOR_SIZE];
	lpoint_t search_point;
	copy_lpoint(&search_point,&(research_editor->buffer->cursor));
	
	while (search_point.line != NULL) {
		char *text = buffer_line_to_text(research_editor->buffer, search_point.line);
		int pos = glyph_pos_to_utf8_byte_pos(text, search_point.glyph);
		
		if (pos < 0) break; // just as safety check, it should never happen that the conversion fails
		
		int rc = pcre_exec(research_regexp, research_regexp_extra, text, strlen(text), pos, 0, ovector, OVECTOR_SIZE);
		
		if (rc >= 0) {
			// there is a match in ovector[0], ovector[1] mark it

			int start_glyph = utf8_byte_pos_to_glyph_pos(text, ovector[0]);
			int end_glyph = utf8_byte_pos_to_glyph_pos(text, ovector[1]);
			
			if ((start_glyph >= 0) && (end_glyph >= 0)) {
				research_editor->buffer->cursor.line = research_editor->buffer->mark.line = search_point.line;
				research_editor->buffer->mark.glyph = start_glyph;
				research_editor->buffer->cursor.glyph = end_glyph;
			}
			
			free(text);
			return;
		} else if (rc == -1) {
			// there was no match
			free(text);
			search_point.line = search_point.line->next;
			search_point.glyph = 0;
		} else {
			// there was an actual error (ie not rc == -1 -> no match)
			char *msg;
			asprintf(&msg, "Error during pcre execution: %d", rc);
			quick_message(research_editor, "Internal PCRE error", msg);
			free(msg);
			free(text);
			return;
		}
		
	}
	
	// if we get here no match was found and we are at the end of the buffer
	
	if (research_next_will_wrap_around) {
		search_point.line = research_editor->buffer->real_line;
		research_next_will_wrap_around = false;
		move_regexp_search_forward();
	} else {
		research_next_will_wrap_around = true;
	}
}

static void research_stop_search(void) {
	pcre_free(research_regexp);
	pcre_free(research_regexp_extra);
	gtk_widget_hide(research_window);
	editor_grab_focus(research_editor);
}

static void research_perform_action(research_window_action_t *action) {
	switch (*action) {
	case RESEARCH_REPLACE:
		if (research_subst != NULL) {
			editor_replace_selection(research_editor, research_subst);
		}
		move_regexp_search_forward();
		break;
	case RESEARCH_NEXT:
		move_regexp_search_forward();
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
		if (research_subst != NULL) {
			editor_replace_selection(research_editor, research_subst);
		}
		move_regexp_search_forward();
		break;
	case GDK_KEY_n:
		move_regexp_search_forward();
		break;
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
	
	GtkWidget *button_replace = gtk_button_new_with_mnemonic("_Replace");
	GtkWidget *button_next = gtk_button_new_with_mnemonic("_Next");
	GtkWidget *button_stop = gtk_button_new_with_mnemonic("_Stop");

	gtk_button_set_use_underline(GTK_BUTTON(button_replace), TRUE);
	gtk_button_set_use_underline(GTK_BUTTON(button_next), TRUE);
	gtk_button_set_use_underline(GTK_BUTTON(button_stop), TRUE);
	
	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);

	gtk_container_add(GTK_CONTAINER(vbox), button_replace);
	gtk_container_add(GTK_CONTAINER(vbox), button_next);
	gtk_container_add(GTK_CONTAINER(vbox), button_stop);
	
	g_signal_connect(G_OBJECT(button_replace), "clicked", G_CALLBACK(research_button_clicked), research_window_results + RESEARCH_REPLACE);
	g_signal_connect(G_OBJECT(button_next), "clicked", G_CALLBACK(research_button_clicked), research_window_results + RESEARCH_NEXT);
	g_signal_connect(G_OBJECT(button_stop), "clicked", G_CALLBACK(research_button_clicked), research_window_results + RESEARCH_STOP);
	
	g_signal_connect(G_OBJECT(research_window), "key-press-event", G_CALLBACK(research_key_press_callback), NULL);
	g_signal_connect(G_OBJECT(research_window), "delete_event", G_CALLBACK(research_window_close_callback), NULL);
	
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(research_window))), vbox);
	
	gtk_window_set_default_size(GTK_WINDOW(research_window), 200, -1);
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
		return;
	}
	
	pcre_extra *re_extra = pcre_study(re, 0, &errptr);
	if (errptr != NULL) {
		char *msg;
		asprintf(&msg, "Syntax error (pcre_study) in regular expression [%s]: %s", regexp, errptr);
		if (msg == NULL) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}
		quick_message(editor, "Regexp Syntax Error", msg);
		free(msg);
		return;
	}
	
	//TODO: if a selection is active simply replace all occourences in the selection and return
	
	// if we get here there is no selection in the current editor's buffer and we should start an interactive search/replace
	
	research_editor = editor;
	research_regexp = re;
	research_regexp_extra = re_extra;
	research_subst = subst;
	research_next_will_wrap_around = false;
	
	//TODO: hide replace button if no substitution was specified
	
	gtk_widget_show_all(research_window);
	move_regexp_search_forward();
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