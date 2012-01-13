#include "interp.h"

#include <tcl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <pty.h>
#include <signal.h>

#include "global.h"
#include "columns.h"
#include "buffers.h"
#include "column.h"
#include "go.h"
#include "baux.h"
#include "jobs.h"
#include "shell.h"
#include "colors.h"
#include "history.h"
#include "cfg.h"
#include "builtin.h"
#include "research.h"
#include "wordcompl.h"
#include "lexy.h"

#define INITFILE ".teddy"

Tcl_Interp *interp;
editor_t *context_editor = NULL;
enum deferred_action deferred_action_to_return;

static int teddy_exit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'exit' command");
		return TCL_ERROR;
	}

	deferred_action_to_return = CLOSE_EDITOR;
	return TCL_OK;
}

static int teddy_new_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc > 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'new', usage: 'new <row|col>'");
		return TCL_ERROR;
	}

	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'new' command");
		return TCL_ERROR;
	}

	if (argc == 1) {
		editor_t *n = heuristic_new_frame(columnset, context_editor, null_buffer());
		if (n != NULL) {
			editor_grab_focus(n, true);
			deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
		}
		return TCL_OK;
	}

	if (strcmp(argv[1], "row") == 0) {
		editor_t *n = column_new_editor(context_editor->column, null_buffer());
		if (n != NULL) {
			editor_grab_focus(n, true);
			deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
		}
		return TCL_OK;
	} else if (strcmp(argv[1], "col") == 0) {
		editor_t *n = columns_new(columnset, null_buffer());
		if (n != NULL) {
			editor_grab_focus(n, true);
			deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
		}
		return TCL_OK;
	}

	{
		char *msg;
		asprintf(&msg, "Unknown option to 'new': '%s'", argv[1]);
		Tcl_AddErrorInfo(interp, msg);
		free(msg);
		return TCL_ERROR;
	}
}

static int teddy_pwf_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'pwf' command");
		return TCL_ERROR;
	}

	Tcl_SetResult(interp, context_editor->buffer->path, TCL_VOLATILE);
	return TCL_OK;
}

static int teddy_pwd_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'pwf' command");
		return TCL_ERROR;
	}

	Tcl_SetResult(interp, context_editor->buffer->wd, TCL_VOLATILE);
	return TCL_OK;
}

static int teddy_setcfg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to setcfg");
		return TCL_ERROR;
	}

	int a = 0, b = CONFIG_NUM-1, i = -1;
	while (a <= b) {
		i = (a + b) / 2;
		int r = strcmp(config_names[i], argv[1]);
		//printf("Comparing <%s> with <%s> (%d)\n", config_names[i], argv[1], r);
		if (r == 0) break;
		if (r > 0) {
			b = i-1;
		} else if (r < 0) {
			a = i+1;
		}
	}

	if ((a > b) || (i < 0)) {
		Tcl_AddErrorInfo(interp, "Unknown configuration option specified in setcfg");
		return TCL_ERROR;
	}

	config_item_t *ci = config + i;

	setcfg(ci, argv[2]);
	return TCL_OK;
}

static int teddy_bindkey_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	char *key, *value;

	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to setcfg");
		return TCL_ERROR;
	}

	key = malloc(sizeof(char) * (strlen(argv[1]) + 1));
	strcpy(key, argv[1]);
	value = malloc(sizeof(char) * (strlen(argv[2]) + 1));
	strcpy(value, argv[2]);

	g_hash_table_replace(keybindings, key, value);

	return TCL_OK;
}

static void set_tcl_result_to_lpoint(Tcl_Interp *interp, lpoint_t *point) {
	if (point->line != NULL) {
		char *r;
		asprintf(&r, "%d:%d", point->line->lineno+1, point->glyph+1);
		if (!r) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}
		Tcl_SetResult(interp, r, TCL_VOLATILE);
		free(r);
	} else {
		Tcl_SetResult(interp, "", TCL_VOLATILE);
	}
}

static int teddy_mark_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'mark' command");
		return TCL_ERROR;
	}

	if (argc == 1) {
		editor_mark_action(context_editor);
	    return TCL_OK;
	}

	if (argc != 2) {
	    Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'mark' command");
		return TCL_ERROR;
	}

	if (strcmp(argv[1], "get") == 0) {
		set_tcl_result_to_lpoint(interp, &(context_editor->buffer->mark));
		return TCL_OK;
	} else if (strcmp(argv[1], "start") == 0) {
		if (context_editor->buffer->mark.line == NULL) {
			buffer_set_mark_at_cursor(context_editor->buffer);
			gtk_widget_queue_draw(context_editor->drar);
		}
	} else if (strcmp(argv[1], "transient") == 0) {
		if (context_editor->buffer->mark.line == NULL) {
			buffer_set_mark_at_cursor(context_editor->buffer);
			context_editor->buffer->mark_transient = true;
			gtk_widget_queue_draw(context_editor->drar);
		}
	} else if (strcmp(argv[1], "stop") == 0) {
		if (context_editor->buffer->mark.line != NULL) {
			buffer_unset_mark(context_editor->buffer);
			gtk_widget_queue_draw(context_editor->drar);
		}
	} else if (strcmp(argv[1], "words") == 0) {
		if (context_editor->buffer->mark.line == NULL) {
			buffer_set_mark_at_cursor(context_editor->buffer);
		}
		buffer_change_select_type(context_editor->buffer, BST_WORDS);
		editor_complete_move(context_editor, FALSE);
	} else if (strcmp(argv[1], "lines") == 0) {
		if (context_editor->buffer->mark.line == NULL) {
			buffer_set_mark_at_cursor(context_editor->buffer);
		}
		buffer_change_select_type(context_editor->buffer, BST_LINES);
		editor_complete_move(context_editor, FALSE);
	} else {
		Tcl_AddErrorInfo(interp, "Called mark command with unknown argument");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int teddy_cursor_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'cursor' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Too many arguments to 'cursor' command");
		return TCL_ERROR;
	}

	set_tcl_result_to_lpoint(interp, &(context_editor->buffer->cursor));
	return TCL_OK;
}

static int teddy_cb_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'cb' command");
		return TCL_ERROR;
	}

	if (argc == 2) {
		if (strcmp(argv[1], "copy") == 0) {
			editor_copy_action(context_editor);
		} else if (strcmp(argv[1], "cut") == 0) {
			editor_cut_action(context_editor);
		} else if (strcmp(argv[1], "paste") == 0) {
			editor_insert_paste(context_editor, default_clipboard);
		} else if (strcmp(argv[1], "ppaste") == 0) {
			editor_insert_paste(context_editor, selection_clipboard);
		} else {
			Tcl_AddErrorInfo(interp, "Wrong argument to 'cb' command");
			return TCL_ERROR;
		}
	} else if (argc == 3) {
		if (strcmp(argv[1], "put") == 0) {
			gtk_clipboard_set_text(default_clipboard, argv[2], -1);
		} else if (strcmp(argv[1], "pput") == 0) {
			gtk_clipboard_set_text(selection_clipboard, argv[2], -1);
		} else {
			Tcl_AddErrorInfo(interp, "Wrong argument to 'cb' command");
			return TCL_ERROR;
		}
	} else {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'cb' command");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int teddy_save_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'save' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'save' command");
		return TCL_ERROR;
	}

	editor_save_action(context_editor);

	return TCL_OK;
}

static int teddy_bufman_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'bufman' command");
		return TCL_ERROR;
	}

	if (argc != 1) {\
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bufman' command");
		return TCL_ERROR;
	}

	buffers_show_window(context_editor);

	return TCL_OK;
}

static int teddy_undo_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'undo' command");
		return TCL_ERROR;
	}

	if (argc == 1) {
		editor_undo_action(context_editor);
		return TCL_OK;
	} else if (argc == 2) {
		if (strcmp(argv[1], "tag") == 0) {
			undo_node_t *u = undo_peek(&(context_editor->buffer->undo));
			if ((u == NULL) || (u->tag == NULL)) {
				Tcl_SetResult(interp, "", TCL_VOLATILE);
			} else {
				Tcl_SetResult(interp, u->tag, TCL_VOLATILE);
			}
		} else if (strcmp(argv[1], "fusenext") == 0) {
			context_editor->buffer->undo.please_fuse = true;
		} else {
			Tcl_AddErrorInfo(interp, "Wrong arguments to 'undo', usage; undo [tag [tagname] | fusenext | get <before|after>]");
			return TCL_ERROR;
		}
	} else if (argc == 3) {
		if (strcmp(argv[1], "tag") == 0) {
			undo_node_t *u = undo_peek(&(context_editor->buffer->undo));
			if (u != NULL) {
				if (u->tag != NULL) free(u->tag);
				u->tag = strdup(argv[2]);
			}
		} else if (strcmp(argv[1], "get") == 0) {
			undo_node_t *u = undo_peek(&(context_editor->buffer->undo));

			if (u == NULL) {
				Tcl_SetResult(interp, "", TCL_VOLATILE);
				return TCL_OK;
			}

			if (strcmp(argv[2], "before") == 0) {
				if (u->before_selection.text != NULL) {
					Tcl_SetResult(interp, u->before_selection.text, TCL_VOLATILE);
				} else {
					Tcl_SetResult(interp, "", TCL_VOLATILE);
				}
				return TCL_OK;
			} else if (strcmp(argv[2], "after") == 0) {
				if (u->after_selection.text != NULL) {
					Tcl_SetResult(interp, u->after_selection.text, TCL_VOLATILE);
				} else {
					Tcl_SetResult(interp, "", TCL_VOLATILE);
				}
				return TCL_OK;
			} else {
				Tcl_AddErrorInfo(interp, "Wrong argument to 'undo get', expected before or after");
				return TCL_ERROR;
			}
		} else {
			Tcl_AddErrorInfo(interp, "Wrong arguments to 'undo', usage; undo [tag [tagname]]");
			return TCL_ERROR;
		}
	}


	return TCL_OK;
}

static int teddy_search_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'search' command");
		return TCL_ERROR;
	}

	if (argc > 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'search' command");
		return TCL_ERROR;
	}

	editor_start_search(context_editor, (argc == 1) ? NULL : argv[1]);

	return TCL_OK;
}

static int teddy_focuscmd_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'focuscmd' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'focuscmd' command");
		return TCL_ERROR;
	}

	gtk_widget_grab_focus(context_editor->entry);

	return TCL_OK;
}

enum teddy_move_command_operation_t {
	TMCO_NONE = 0,
	TMCO_DEL,
	TMCO_CUT
};

static int teddy_move_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int next;
	enum teddy_move_command_operation_t operation = TMCO_NONE;
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'move' command");
		return TCL_ERROR;
	}

	if ((argc < 3) || (argc > 4)) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'move' command, usage: move <prev|next> <char|wnwa|softline> [del|cut]");
		return TCL_ERROR;
	}

	next = (strcmp(argv[1], "next") == 0);

	if (argc == 4) {
		if (strcmp(argv[3], "del") == 0) {
			operation = TMCO_DEL;
		} else if (strcmp(argv[3], "cut") == 0) {
			operation = TMCO_CUT;
		} else {
			Tcl_AddErrorInfo(interp, "Unknown action argument to 'move' command");
			return TCL_ERROR;
		}
	}

	if (operation != TMCO_NONE) {
		editor_mark_action(context_editor);
	}

	if (strcmp(argv[2], "char") == 0) {
		editor_move_cursor(context_editor, 0, next ? 1 : -1, MOVE_NORMAL, TRUE);
	} else if (strcmp(argv[2], "softline") == 0) {
		editor_move_cursor(context_editor, next ? 1 : -1, 0, MOVE_NORMAL, TRUE);
	} else if (strcmp(argv[2], "line") == 0) {
		real_line_t *n = NULL;
		n = next ? context_editor->buffer->cursor.line->next : context_editor->buffer->cursor.line->prev;
		if (n != NULL) {
			context_editor->buffer->cursor.line = n;
			editor_complete_move(context_editor, TRUE);
		}
	} else if (strcmp(argv[2], "wnwa") == 0) {
		if (next)
			buffer_aux_wnwa_next(context_editor->buffer);
		else
			buffer_aux_wnwa_prev(context_editor->buffer);
		editor_complete_move(context_editor, TRUE);
	} else {
		if (operation != TMCO_NONE)
		Tcl_AddErrorInfo(interp, "Unknown argument to 'move' command");
		return TCL_ERROR;
	}

	switch(operation) {
	case TMCO_DEL:
		editor_replace_selection(context_editor, "");
		break;
	case TMCO_CUT:
		editor_cut_action(context_editor);
		break;
	default: // TMCO_NONE -- nothing is done
		break;
	}

	return TCL_OK;
}

static int teddy_gohome_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'gohome' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'gohome' command");
		return TCL_ERROR;
	}

	buffer_aux_go_first_nonws_or_0(context_editor->buffer);
	editor_complete_move(context_editor, TRUE);

	return TCL_OK;
}

static void waitall(void) {
	for (;;) {
		int status;
		pid_t done = wait(&status);
		if (done == -1) {
			if (errno == ECHILD) break; // no more child processes
		} else {
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
				fprintf(stderr, "pid %d failed\n", done);
				break;
			}
		}
	}
}

static int teddy_backgrounded_bg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	pid_t child;

	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bg' command");
		return TCL_ERROR;
	}

	child = fork();
	if (child == -1) {
		Tcl_AddErrorInfo(interp, "Fork failed");
		return TCL_ERROR;
	} else if (child != 0) {
		/* parent code */

		return TCL_OK;
	}

	{
		int code = Tcl_Eval(interp, argv[1]);
		if (code != TCL_OK) {
			Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
			Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
			Tcl_Obj *stackTrace;
			Tcl_IncrRefCount(key);
			Tcl_DictObjGet(NULL, options, key, &stackTrace);
			Tcl_DecrRefCount(key);

			fprintf(stderr, "TCL Exception: %s\n", Tcl_GetString(stackTrace));
			waitall();
			exit(EXIT_FAILURE);
		} else {
			waitall();
			exit(atoi(Tcl_GetStringResult(interp)));
		}
	}

	exit(EXIT_SUCCESS); // the child's life end's here (if we didn't exec something before)
}

static int teddy_setenv_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'setenv' command");
		return TCL_ERROR;
	}

	if (setenv(argv[1], argv[2], 1) == -1) {
		Tcl_AddErrorInfo(interp, "Error executing 'setenv' command");
		return TCL_ERROR;
	} else {
		return TCL_OK;
	}

}

static int teddy_bg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	pid_t child;
	int masterfd;
	buffer_t *buffer;
	struct termios term;

	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'bg' command");
		return TCL_ERROR;
	}

	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bg' command");
		return TCL_ERROR;
	}

	buffer = buffers_get_buffer_for_process();
	if (context_editor->buffer != buffer)
		buffer_cd(buffer, context_editor->buffer->wd);

	go_to_buffer(context_editor, buffer, -1);

	bzero(&term, sizeof(struct termios));

	term.c_iflag = IGNCR | IUTF8;
	term.c_oflag = ONLRET;

	child = forkpty(&masterfd, NULL, &term, NULL);
	if (child == -1) {
		Tcl_AddErrorInfo(interp, "Fork failed");
		return TCL_ERROR;
	} else if (child != 0) {
		/* parent code */

		if (!jobs_register(child, masterfd, buffer, argv[1])) {
			Tcl_AddErrorInfo(interp, "Registering job failed, probably exceeded the maximum number of jobs available");
			return TCL_ERROR;
		}

		return TCL_OK;
	}

	/* child code here */

	setenv("TERM", "ansi", 1);
	setenv("PAGER", "", 1);
	setenv("SHELL", "teddy", 1);

	Tcl_CreateCommand(interp, "fdopen", &teddy_fdopen_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fdclose", &teddy_fdclose_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fddup2", &teddy_fddup2_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fdpipe", &teddy_fdpipe_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixfork", &teddy_posixfork_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixexec", &teddy_posixexec_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixwaitpid", &teddy_posixwaitpid_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixexit", &teddy_posixexit_command, (ClientData)NULL, NULL);
	Tcl_SetVar(interp, "backgrounded", "1", TCL_GLOBAL_ONLY);
	Tcl_CreateCommand(interp, "setenv", &teddy_setenv_command, (ClientData)NULL, NULL);

	Tcl_HideCommand(interp, "unknown", "_non_backgrounded_unknown");
	Tcl_Eval(interp, "rename backgrounded_unknown unknown");
	Tcl_HideCommand(interp, "bg", "_non_backgrounded_bg");
	Tcl_CreateCommand(interp, "bg", &teddy_backgrounded_bg_command, (ClientData)NULL, NULL);

	// Without this tcl screws up the newline character on its own output, it tries to output cr + lf and the terminal converts lf again, resulting in an output of cr + cr + lf, other c programs seem to behave correctly
	Tcl_Eval(interp, "fconfigure stdin -translation binary; fconfigure stdout -translation binary; fconfigure stderr -translation binary");

	{
		int code = Tcl_Eval(interp, argv[1]);
		if (code != TCL_OK) {
			Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
			Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
			Tcl_Obj *stackTrace;
			Tcl_IncrRefCount(key);
			Tcl_DictObjGet(NULL, options, key, &stackTrace);
			Tcl_DecrRefCount(key);

			fprintf(stderr, "TCL Exception: %s\n", Tcl_GetString(stackTrace));
			waitall();
			exit(EXIT_FAILURE);
		} else {
			waitall();
			exit(atoi(Tcl_GetStringResult(interp)));
		}
	}

	exit(EXIT_SUCCESS); // the child's life end's here (if we didn't exec something before)
}

static int teddy_rgbcolor_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	switch(argc) {
	case 2: {
		int ret = (long int)g_hash_table_lookup(x11colors, argv[1]);
		Tcl_SetObjResult(interp, Tcl_NewIntObj(ret));
		return TCL_OK;
	}
	case 4: {
		int ret = (atoi(argv[3]) << 16) + (atoi(argv[2]) << 8) + (atoi(argv[1]));
		Tcl_SetObjResult(interp, Tcl_NewIntObj(ret));
		return TCL_OK;
	}
	default:
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'rgbcolor' command");
		return TCL_ERROR;
	}
}

static int teddy_sendinput_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	job_t *job;
	int i;

	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute '<' command");
		return TCL_ERROR;
	}

	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to '<' command");
		return TCL_ERROR;
	}

	job = context_editor->buffer->job;

	if (job == NULL) {
		Tcl_AddErrorInfo(interp, "No job associated with this buffer, can not send input");
		return TCL_ERROR;
	}

	for (i = 1; i < argc; ++i) {
		if (write_all(job->masterfd, argv[i]) < 0) {
			Tcl_AddErrorInfo(interp, "Error sending input to process");
			return TCL_ERROR;
		}
		buffer_append(context_editor->buffer, argv[i], strlen(argv[i]), 0);
		if (i != argc-1) {
			if (write_all(job->masterfd, " ") < 0) {
				Tcl_AddErrorInfo(interp, "Error sending input to process");
				return TCL_ERROR;
			}
			buffer_append(context_editor->buffer, " ", strlen(" "), 0);
		}
	}
	if (write_all(job->masterfd, "\n") < 0) {
		Tcl_AddErrorInfo(interp, "Error sending input to process");
		return TCL_ERROR;
	}
	buffer_append(context_editor->buffer, "\n", strlen("\n"), 0);

	return TCL_OK;
}

static int teddy_interactarg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'interactarg' command");
		return TCL_ERROR;
	}

	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'interactarg'");
		return TCL_ERROR;
	}

	if (strlen(argv[1]) >= LOCKED_COMMAND_LINE_SIZE-1) {
		Tcl_AddErrorInfo(interp, "Argument of 'interactarg' is too long");
		return TCL_ERROR;
	}

	strcpy(context_editor->locked_command_line, argv[1]);
	set_label_text(context_editor);
	gtk_widget_grab_focus(context_editor->entry);
	deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
	return TCL_OK;
}

static int teddy_change_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'change' command");
		return TCL_ERROR;
	}

	switch (argc) {
	case 1:
		{
			lpoint_t start, end;
			buffer_get_selection(context_editor->buffer, &start, &end);
			char *text = buffer_lines_to_text(context_editor->buffer, &start, &end);
			Tcl_SetResult(interp, text, TCL_VOLATILE);
			free(text);
			return TCL_OK;
		}
	case 2:
		editor_replace_selection(context_editor, argv[1]);
		return TCL_OK;
	default:
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'selectlines'");
		return TCL_ERROR;
	}
}

static int parse_signum(const char *sigspec) {
	char *endptr = NULL;
	int signum = (int)strtol(sigspec, &endptr, 10);
	if ((endptr != NULL) && (*endptr == '\0')) {
		return signum;
	}

	if (!strcmp(sigspec, "HUP")) return SIGHUP;
	if (!strcmp(sigspec, "INT")) return SIGINT;
	if (!strcmp(sigspec, "QUIT")) return SIGQUIT;
	if (!strcmp(sigspec, "ILL")) return SIGILL;
	if (!strcmp(sigspec, "ABRT")) return SIGABRT;
	if (!strcmp(sigspec, "FPE")) return SIGFPE;
	if (!strcmp(sigspec, "KILL")) return SIGKILL;
	if (!strcmp(sigspec, "SEGV")) return SIGSEGV;
	if (!strcmp(sigspec, "PIPE")) return SIGPIPE;
	if (!strcmp(sigspec, "ALRM")) return SIGALRM;
	if (!strcmp(sigspec, "TERM")) return SIGTERM;
	if (!strcmp(sigspec, "USR1")) return SIGUSR1;
	if (!strcmp(sigspec, "USR2")) return SIGUSR2;
	if (!strcmp(sigspec, "CHLD")) return SIGCHLD;
	if (!strcmp(sigspec, "CONT")) return SIGCONT;
	if (!strcmp(sigspec, "STOP")) return SIGSTOP;
	if (!strcmp(sigspec, "TSTP")) return SIGTSTP;
	if (!strcmp(sigspec, "TTIN")) return SIGTTIN;
	if (!strcmp(sigspec, "TTOU")) return SIGTTOU;
	if (!strcmp(sigspec, "BUS")) return SIGBUS;
	if (!strcmp(sigspec, "POLL")) return SIGPOLL;
	if (!strcmp(sigspec, "PROF")) return SIGPROF;
	if (!strcmp(sigspec, "SYS")) return SIGSYS;
	if (!strcmp(sigspec, "TRAP")) return SIGTRAP;
	if (!strcmp(sigspec, "URG")) return SIGURG;
	if (!strcmp(sigspec, "VTALRM")) return SIGVTALRM;
	if (!strcmp(sigspec, "XCPU")) return SIGXCPU;
	if (!strcmp(sigspec, "XFSZ")) return SIGXFSZ;

	return -1;
}

static int teddy_kill_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc == 1) {
		if (context_editor == NULL) {
			/* automatic version of kill doesn't work unless there is an editor to automatically infer what to do from */
			Tcl_AddErrorInfo(interp, "Short version of 'kill' called without an active editor, provide an editor or more arguments");
			return TCL_ERROR;
		}

		if (context_editor->buffer->job != NULL) {
			kill(context_editor->buffer->job->child_pid, SIGTERM);
		} else {
			buffers_close(context_editor->buffer, context_editor->window);
			chdir(context_editor->buffer->wd);
		}

		return TCL_OK;
	} else if (argc == 2) {
		if (strcmp(argv[1], "process") == 0) {
			// kill current process with SIGTERM (check that context_editor is defined and associated buffer has a job)
			if (context_editor == NULL) {
				Tcl_AddErrorInfo(interp, "No active editor");
				return TCL_ERROR;
			}
			if (context_editor->buffer->job == NULL) {
				Tcl_AddErrorInfo(interp, "No active job");
				return TCL_ERROR;
			}
			kill(context_editor->buffer->job->child_pid, SIGTERM);
		} else if (strcmp(argv[1], "buffer") == 0) {
			// close buffer (check that context_editor is defined)
			if (context_editor == NULL) {
				Tcl_AddErrorInfo(interp, "No active editor");
				return TCL_ERROR;
			}
			buffers_close(context_editor->buffer, context_editor->window);
			chdir(context_editor->buffer->wd);
		} else if (argv[1][0] == '-') {
			// kill current process with specific signal (check that context_editor is defined and associated buffer has a job)
			if (context_editor == NULL) {
				Tcl_AddErrorInfo(interp, "No active editor");
				return TCL_ERROR;
			}
			if (context_editor->buffer->job == NULL) {
				Tcl_AddErrorInfo(interp, "No active job");
				return TCL_ERROR;
			}
			int signum = parse_signum(argv[1]+1);
			if (signum < 0) {
				Tcl_AddErrorInfo(interp, "Can not parse signal specification");
				return TCL_ERROR;
			}
			kill(context_editor->buffer->job->child_pid, signum);
		} else {
			// kill specified process with SIGTERM
			char *endptr = NULL;
			int target_pid = (int)strtol(argv[1], &endptr, 10);
			if ((endptr == NULL) || (*endptr != '\0')) {
				Tcl_AddErrorInfo(interp, "Invalid pid specification");
				return TCL_ERROR;
			}
			kill(target_pid, SIGTERM);
		}

		return TCL_OK;
	} else if (argc == 3) {
		// kill specified process with specified signal

		if (argv[1][0] != '-') {
			Tcl_AddErrorInfo(interp, "Expected a signal specification as first argument for two-argument kill");
			return TCL_ERROR;
		}
		int signum = parse_signum(argv[1]+1);
		if (signum < 0) {
			Tcl_AddErrorInfo(interp, "Can not parse signal specification");
			return TCL_ERROR;
		}
		char *endptr = NULL;
		int target_pid = (int)strtol(argv[2], &endptr, 10);
		if ((endptr == NULL) || (*endptr != '\0')) {
			Tcl_AddErrorInfo(interp, "Invalid pid specification");
			return TCL_ERROR;
		}
		kill(target_pid, signum);
	} else {
		Tcl_AddErrorInfo(interp, "Too many arguments to 'kill', consult documentation");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int teddy_refresh_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'refresh' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "No arguments should be supplied to refresh");
		return TCL_ERROR;
	}

	char *path = strdup(context_editor->buffer->path);
	if (path == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}

	if (null_buffer() == context_editor->buffer) return TCL_OK;

	int r = buffers_close(context_editor->buffer, context_editor->window);
	if (r == 0) return TCL_OK;

	buffer_t *buffer = go_file(NULL, path, false);
	if (buffer != NULL) editor_switch_buffer(context_editor, buffer);

	free(path);

	return TCL_OK;
}

void interp_init(void) {
	interp = Tcl_CreateInterp();
	if (interp == NULL) {
		printf("Couldn't create TCL interpreter\n");
		exit(EXIT_FAILURE);
	}

	Tcl_SetVar(interp, "backgrounded", "0", 0);
	Tcl_HideCommand(interp, "after", "hidden_after");
	Tcl_HideCommand(interp, "cd", "hidden_cd");
	Tcl_HideCommand(interp, "tcl_endOfWord", "hidden_tcl_endOfWord");
	Tcl_HideCommand(interp, "tcl_findLibrary", "hidden_tcl_findLibrary");
	Tcl_HideCommand(interp, "tcl_startOfPreviousWord", "hidden_tcl_startOfPreviousWord");
	Tcl_HideCommand(interp, "tcl_wordBreakAfter", "hidden_tcl_wordBreakAfter");
	Tcl_HideCommand(interp, "tcl_wordBreakBefore", "hidden_tcl_wordBreakBefore");

	Tcl_HideCommand(interp, "exit", "hidden_exit");
	Tcl_CreateCommand(interp, "exit", &teddy_exit_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "kill", &teddy_kill_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "setcfg", &teddy_setcfg_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "bindkey", &teddy_bindkey_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "new", &teddy_new_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "pwf", &teddy_pwf_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "pwd", &teddy_pwd_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "go", &teddy_go_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "refresh", &teddy_refresh_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "mark", &teddy_mark_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "cursor", &teddy_cursor_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "cb", &teddy_cb_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "save", &teddy_save_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "bufman", &teddy_bufman_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "undo", &teddy_undo_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "search", &teddy_search_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "focuscmd", &teddy_focuscmd_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "move", &teddy_move_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "gohome", &teddy_gohome_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "bg", &teddy_bg_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "<", &teddy_sendinput_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "rgbcolor", &teddy_rgbcolor_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddyhistory", &teddy_history_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "interactarg", &teddy_interactarg_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "s", &teddy_research_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "c", &teddy_change_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "wordcompl_dump", &teddy_wordcompl_dump_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "lexydef-create", &lexy_create_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexydef-append", &lexy_append_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexyassoc", &lexy_assoc_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexy_dump", &lexy_dump_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexycfg", &lexy_cfg_command, (ClientData)NULL, NULL);

	int code = Tcl_Eval(interp, BUILTIN_TCL_CODE);
	if (code != TCL_OK) {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);

		printf("Internal TCL Error: %s\n", Tcl_GetString(stackTrace));
		exit(EXIT_FAILURE);
	}
}

void interp_free(void) {
	Tcl_DeleteInterp(interp);
}

enum deferred_action interp_eval(editor_t *editor, const char *command) {
	int code;

	context_editor = editor;
	deferred_action_to_return = NOTHING;

	chdir(context_editor->buffer->wd);

	code = Tcl_Eval(interp, command);

	if (code != TCL_OK) {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);

		quick_message(context_editor, "TCL Error", Tcl_GetString(stackTrace));
		//TODO: if the error string is very long use a buffer instead
	} else {
		const char *result = Tcl_GetStringResult(interp);
		if (strcmp(result, "") != 0) {
			quick_message(context_editor, "TCL Result", result);
		}
	}

	Tcl_ResetResult(interp);

	return deferred_action_to_return;
}

void read_conf(void) {
	const char *home = getenv("HOME");
	char *name;

	asprintf(&name, "%s/%s", home, INITFILE);

	int code = Tcl_EvalFile(interp, name);

	if (code != TCL_OK) {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);

		printf("TCL Error: %s\n", Tcl_GetString(stackTrace));
		exit(EXIT_FAILURE);
	}

	free(name);
}
