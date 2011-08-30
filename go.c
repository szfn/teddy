#include "go.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "interp.h"
#include "baux.h"
#include "ctype.h"
#include "editor.h"
#include "buffers.h"
#include "global.h"
#include "columns.h"

GtkWidget *go_switch_label;
GtkWidget *go_switch_window;

typedef enum _swich_window_result_t {
    GO_CURRENT = 0,
    GO_NEW,
    GO_SELECT,
} switch_window_result_t;

switch_window_result_t switch_window_results[] = { GO_CURRENT, GO_NEW, GO_SELECT };

static int exec_char_specifier(const char *specifier, int really_exec, editor_t *context_editor) {
    long int n1;
    
    if ((strcmp(specifier, "^") == 0) || (strcmp(specifier, ":^") == 0)) {
        if (really_exec) buffer_aux_go_first_nonws(context_editor->buffer);
        return 1;
    }
    
    if ((strcmp(specifier, "$") == 0) || (strcmp(specifier, ":$") == 0)) {
        if (really_exec) buffer_aux_go_end(context_editor->buffer);
        return 1;
    }
    
    if (specifier[0] == ':') {
        n1 = strtol(specifier+1, NULL, 10);
        if (n1 != LONG_MIN) {
            if (really_exec) buffer_aux_go_char(context_editor->buffer, (int)n1);
            return 1;
        }
    }

    return 0;
}

static int isnumber(const char *str) {
    const char *c = str;
    
    if ((*c == '+') || (*c == '-')) ++c;
    
    for (; *c != '\0'; ++c) {
        if (!isdigit(*c)) return 0;
    }
    return 1;
}

static int exec_go_position(const char *specifier, editor_t *context_editor) {
    long int n1;
    char *pos;

    if (exec_char_specifier(specifier, 1, context_editor)) {
        editor_complete_move(context_editor, TRUE);
        return 1;
    }
    
    if (isnumber(specifier)) {
        n1 = strtol(specifier, NULL, 10);        
        printf("Line\n");
        buffer_aux_go_line(context_editor->buffer, (int)n1);
        editor_complete_move(context_editor, TRUE);
        return 1;
    }

    pos = strchr(specifier, ':');
    if ((pos != NULL) && (pos != specifier)) {
        char *c = alloca(sizeof(char) * (pos - specifier + 1));
        strncpy(c, specifier, pos - specifier);
        c[pos - specifier] = '\0';
        if (isnumber(c)) {
            n1 = strtol(c, NULL, 10);
            if (exec_char_specifier(pos, 0, context_editor)) {
                printf("Line + char\n");
                buffer_aux_go_line(context_editor->buffer, (int)n1);
                exec_char_specifier(pos, 1, context_editor);
                editor_complete_move(context_editor, TRUE);
                return 1;
            } else {
                return 0;
            }
        }
    }

    return 0;
}

editor_t *go_to_buffer(editor_t *editor, buffer_t *buffer) {
    editor_t *target = columns_get_buffer(buffer);
    int response;
    char *msg;
    if (target != NULL) {
        gtk_widget_grab_focus(target->drar);
        deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
        return target;
    }

    asprintf(&msg, "Show buffer [%s]:", buffer->name);
    gtk_label_set_text(GTK_LABEL(go_switch_label), msg);
    free(msg);
    
    gtk_widget_show_all(go_switch_window);
    response = gtk_dialog_run(GTK_DIALOG(go_switch_window));
    gtk_widget_hide(go_switch_window);

    printf("Response was: %d\n", response);

    switch(response) {
    case GO_CURRENT:
        editor_switch_buffer(editor, buffer);
        chdir(editor->buffer->wd);
        return editor;
        
    case GO_SELECT:
        selection_target_buffer = buffer;
        gtk_widget_queue_draw(editor->window);
        return editor;
        
    case GO_NEW:
    default: 
        target = heuristic_new_frame(editor, buffer);
        if (target != NULL) {
            gtk_widget_grab_focus(target->drar);
            deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
        }
        return target;
    }
    
    return editor;
}

int exec_go(const char *specifier) {
     char *sc = NULL;
     char *saveptr, *tok;
     char *urp;
     buffer_t *buffer = NULL;
     editor_t *editor = NULL;
     int retval;

     if (exec_go_position(specifier, context_editor)) {
         retval = 1;
         goto exec_go_cleanup;
     }

     sc = malloc(sizeof(char) * (strlen(specifier) + 1));
     strcpy(sc, specifier);

     tok = strtok_r(sc, ":", &saveptr);
     if (tok == NULL) { retval = 0; goto exec_go_cleanup; }

     urp = unrealpath(context_editor->buffer->path, tok);

     buffer = buffers_find_buffer_from_path(urp);
     if (buffer == NULL) {
         buffer = buffer_create(&library);
         if (load_text_file(buffer, urp) != 0) {
             buffer_free(buffer);
             retval = 0;
             goto exec_go_cleanup;
         } 
         buffers_add(buffer);
    }

    editor = go_to_buffer(context_editor, buffer);

    if (editor != NULL) {
        tok = strtok_r(sc, ":", &saveptr);
        if (tok == NULL) { retval = 1; goto exec_go_cleanup; }
        if (strlen(tok) == 0) { retval = 1; goto exec_go_cleanup; }
        
        if (tok[0] == '[') ++tok;
        if (tok[strlen(tok)-1] == ']') tok[strlen(tok)-1] = '\0';
        
        exec_go_position(tok, editor);
    }

    retval = 1;

 exec_go_cleanup:
    if (sc != NULL) free(sc);
    return retval;
}

int teddy_go_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'go', usage: 'go [<filename>][\":[\"[\"][<line-specifier>][\":\"][<column-specifier][\"]\"]]");
        return TCL_ERROR;
    }

    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'go' command");
        return TCL_ERROR;
    }

    if (!exec_go(argv[1])) {
		char *urp = unrealpath(context_editor->buffer->path, argv[1]);
		char *msg;
		GtkWidget *dialog = gtk_dialog_new_with_buttons("Create file", GTK_WINDOW(context_editor->window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Yes", 1, "No", 0, NULL);
		
		asprintf(&msg, "File [%s] does not exist, do you want to create it?", urp);
		gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), gtk_label_new(msg));
		free(msg);

		gtk_widget_show_all(dialog);

		if (gtk_dialog_run(GTK_DIALOG(dialog))) {
			FILE *f = fopen(urp, "w");

			gtk_widget_hide(dialog);

			if (!f) {
				asprintf(&msg, "Could not create [%s]", urp);
				quick_message(context_editor, "Error", msg);
				free(msg);
			} else {
				buffer_t *buffer = buffer_create(&library);
				fclose(f);
				if (load_text_file(buffer, urp) != 0) {
					buffer_free(buffer);
					quick_message(context_editor, "Error", "Unexpected error during file creation");
				} else {
					buffers_add(buffer);
					go_to_buffer(context_editor, buffer);
				}
			}
		}

		gtk_widget_destroy(dialog);
		free(urp);
		return TCL_OK;
    } else {
        editor_center_on_cursor(context_editor);
        gtk_widget_queue_draw(context_editor->drar);
        return TCL_OK;
    }
}

static void go_button_clicked(GtkButton *button, switch_window_result_t *response) {
    gtk_dialog_response(GTK_DIALOG(go_switch_window), *response);
}

static gboolean go_key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    int shift = event->state & GDK_SHIFT_MASK;
    int ctrl = event->state & GDK_CONTROL_MASK;
    int alt = event->state & GDK_MOD1_MASK;
    int super = event->state & GDK_SUPER_MASK;

    if (!shift && !ctrl && !alt && !super) {
        switch(event->keyval) {
        case GDK_KEY_c:
            gtk_dialog_response(GTK_DIALOG(go_switch_window), GO_CURRENT);
            return TRUE;
        case GDK_KEY_n:
            gtk_dialog_response(GTK_DIALOG(go_switch_window), GO_NEW);
            return TRUE;
        case GDK_KEY_s:
            gtk_dialog_response(GTK_DIALOG(go_switch_window), GO_SELECT);
            return TRUE;
        }
    }
    
    return FALSE;
}

void go_init(GtkWidget *window) {
    GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
    GtkWidget *button_current = gtk_button_new_with_mnemonic("_Current frame");
    GtkWidget *button_new = gtk_button_new_with_mnemonic("_New frame");
    GtkWidget *button_select = gtk_button_new_with_mnemonic("_Select frame");

    go_switch_label = gtk_label_new("Show buffer:");

    go_switch_window = gtk_dialog_new_with_buttons("Show Buffer", GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, NULL);

    gtk_label_set_justify(GTK_LABEL(go_switch_label), GTK_JUSTIFY_LEFT);

    gtk_button_set_use_underline(GTK_BUTTON(button_current), TRUE);
    gtk_button_set_use_underline(GTK_BUTTON(button_new), TRUE);
    gtk_button_set_use_underline(GTK_BUTTON(button_select), TRUE);    

    gtk_container_add(GTK_CONTAINER(vbox), go_switch_label);
    gtk_container_add(GTK_CONTAINER(vbox), button_current);
    gtk_container_add(GTK_CONTAINER(vbox), button_new);
    gtk_container_add(GTK_CONTAINER(vbox), button_select);

    g_signal_connect(G_OBJECT(button_current), "clicked", G_CALLBACK(go_button_clicked), switch_window_results + GO_CURRENT);
    g_signal_connect(G_OBJECT(button_new), "clicked", G_CALLBACK(go_button_clicked), switch_window_results + GO_NEW);
    g_signal_connect(G_OBJECT(button_select), "clicked", G_CALLBACK(go_button_clicked), switch_window_results + GO_SELECT);

    g_signal_connect(G_OBJECT(go_switch_window), "key-press-event", G_CALLBACK(go_key_press_callback), NULL);

    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(go_switch_window))), vbox);

    gtk_window_set_default_size(GTK_WINDOW(go_switch_window), 200, 100);
}
