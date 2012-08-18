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

static int exec_char_specifier(const char *specifier, int really_exec, buffer_t *buffer) {
	long int n1;

	if ((strcmp(specifier, "^") == 0) || (strcmp(specifier, ":^") == 0)) {
		if (really_exec) buffer_aux_go_first_nonws(buffer);
		return 1;
	}

	if ((strcmp(specifier, "$") == 0) || (strcmp(specifier, ":$") == 0)) {
		if (really_exec) buffer_aux_go_end(buffer);
		return 1;
	}

	if (specifier[0] == ':') {
		n1 = strtol(specifier+1, NULL, 10);
		if (n1 != LONG_MIN) {
			if (really_exec) buffer_aux_go_char(buffer, (int)(n1-1));
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

static int exec_go_position(const char *specifier, editor_t *context_editor, buffer_t *buffer) {
	long int n1;
	char *pos;

	if (exec_char_specifier(specifier, 1, buffer)) {
		if (context_editor != NULL)
			editor_complete_move(context_editor, TRUE);
		return 1;
	}

	if (isnumber(specifier)) {
		n1 = strtol(specifier, NULL, 10);
		//printf("Line\n");
		buffer_aux_go_line(buffer, (int)n1);
		if (context_editor != NULL)
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
			if (exec_char_specifier(pos, 0, buffer)) {
				//printf("Line + char\n");
				buffer_aux_go_line(buffer, (int)n1);
				exec_char_specifier(pos, 1, buffer);
				if (context_editor != NULL)
					editor_complete_move(context_editor, TRUE);
				return 1;
			} else {
				return 0;
			}
		}
	}

	return 0;
}

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
		deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
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
			deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
		}

		return target;
	}

	return editor;
}

static int exec_go(const char *specifier, enum go_file_failure_reason *gffr) {
	char *sc = NULL;
	char *saveptr, *tok;
	buffer_t *buffer = NULL;
	editor_t *editor = NULL;
	int retval;

	*gffr = GFFR_OTHER;

	if (exec_go_position(specifier, interp_context_editor(), interp_context_buffer())) {
	    retval = 1;
	    goto exec_go_cleanup;
	}

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

	editor = go_to_buffer(interp_context_editor(), buffer, false);
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

	exec_go_position(subspecifier, editor, buffer);

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

		char *urp = unrealpath(interp_context_buffer()->path, goarg);
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
					go_to_buffer(interp_context_editor(), buffer, -1);
				}
			}
		}

		gtk_widget_destroy(dialog);
		free(urp);
		return TCL_OK;
	} else {
		interp_context_editor()->center_on_cursor_after_next_expose = TRUE;
		lexy_update_for_move(interp_context_buffer(), interp_context_buffer()->cursor.line);
		gtk_widget_queue_draw(interp_context_editor()->drar);
		return TCL_OK;
	}
}

static bool mouse_open_select_like_file(editor_t *editor, lpoint_t *cursor, lpoint_t *start, lpoint_t *end) {
	start->glyph = 0;
	end->glyph = cursor->line->cap;

	start->line = cursor->line;
	end->line = cursor->line;

	for (int i = cursor->glyph-1; i >= 0; --i) {
		uint32_t code = start->line->glyph_info[i].code;
		if ((code == 0x20) || (code == 0x09) || (code == 0xe650) || (code == 0xe651) || (code == 0xe652)) {
			start->glyph = i+1;
			break;
		}
	}

	for (int i = cursor->glyph; i < start->line->cap; ++i) {
		uint32_t code = start->line->glyph_info[i].code;
		if ((code == 0x20) || (code == 0x09)) {
			end->glyph = i;
			break;
		}
	}

	//printf("start_glyph: %d end_glyph: %d\n", start->glyph, end->glyph);
	if (start->glyph > end->glyph) {
		return false;
	}

	return true;
}

void mouse_open_action(editor_t *editor, lpoint_t *start, lpoint_t *end) {
	if (buffer_aux_is_directory(editor->buffer)) {
		rd_open(editor);
		return;
	}

	lpoint_t changed_end;
	lpoint_t cursor;

	interp_context_editor_set(editor);

	copy_lpoint(&cursor, start);

	//printf("start_glyph: %d\n", start->glyph);

	if (end == NULL) {
		end = &changed_end;
		if (!mouse_open_select_like_file(editor, &cursor, start, end)) return;
	}

	if (end->line != start->line) {
		char *r = buffer_lines_to_text(editor->buffer, start, end);
		interp_eval(editor, r, true);
		free(r);
		return;
	}

	char *text = buffer_lines_to_text(editor->buffer, start, end);

	//printf("Open or search on selection [%s]\n", text);

	if (text == NULL) return;

	const char *eval_argv[] = { "mouse_go_preprocessing_hook", text };

	int code = Tcl_Eval(interp, Tcl_Merge(2, eval_argv));

	free(text);

	if (code == TCL_OK) {
		//printf("After processing hook: [%s]\n", Tcl_GetStringResult(interp));
	} else {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);

		fprintf(stderr, "TCL Exception: %s\n", Tcl_GetString(stackTrace));
		return;
	}

	char *go_arg = strdup(Tcl_GetStringResult(interp));
	alloc_assert(go_arg);

	enum go_file_failure_reason gffr;
	if (!exec_go(go_arg, &gffr)) {
		// if this succeeded we could open the file at the specified line and we are done
		// otherwise we check gffr

		if (gffr == GFFR_BINARYFILE) {
			// it's was a readable file but it was binary
			// we should just copy it in the command line

			char *ex = malloc(sizeof(char) * (strlen(go_arg) + 4));
			alloc_assert(ex);

			strcpy(ex, " {");
			strcat(ex, go_arg);
			strcat(ex, "}");

			// TODO:
			//editor_add_to_command_line(editor, ex);

			free(ex);
		} else {
			// we either couldn't read the file or it wasn't a file at all, just restrict to the word
			// around the cursor and call search

			copy_lpoint(start, &cursor);
			copy_lpoint(end, &cursor);

			buffer_aux_wnwa_prev_ex(start);
			buffer_aux_wnwa_next_ex(end);

			// artifact of how wnwa_prev works
			/*++(start->glyph);
			if (start->glyph > start->line->cap) start->glyph = start->line->cap;*/

			char *wordsel = buffer_lines_to_text(editor->buffer, start, end);
			//TODO
			//editor_start_search(context_editor, wordsel);
			free(wordsel);
		}
	}

	free(go_arg);

	interp_context_editor_set(NULL);
}
