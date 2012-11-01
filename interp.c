#include "interp.h"

#include <stdlib.h>
#include <tcl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <pty.h>
#include <signal.h>

#include "global.h"
#include "columns.h"
#include "buffers.h"
#include "column.h"
#include "jobs.h"
#include "colors.h"
#include "history.h"
#include "cfg.h"
#include "builtin.h"
#include "research.h"
#include "lexy.h"
#include "autoconf.h"
#include "top.h"
#include "iopen.h"
#include "tags.h"

Tcl_Interp *interp;
editor_t *the_context_editor = NULL;
buffer_t *the_context_buffer = NULL;

static void interp_context_editor_set(editor_t *editor) {
	the_context_editor = editor;
	the_context_buffer = (editor != NULL) ? editor->buffer : NULL;
}

static void interp_context_buffer_set(buffer_t *buffer) {
	the_context_editor = NULL;
	the_context_buffer = buffer;
}

static int teddy_cd_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 2), "cd");

	top_cd(argv[1]);

	const char *open_cmd_argv[] = { "buffer", "open", "." };
	teddy_buffer_command(client_data, interp, 3, open_cmd_argv);

	GList *column_list = gtk_container_get_children(GTK_CONTAINER(columnset));
	for (GList *column_cur = column_list; column_cur != NULL; column_cur = column_cur->next) {
		GList *frame_list = gtk_container_get_children(GTK_CONTAINER(column_cur->data));
		for (GList *frame_cur = frame_list; frame_cur != NULL; frame_cur = frame_cur->next) {
			tframe_t *frame = GTK_TFRAME(frame_cur->data);
			GtkWidget *cur_content = tframe_content(frame);
			if (GTK_IS_TEDITOR(cur_content)) {
				set_label_text(GTK_TEDITOR(cur_content));
				gtk_widget_queue_draw(GTK_WIDGET(frame));
			}
		}
		g_list_free(frame_list);
	}
	g_list_free(column_list);

	return TCL_OK;
}

static int teddy_in_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc < 3), "in");

	char *wd = get_current_dir_name();
	chdir(argv[1]);

	char *cmd = Tcl_Merge(argc-2, argv+2);
	int code = Tcl_Eval(interp, cmd);
	Tcl_Free(cmd);

	chdir(wd);
	free(wd);

	return code;
}

static int teddy_pwf_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	HASBUF("pwf");

	Tcl_SetResult(interp, interp_context_buffer()->path, TCL_VOLATILE);
	return TCL_OK;
}

static int teddy_iopen_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	iopen();
	return TCL_OK;
}

static int teddy_setcfg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc < 3), "setcfg");

	const char *name = NULL, *value = NULL;
	config_t *config = (interp_context_buffer() != NULL) ? &(interp_context_buffer()->config) : &global_config;

	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			if (strcmp(argv[i], "-global") == 0) {
				config = &global_config;
			} else {
				Tcl_AddErrorInfo(interp, "Unknown argument to setcfg");
				return TCL_ERROR;
			}
		} else {
			if (name == NULL) name = argv[i];
			else if (value == NULL) value = argv[i];
		}
	}

	if ((name == NULL) || (value == NULL)) {
		Tcl_AddErrorInfo(interp, "Couldn't find the two arguments for setcfg");
		return TCL_ERROR;
	}

	int i;
	for (i = 0; i < CONFIG_NUM; ++i) {
		if (strcmp(config_names[i], name) == 0) break;
	}

	if (i >= CONFIG_NUM) {
		Tcl_AddErrorInfo(interp, "Unknown configuration option specified in setcfg");
		return TCL_ERROR;
	}

	config_set(config, i, (char *)value);

	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		buffer_config_changed(buffers[i]);
	}

	if (columnset != NULL)
		gtk_widget_queue_draw(GTK_WIDGET(columnset));

	return TCL_OK;
}

static int teddy_bindkey_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 3), "bindkey")

	char *key = malloc(sizeof(char) * (strlen(argv[1]) + 1));
	strcpy(key, argv[1]);
	char *value = malloc(sizeof(char) * (strlen(argv[2]) + 1));
	strcpy(value, argv[2]);

	g_hash_table_replace(keybindings, key, value);

	return TCL_OK;
}

static int teddy_cb_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc == 2) {
		if (strcmp(argv[1], "get") == 0) {
			gchar *text = gtk_clipboard_wait_for_text(default_clipboard);
			Tcl_SetResult(interp, (text != NULL) ? text : "", TCL_VOLATILE);
			return TCL_OK;
		} else if (strcmp(argv[1], "pget") == 0) {
			gchar *text = gtk_clipboard_wait_for_text(selection_clipboard);
			Tcl_SetResult(interp, (text != NULL) ? text : "", TCL_VOLATILE);
			return TCL_OK;
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

static int teddy_undo_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	HASED("undo");

	if (argc == 1) {
		editor_undo_action(interp_context_editor(), false);
		return TCL_OK;
	} else if (argc == 2) {
		if (strcmp(argv[1], "tag") == 0) {
			undo_node_t *u = undo_peek(&(interp_context_buffer()->undo));
			if ((u == NULL) || (u->tag == NULL)) {
				Tcl_SetResult(interp, "", TCL_VOLATILE);
			} else {
				Tcl_SetResult(interp, u->tag, TCL_VOLATILE);
			}
		} else if (strcmp(argv[1], "fusenext") == 0) {
			interp_context_buffer()->undo.please_fuse = true;
		} else if (strcmp(argv[1], "redo") == 0) {
			editor_undo_action(interp_context_editor(), true);
		} else {
			Tcl_AddErrorInfo(interp, "Wrong arguments to 'undo', usage; undo [tag [tagname] | fusenext | get <before|after>]");
			return TCL_ERROR;
		}
	} else if (argc == 3) {
		if (strcmp(argv[1], "tag") == 0) {
			undo_node_t *u = undo_peek(&(interp_context_buffer()->undo));
			if (u != NULL) {
				if (u->tag != NULL) free(u->tag);
				u->tag = strdup(argv[2]);
			}
		} else if (strcmp(argv[1], "get") == 0) {
			undo_node_t *u = undo_peek(&(interp_context_buffer()->undo));

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
	HASED("search");
	ARGNUM((argc > 2), "search");

	editor_start_search(interp_context_editor(), SM_LITERAL, (argc == 1) ? NULL : argv[1]);

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
	ARGNUM((argc != 2), "bg");

	pid_t child = fork();
	if (child == -1) {
		Tcl_AddErrorInfo(interp, "Fork failed");
		return TCL_ERROR;
	} else if (child != 0) {
		/* parent code */

		return TCL_OK;
	}

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

	exit(EXIT_SUCCESS); // the child's life end's here (if we didn't exec something before)
}

static int teddy_setenv_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 3), "setenv");

	if (setenv(argv[1], argv[2], 1) == -1) {
		Tcl_AddErrorInfo(interp, "Error executing 'setenv' command");
		return TCL_ERROR;
	} else {
		return TCL_OK;
	}

}

static void configure_for_bg_execution(Tcl_Interp *interp) {
	interp_context_editor_set(NULL);

	setenv("TERM", "ansi", 1);
	setenv("PAGER", "cat", 1);

	Tcl_SetVar(interp, "backgrounded", "1", TCL_GLOBAL_ONLY);
	Tcl_CreateCommand(interp, "setenv", &teddy_setenv_command, (ClientData)NULL, NULL);

	Tcl_HideCommand(interp, "unknown", "_non_backgrounded_unknown");
	Tcl_Eval(interp, "rename backgrounded_unknown unknown");
	Tcl_HideCommand(interp, "bg", "_non_backgrounded_bg");
	Tcl_CreateCommand(interp, "bg", &teddy_backgrounded_bg_command, (ClientData)NULL, NULL);

	// Without this tcl screws up the newline character on its own output, it tries to output cr + lf and the terminal converts lf again, resulting in an output of cr + cr + lf, other c programs seem to behave correctly
	Tcl_Eval(interp, "fconfigure stdin -translation binary; fconfigure stdout -translation binary; fconfigure stderr -translation binary");
}

static int teddy_bg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	pid_t child;
	int masterfd;
	struct termios term;

	const char *codearg;
	buffer_t *buffer = NULL;
	if (argc == 2) {
		if (strcmp(argv[1], "-setup") == 0) {
			configure_for_bg_execution(interp);
			return TCL_OK;
		} else {
			codearg = argv[1];
		}
	} else if (argc == 3) {
		buffer = buffer_id_to_buffer(argv[1]);
		if (buffer == NULL) {
			buffer = buffers_create_with_name(strdup(argv[1]));
			if (buffer != NULL) go_to_buffer(interp_context_editor(), buffer, false);
		}
		codearg = argv[2];
	} else {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bg' command");
		return TCL_ERROR;
	}


	bzero(&term, sizeof(struct termios));

	term.c_iflag = IGNCR | IUTF8;
	term.c_oflag = ONLRET;

	child = forkpty(&masterfd, NULL, &term, NULL);
	if (child == -1) {
		Tcl_AddErrorInfo(interp, "Fork failed");
		return TCL_ERROR;
	} else if (child != 0) {
		/* parent code */

		if (!jobs_register(child, masterfd, buffer, codearg)) {
			Tcl_AddErrorInfo(interp, "Registering job failed, probably exceeded the maximum number of jobs available");
			return TCL_ERROR;
		}

		return TCL_OK;
	}

	/* child code here */

	configure_for_bg_execution(interp);

	int code = Tcl_Eval(interp, codearg);
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

static int teddy_change_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	HASBUF("change");

	switch (argc) {
	case 1:
		{
			char *text = buffer_get_selection_text(interp_context_buffer());
			Tcl_SetResult(interp, (text != NULL ? text : ""), TCL_VOLATILE);
			if (text != NULL) free(text);
			return TCL_OK;
		}
	case 2:
		buffer_replace_selection(interp_context_buffer(), argv[1]);
		if (interp_context_editor() != NULL) gtk_widget_queue_draw(GTK_WIDGET(interp_context_editor()));
		return TCL_OK;
	default:
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'selectlines'");
		return TCL_ERROR;
	}
}

static bool move_command_ex(const char *sin, int *p, int ref, enum movement_type_t default_glyph_motion) {
	if (strcmp(sin, "nil") == 0) {
		if (ref < 0) {
			Tcl_AddErrorInfo(interp, "Attempted to null cursor in 'm' command");
			return false;
		} else {
			*p = -1;
			return true;
		}
	}

	char *s = strdup(sin);
	alloc_assert(s);

	char *saveptr;
	char *first = strtok_r(s, ":", &saveptr);
	char *second = strtok_r(NULL, ":", &saveptr);
	char *expectfailure = (second != NULL) ? strtok_r(NULL, ":", &saveptr) : NULL;

	if (first == NULL) goto move_command_ex_bad_argument;
	if (expectfailure != NULL) goto move_command_ex_bad_argument;

	enum movement_type_t lineflag = MT_ABS, colflag = MT_ABS;
	int lineno = 0, colno = 0;

	if (strcmp(first, "$") == 0) {
		lineflag = MT_END;
	} else {
		bool forward = true;
		switch (first[0]) {
		case '+':
			lineflag = MT_REL;
			++first;
			break;
		case '-':
			lineflag = MT_REL;
			forward = false;
			++first;
			break;
		default:
			lineflag = MT_ABS;
			break;
		}

		lineno = atoi(first);
		if (lineno < 0) goto move_command_ex_bad_argument;
		if (!forward) lineno = -lineno;
	}

	if (second == NULL) {
		colflag = default_glyph_motion;
		colno = 0;
	} else if (strcmp(second, "$") == 0) {
		colflag = MT_END;
	} else if (strcmp(second, "^") == 0) {
		colflag = MT_START;
	} else if ((strcmp(second, "^1") == 0) || (strcmp(second, "1^") == 0)) {
		colflag = MT_HOME;
	} else if (strlen(second) == 0) {
		goto move_command_ex_bad_argument;
	} else {
		bool words = false, forward = true;
		if (second[strlen(second)-1] == 'w') {
			words = true;
			second[strlen(second)-1] = '\0';
		}
		switch (second[0]) {
		case '+':
			colflag = words ? MT_RELW : MT_REL;
			++second;
			break;
		case '-':
			colflag = words ? MT_RELW : MT_REL;
			forward = false;
			++second;
			break;
		default:
			if (words) goto move_command_ex_bad_argument;
			colflag = MT_ABS;
			break;
		}

		colno = atoi(second);
		if (colno < 0) goto move_command_ex_bad_argument;
		if (!forward) colno = -colno;
	}

	if (*p < 0) {
		if (ref >= 0) {
			*p = ref;
		}
	}

	if (*p < 0) {
		if (lineflag == MT_REL) goto move_command_relative_with_nil;
		if (colflag == MT_REL) goto move_command_relative_with_nil;
	}

	bool rl = buffer_move_point_line(interp_context_buffer(), p, lineflag, lineno);
	bool rc = buffer_move_point_glyph(interp_context_buffer(), p, colflag, colno);

	free(s);

	Tcl_SetResult(interp, (rl && rc) ? "true" : "false", TCL_VOLATILE);

	return true;

move_command_ex_bad_argument: {
	char *msg;
	asprintf(&msg, "Malformed argument passed to 'm' command: '%s'", sin);
	alloc_assert(msg);
	Tcl_AddErrorInfo(interp, msg);
	free(msg);
	free(s);
	return false; }

move_command_relative_with_nil: {
	char *msg;
	asprintf(&msg, "Argument passed to 'm' specifies relative movement but cursor isn't set: '%s'", sin);
	alloc_assert(msg);
	Tcl_AddErrorInfo(interp, msg);
	free(msg);
	free(s);
	return false; }
}

static void sort_mark_cursor(buffer_t *buffer) {
	if (buffer->mark < 0) return;
	if (buffer->mark < buffer->cursor) return;

	int swap;
	swap = buffer->mark;
	buffer->mark = buffer->cursor;
	buffer->cursor = swap;
}

static int teddy_move_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
#define MOVE_MARK(argument, d) { \
	if (!move_command_ex(argument, &(interp_context_buffer()->mark), interp_context_buffer()->cursor, d)) { \
		return TCL_ERROR; \
	} \
}

#define MOVE_CURSOR(argument, d) {\
	if (!move_command_ex(argument, &(interp_context_buffer()->cursor), -1, d)) { \
		return TCL_ERROR; \
	} \
}

#define MOVE_MARK_CURSOR(mark_argument, cursor_argument) {\
	MOVE_MARK(mark_argument, MT_START);\
	MOVE_CURSOR(cursor_argument, MT_END);\
	interp_context_buffer()->savedmark = interp_context_buffer()->mark;\
}

	HASBUF("move");

	switch (argc) {
	case 1:
		interp_return_point_pair(interp_context_buffer(), interp_context_buffer()->mark, interp_context_buffer()->cursor);
		return TCL_OK;
	case 2:
		if (strcmp(argv[1], "all") == 0) {
			MOVE_MARK_CURSOR("1:1", "$:$");
		} else if (strcmp(argv[1], "sort") == 0) {
			sort_mark_cursor(interp_context_buffer());
		} else if (strcmp(argv[1], "line") == 0) {
			sort_mark_cursor(interp_context_buffer());
			MOVE_MARK_CURSOR("+0:1", "+0:$");
		} else {
			// not a shortcut, actually movement command to execute
			MOVE_CURSOR(argv[1], MT_START);
		}
		break;
	case 3:
		MOVE_MARK_CURSOR(argv[1], argv[2]);
		break;
	default:
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'move' command");
		return TCL_ERROR;
	}

	return TCL_OK;
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
	ARGNUM((argc > 3), "kill");

	bool pid_specified = false;
	pid_t pid;
	int signum = SIGTERM;

	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			int signum = parse_signum(argv[i]+1);
			if (signum < 0) {
				Tcl_AddErrorInfo(interp, "Can not parse signal specification");
				return TCL_ERROR;
			}
		} else {
			int ipid = atoi(argv[i]);
			if (ipid < 1) {
				Tcl_AddErrorInfo(interp, "Can not parse pid number");
				return TCL_ERROR;
			}
			pid_specified = true;
			pid = ipid;
		}
	}

	if (!pid_specified) {
		if (interp_context_buffer() == NULL) return TCL_OK;
		if (interp_context_buffer()->job == NULL) return TCL_OK;
		pid = interp_context_buffer()->job->child_pid;
	}

	kill(pid, signum);

	return TCL_OK;
}

static int teddy_inpath_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to teddy_intl::inpath");
		return TCL_ERROR;
	}

	Tcl_SetResult(interp, in_external_commands(argv[1]) ? "true" : "false", TCL_VOLATILE);
	return TCL_OK;
}

static int teddy_session_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to session command");
		return TCL_ERROR;
	}

	if (strcmp(argv[1], "tie") == 0) {
		if (tied_session != NULL) free(tied_session);
		tied_session = strdup(argv[2]);
		alloc_assert(tied_session);
		save_tied_session();
		return TCL_OK;
	} else if (strcmp(argv[1], "load") == 0) {
		if (tied_session != NULL) free(tied_session);
		tied_session = strdup(argv[2]);
		alloc_assert(tied_session);
		load_tied_session();
		return TCL_OK;
	} else if (strcmp(argv[1], "directory") == 0) {
		char *sessiondir = session_directory();
		Tcl_SetResult(interp, sessiondir, TCL_VOLATILE);
		free(sessiondir);
		return TCL_OK;
	} else {
		Tcl_AddErrorInfo(interp, "Wrong subcommand to session command");
		return TCL_ERROR;
	}
}

static int teddy_fdopen_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int i, intr;
	int flags = 0;
	Tcl_Obj *r;

	ARGNUM((argc < 3), "fdopen");

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "-append") == 0) {
			flags |= O_APPEND;
		} else if (strcmp(argv[i], "-async") == 0) {
			flags |= O_ASYNC;
		} else if (strcmp(argv[i], "-cloexec") == 0) {
			flags |= O_CLOEXEC;
		} else if (strcmp(argv[i], "-creat") == 0) {
			flags |= O_CREAT;
		} else if (strcmp(argv[i], "-direct") == 0) {
			flags |= O_DIRECT;
		} else if (strcmp(argv[i], "-directory") == 0) {
			flags |= O_DIRECTORY;
		} else if (strcmp(argv[i], "-excl") == 0) {
			flags |= O_EXCL;
		} else if (strcmp(argv[i], "-largefile") == 0) {
			flags |= O_LARGEFILE;
		} else if (strcmp(argv[i], "-noatime") == 0) {
			flags |= O_NOATIME;
		} else if (strcmp(argv[i], "-noctty") == 0) {
			flags |= O_NOCTTY;
		} else if (strcmp(argv[i], "-nofollow") == 0) {
			flags |= O_NOFOLLOW;
		} else if (strcmp(argv[i], "-nonblock") == 0) {
			flags |= O_NONBLOCK;
		} else if (strcmp(argv[i], "-sync") == 0) {
			flags |= O_SYNC;
		} else if (strcmp(argv[i], "-trunc") == 0) {
			flags |= O_TRUNC;
		} else if (strcmp(argv[i], "-rdonly") == 0) {
			flags |= O_RDONLY;
		} else if (strcmp(argv[i], "-wronly") == 0) {
			flags |= O_WRONLY;
		} else if (strcmp(argv[i], "-rdwr") == 0) {
			flags |= O_RDWR;
		} else {
			Tcl_AddErrorInfo(interp, "Unknown option passed to 'fdopen' command");
			return TCL_ERROR;
		}
	}

	if (i != argc - 1) {
		Tcl_AddErrorInfo(interp, "Extraneous arguments given to 'fdopen' command");
		return TCL_ERROR;
	}

	intr = open(argv[i], flags, S_IRWXU | S_IRWXG);
	if (intr == -1) {
#define FDOPEN_ERROR ": "
		char *err = strerror(errno);
		char *e = malloc(sizeof(char) * (strlen(err) + strlen(FDOPEN_ERROR) + strlen(argv[i]) + 1));
		strcpy(e, argv[i]);
		strcat(e, FDOPEN_ERROR);
		strcat(e, err);
		Tcl_AddErrorInfo(interp, e);
		free(e);
		return TCL_ERROR;
	}
	r = Tcl_NewIntObj(intr);
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

static int teddy_fdclose_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_Obj *r;
	int intr;

	ARGNUM((argc != 2), "fdclose");

	intr = close(atoi(argv[1]));
	r = Tcl_NewIntObj(intr);
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing fdclose");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

static int teddy_fddup2_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_Obj *r;
	int intr;

	ARGNUM((argc != 3), "fddup2");

	intr = dup2(atoi(argv[1]), atoi(argv[2]));
	r = Tcl_NewIntObj(intr);
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing fddup2");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

static int teddy_fdpipe_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int pipefd[2];
	int ret;
	Tcl_Obj *pipeobj[2], *retlist;

	ARGNUM((argc != 1), "fdpipe");

	ret = pipe(pipefd);

	if (ret < 0) {
		Tcl_AddErrorInfo(interp, "Error executing fdpipe command");
		return TCL_ERROR;
	}

	pipeobj[0] = Tcl_NewIntObj(pipefd[0]);
	Tcl_IncrRefCount(pipeobj[0]);
	pipeobj[1] = Tcl_NewIntObj(pipefd[1]);
	Tcl_IncrRefCount(pipeobj[1]);

	retlist = Tcl_NewListObj(2, pipeobj);
	Tcl_DecrRefCount(pipeobj[0]);
	Tcl_DecrRefCount(pipeobj[1]);

	Tcl_SetObjResult(interp, retlist);

	return TCL_OK;
}

static int teddy_posixfork_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 1), "posixfork");

	int intr = fork();
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing posixfork command");
		return TCL_ERROR;
	}

	Tcl_Obj *r = Tcl_NewIntObj(intr);
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

static int teddy_posixexec_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	// first argument passed to this command is the name of this command (i.e. the string "posixexec") this needs to be discarded before calling execvp and a NULL needs to be appended to argv as a terminator

	char ** newargv = malloc(sizeof(char *) * argc);

	for (int i = 1; i < argc; ++i) {
		newargv[i-1] = (char *)argv[i];
	}
	newargv[argc-1] = NULL;

	if (execvp(newargv[0], newargv) == -1) {
		perror("posixexec error");
		Tcl_AddErrorInfo(interp, "Error executing posixexec command");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int teddy_posixwaitpid_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int i;
	int status, options = 0;
	pid_t r;
	pid_t pid;
	Tcl_Obj *retval[2], *retlist;

	ARGNUM((argc < 2), "posixwaitpid");

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "-wexited") == 0) {
			options |= WEXITED;
		} else if (strcmp(argv[i], "-wstopped") == 0) {
			options |= WSTOPPED;
		} else if (strcmp(argv[i], "-wcontinued") == 0) {
			options |= WCONTINUED;
		} else if (strcmp(argv[i], "-wnohang") == 0) {
			options |= WNOHANG;
		} else if (strcmp(argv[i], "-wnowait") == 0) {
			options |= WNOWAIT;
		} else {
			Tcl_AddErrorInfo(interp, "Unknown option passed to 'posixwaitpid' command");
			return TCL_ERROR;
		}
	}

	if (i != argc - 1) {
		Tcl_AddErrorInfo(interp, "Extraneous arguments given to 'posixwaitpid' command");
		return TCL_ERROR;
	}

	pid = atoi(argv[i]);

	r = waitpid(pid, &status, options);

	if (r == -1) {
		Tcl_AddErrorInfo(interp, "Error executing waitpid command");
		return TCL_ERROR;
	}

	//printf("WAITPID output: %d %d\n", r, status);

	retval[0] = Tcl_NewIntObj(r);
	Tcl_IncrRefCount(retval[0]);
	retval[1] = Tcl_NewIntObj(WEXITSTATUS(status));
	Tcl_IncrRefCount(retval[1]);

	retlist = Tcl_NewListObj(2, retval);
	Tcl_SetObjResult(interp, retlist);
	Tcl_DecrRefCount(retval[0]);
	Tcl_DecrRefCount(retval[1]);

	return TCL_OK;
}

static int teddy_posixexit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 2), "posixexit");
	exit(atoi(argv[1]));
	return TCL_OK;
}

static int teddy_fd2channel_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 3), "fd2channel");

	int fd = atoi(argv[1]);
	const char *type = argv[2];

	if (fd < 0) {
		Tcl_AddErrorInfo(interp, "Wrong argument to fd2channel (not a file descriptor)");
		return TCL_ERROR;
	}

	int readOrWrite;

	if (strcmp(type, "read") == 0) {
		readOrWrite = TCL_READABLE;
	} else if (strcmp(type, "write") == 0) {
		readOrWrite = TCL_WRITABLE;
	} else {
		Tcl_AddErrorInfo(interp, "Wrong argument to fd2channel (not read or write)");
	}

	Tcl_Channel chan = Tcl_MakeFileChannel(fd, readOrWrite);
	Tcl_RegisterChannel(interp, chan);
	Tcl_SetResult(interp, Tcl_GetChannelName(chan), TCL_VOLATILE);

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

	Tcl_CreateCommand(interp, "kill", &teddy_kill_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "cd", &teddy_cd_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "in", &teddy_in_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "setcfg", &teddy_setcfg_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "bindkey", &teddy_bindkey_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "pwf", &teddy_pwf_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "iopen", &teddy_iopen_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "cb", &teddy_cb_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "undo", &teddy_undo_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "search", &teddy_search_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy::bg", &teddy_bg_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy_intl::inpath", &teddy_inpath_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "rgbcolor", &teddy_rgbcolor_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy::history", &teddy_history_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy::session", &teddy_session_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "s", &teddy_research_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "c", &teddy_change_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "m", &teddy_move_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "lexydef-create", &lexy_create_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexydef-append", &lexy_append_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexyassoc", &lexy_assoc_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexy_dump", &lexy_dump_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexy-token", &lexy_token_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "buffer", &teddy_buffer_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "fdopen", &teddy_fdopen_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fdclose", &teddy_fdclose_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fddup2", &teddy_fddup2_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fdpipe", &teddy_fdpipe_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixfork", &teddy_posixfork_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixexec", &teddy_posixexec_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixwaitpid", &teddy_posixwaitpid_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixexit", &teddy_posixexit_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "fd2channel", &teddy_fd2channel_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy::tags", &teddy_tags_command, (ClientData)NULL, NULL);

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

	// Without this tcl screws up the newline character on its own output, it tries to output cr + lf and the terminal converts lf again, resulting in an output of cr + cr + lf, other c programs seem to behave correctly
	Tcl_Eval(interp, "fconfigure stdin -translation binary; fconfigure stdout -translation binary; fconfigure stderr -translation binary");
}

void interp_free(void) {
	Tcl_DeleteInterp(interp);
}

static int interp_eval_ex(const char *command, bool show_ret) {
	int code;

	code = Tcl_Eval(interp, command);

	switch (code) {
	case TCL_OK:
		if (show_ret) {
			const char *result = Tcl_GetStringResult(interp);
			if (strcmp(result, "") != 0) {
				if (interp_context_editor() != NULL) {
					quick_message("TCL Result", result);
				} else {
					fprintf(stderr, "%s", result);
					exit(0);
				}
			}
		}
		Tcl_ResetResult(interp);
		break;

	case TCL_ERROR: {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);
		quick_message("TCL Error", Tcl_GetString(stackTrace));
		Tcl_ResetResult(interp);
		break;
	}

	case TCL_BREAK:
	case TCL_CONTINUE:
	case TCL_RETURN:
		break;
	}

	return code;
}

int interp_eval(editor_t *editor, buffer_t *buffer, const char *command, bool show_ret) {
	editor_t *prev_editor = interp_context_editor();
	buffer_t *prev_buffer = interp_context_buffer();

	//printf("Starting eval: %p %p\n", prev_editor, prev_buffer);

	interp_context_buffer_set(buffer);
	if (editor != NULL) interp_context_editor_set(editor);

	int code = interp_eval_ex(command, show_ret);

	if (prev_editor != NULL) interp_context_editor_set(prev_editor);
	else interp_context_buffer_set(prev_buffer);

	//printf("Ending eval: %p %p\n", interp_context_editor(), interp_context_buffer());

	return code;
}

void read_conf(void) {
	char *config_dir;

	char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home != NULL) {
		asprintf(&config_dir, "%s/teddy", xdg_config_home);
	} else {
		asprintf(&config_dir, "%s/.config/teddy", getenv("HOME"));
	}
	alloc_assert(config_dir);

	int mkdir_r = mkdir(config_dir, 0770);
	if ((mkdir_r < 0) && (errno != EEXIST)) {
		perror("Couldn't create configuration directory");
		exit(EXIT_FAILURE);
	}


	char *name;
	asprintf(&name, "%s/rc", config_dir);

	FILE *f = fopen(name, "r");
	if (f) {
		fclose(f);
	} else {
		f = fopen(name, "w");
		if (f) {
			fprintf(f, "%s\n", AUTOCONF_TEDDY);
			fclose(f);
		}
	}

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

	free(xdg_config_home);
	free(name);
}

static const char *interp_eval_command_ex(int count, const char *argv[]) {
	char *cmd = Tcl_Merge(count, argv);
	int code = Tcl_Eval(interp, cmd);
	Tcl_Free(cmd);

	if (code == TCL_OK) {
		return Tcl_GetStringResult(interp);
	} else {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);

		fprintf(stderr, "TCL Exception: %s\n", Tcl_GetString(stackTrace));

		return NULL;
	}
}

const char *interp_eval_command(editor_t *editor, buffer_t *buffer, int count, const char *argv[]) {
	editor_t *prev_editor = interp_context_editor();
	buffer_t *prev_buffer = interp_context_buffer();

	//printf("Starting eval command: %p %p\n", prev_editor, prev_buffer);

	interp_context_buffer_set(buffer);
	if (editor != NULL) interp_context_editor_set(editor);

	const char *r = interp_eval_command_ex(count, argv);

	if (prev_editor != NULL) interp_context_editor_set(prev_editor);
	else interp_context_buffer_set(prev_buffer);

	//printf("Ending eval command: %p %p\n", interp_context_editor(), interp_context_buffer());

	return r;
}

editor_t *interp_context_editor(void) {
	return the_context_editor;
}

buffer_t *interp_context_buffer(void) {
	return the_context_buffer;
}

void interp_return_point_pair(buffer_t *buffer, int mark, int cursor) {
	char *r;
	if (mark < 0) {
		asprintf(&r, "nil %d:%d",
			1, 1);
	} else {
		asprintf(&r, "%d:%d %d:%d",
			1, 1,
			1, 1);
	}
	alloc_assert(r);
	Tcl_SetResult(interp, r, TCL_VOLATILE);
	free(r);
}

void interp_frame_debug() {
	const char *info_frame[] = { "info", "frame" };
	int n = atoi(interp_eval_command_ex(2, info_frame));

	printf("Stack trace:\n");
	for (int i = 0; i < n; ++i) {
		char *msg;
		asprintf(&msg, "%d", i);
		alloc_assert(msg);
		const char *info_frame2[] = { "info", "frame", msg };
		const char *s = interp_eval_command_ex(3, info_frame2);
		free(msg);
		printf("\t%d\t%s\n", i, s);
	}
}

bool interp_toplevel_frame() {
	const char *info_frame[] = { "info", "frame" };
	int n = atoi(interp_eval_command_ex(2, info_frame));

	return n <= 2;
}
