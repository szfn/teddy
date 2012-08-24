#include "go.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "interp.h"
#include "baux.h"
#include "ctype.h"
#include "editor.h"
#include "buffers.h"
#include "global.h"
#include "columns.h"
#include "lexy.h"
#include "rd.h"
#include "top.h"

buffer_t *go_file(const char *filename, bool create, enum go_file_failure_reason *gffr) {
	char *p;
	asprintf(&p, "%s/%s", top_working_directory(), filename);
	alloc_assert(p);
	char *urp = realpath(p, NULL);
	free(p);

	*gffr = GFFR_OTHER;

	if (urp == NULL) {
		return NULL;
	}

	//printf("going file\n");

	buffer_t *buffer = buffers_find_buffer_from_path(urp);
	if (buffer != NULL) goto go_file_return;

	//printf("path: <%s>\n", urp);

	struct stat s;
	if (stat(urp, &s) != 0) {
		if (create) {
			FILE *f = fopen(urp, "w");
			if (f) { fclose(f); }
			if (stat(urp, &s) != 0) {
				buffer = NULL;
				goto go_file_return;
			}
		} else {
			buffer = NULL;
			goto go_file_return;
		}
	}

	buffer = buffer_create();

	if (S_ISDIR(s.st_mode)) {
		if (load_dir(buffer, urp) != 0) {
			buffer_free(buffer);
			buffer = NULL;
		}
	} else {
		int r = load_text_file(buffer, urp);
		if (r != 0) {
			if (r == -2) {
				*gffr = GFFR_BINARYFILE;
			}
			buffer_free(buffer);
			buffer = NULL;
		}
	}

	if (buffer != NULL) {
		buffers_add(buffer);
	}

go_file_return:
	free(urp);
	return buffer;
}

editor_t *go_to_buffer(editor_t *editor, buffer_t *buffer, bool take_over) {
	editor_t *target = NULL;
	find_editor_for_buffer(buffer, NULL, NULL, &target);

	if (target != NULL) {
		editor_grab_focus(target, true);
		return target;
	}

	if (take_over && (editor != NULL)) {
		editor_switch_buffer(editor, buffer);
		return editor;
	} else {
		tframe_t *spawning_frame;

		if (editor != NULL) find_editor_for_buffer(editor->buffer, NULL, &spawning_frame, NULL);
		else spawning_frame = NULL;

		tframe_t *target_frame = heuristic_new_frame(columnset, spawning_frame, buffer);

		if (target_frame != NULL) {
			target = GTK_TEDITOR(tframe_content(target_frame));
			editor_grab_focus(target, true);
		}

		return target;
	}

	return editor;
}

static int exec_go(const char *specifier, enum go_file_failure_reason *gffr) {
	char *sc = NULL;
	char *saveptr, *tok;
	buffer_t *buffer = NULL;
	int retval;

	*gffr = GFFR_OTHER;

	sc = malloc(sizeof(char) * (strlen(specifier) + 1));
	strcpy(sc, specifier);

	tok = strtok_r(sc, ":", &saveptr);
	if (tok == NULL) { retval = 0; goto exec_go_cleanup; }

	buffer = buffer_id_to_buffer(tok);
	if (buffer == NULL) {
		buffer = go_file(tok, false, gffr);
		//printf("buffer = %p gffr = %d\n", buffer, (int)(*gffr));
		if (buffer == NULL) { retval = 0; goto exec_go_cleanup; }
	}

	go_to_buffer(interp_context_editor(), buffer, false);
	// editor will be NULL here if the user decided to select an editor

	tok = strtok_r(NULL, ":", &saveptr);
	//printf("possible position token: %s\n", tok);
	if (tok == NULL) { retval = 1; goto exec_go_cleanup; }
	if (strlen(tok) == 0) { retval = 1; goto exec_go_cleanup; }

	char *tok2 = strtok_r(NULL, ":", &saveptr);
	char *subspecifier;

	if (tok2 != NULL) {
		asprintf(&subspecifier, "%s:%s", tok, tok2);
	} else {
		asprintf(&subspecifier, "%s", tok);
	}

	if (subspecifier[0] == '[') ++tok;
	if (subspecifier[strlen(subspecifier)-1] == ']') subspecifier[strlen(subspecifier)-1] = '\0';

	free(subspecifier);

	retval = 1;

 exec_go_cleanup:
	if (sc != NULL) free(sc);
	return retval;
}

int teddy_go_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	const char *goarg;

	if (argc == 2) {
		goarg = argv[1];
	} else {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'go', usage: 'go [-here|-select|-new] [<filename>]:[<position-specifier>]");
		return TCL_ERROR;
	}

	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'go' command");
		return TCL_ERROR;
	}

	if (strcmp(goarg, "&") == 0) {
		lpoint_t start, end;
		buffer_get_selection(interp_context_buffer(), &start, &end);

		if (start.line == NULL) {
			copy_lpoint(&start, &(interp_context_buffer()->cursor));
			mouse_open_action(interp_context_editor(), &start, NULL);
		} else {
			mouse_open_action(interp_context_editor(), &start, &end);
		}

		return TCL_OK;
	}

	enum go_file_failure_reason gffr;
	if (!exec_go(goarg, &gffr)) {
		if (gffr == GFFR_BINARYFILE) return TCL_OK;

		char *urp = unrealpath(top_working_directory(), goarg);
		char *msg;
		GtkWidget *dialog = gtk_dialog_new_with_buttons("Create file", GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(interp_context_editor()))), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Yes", 1, "No", 0, NULL);

		asprintf(&msg, "File [%s] does not exist, do you want to create it?", urp);
		gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), gtk_label_new(msg));
		free(msg);

		gtk_widget_show_all(dialog);

		if (gtk_dialog_run(GTK_DIALOG(dialog)) == 1) {
			FILE *f = fopen(urp, "w");

			gtk_widget_hide(dialog);

			if (!f) {
				asprintf(&msg, "Could not create [%s]", urp);
				quick_message("Error", msg);
				free(msg);
			} else {
				buffer_t *buffer = buffer_create();
				fclose(f);
				if (load_text_file(buffer, urp) != 0) {
					buffer_free(buffer);
					quick_message("Error", "Unexpected error during file creation");
				} else {
					buffers_add(buffer);
					go_to_buffer(interp_context_editor(), buffer, false);
				}
			}
		}

		gtk_widget_destroy(dialog);
		free(urp);
		return TCL_OK;
	} else {
		interp_context_editor()->center_on_cursor_after_next_expose = TRUE;
		lexy_update_for_move(interp_context_buffer(), interp_context_buffer()->cursor.line);
		gtk_widget_queue_draw(GTK_WIDGET(interp_context_editor()));
		return TCL_OK;
	}
}

void mouse_open_action(editor_t *editor, lpoint_t *start, lpoint_t *end) {
}
