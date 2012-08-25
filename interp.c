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
#include "lexy.h"
#include "autoconf.h"
#include "top.h"
#include "iopen.h"

Tcl_Interp *interp;
editor_t *the_context_editor = NULL;
buffer_t *the_context_buffer = NULL;

static int teddy_cd_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'cd' command");
		return TCL_ERROR;
	}

	top_cd(argv[1]);

	return TCL_OK;
}

static int teddy_in_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'in' command");
		return TCL_ERROR;
	}

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
	if (interp_context_buffer() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'pwf' command");
		return TCL_ERROR;
	}

	Tcl_SetResult(interp, interp_context_buffer()->path, TCL_VOLATILE);
	return TCL_OK;
}

static int teddy_iopen_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	iopen();
	return TCL_OK;
}

static int teddy_setcfg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to setcfg");
		return TCL_ERROR;
	}

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

	if (interp_context_buffer() != NULL) buffer_config_changed(interp_context_buffer());

	if (columnset != NULL)
		gtk_widget_queue_draw(GTK_WIDGET(columnset));

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

static int teddy_save_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'save' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'save' command");
		return TCL_ERROR;
	}

	editor_save_action(interp_context_editor());

	return TCL_OK;
}

static int teddy_nexteditor_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'bufman' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'next-editor' command");
		return TCL_ERROR;
	}

	tframe_t *context_frame;
	find_editor_for_buffer(interp_context_buffer(), NULL, &context_frame, NULL);
	if (context_frame != NULL) {
		tframe_t *next_frame;
		columns_find_frame(columnset, context_frame, NULL, NULL, NULL, NULL, &next_frame);
		if (next_frame != NULL) gtk_widget_grab_focus(GTK_WIDGET(next_frame));
	}
	return TCL_OK;
}

static int teddy_undo_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'undo' command");
		return TCL_ERROR;
	}

	if (argc == 1) {
		editor_undo_action(interp_context_editor());
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
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'search' command");
		return TCL_ERROR;
	}

	if (argc > 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'search' command");
		return TCL_ERROR;
	}

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

static void configure_for_bg_execution(Tcl_Interp *interp) {
	interp_context_editor_set(NULL);

	setenv("TERM", "ansi", 1);
	setenv("PAGER", "", 1);
	setenv("SHELL", "teddy", 1);

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
	buffer_t *buffer;
	struct termios term;

	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'bg' command");
		return TCL_ERROR;
	}

	const char *codearg;
	if (argc == 2) {
		if (strcmp(argv[1], "-setup") == 0) {
			configure_for_bg_execution(interp);
			return TCL_OK;
		} else {
			buffer = buffers_get_buffer_for_process();
			codearg = argv[1];
		}
	} else if (argc == 3) {
		buffer = buffers_create_with_name(strdup(argv[1]));
		codearg = argv[2];
	} else {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bg' command");
		return TCL_ERROR;
	}

	go_to_buffer(interp_context_editor(), buffer, false);

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

	{
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

	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute '<' command");
		return TCL_ERROR;
	}

	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to '<' command");
		return TCL_ERROR;
	}

	job = interp_context_buffer()->job;

	if (job == NULL) {
		Tcl_AddErrorInfo(interp, "No job associated with this buffer, can not send input");
		return TCL_ERROR;
	}

	for (i = 1; i < argc; ++i) {
		if (write_all(job->masterfd, argv[i]) < 0) {
			Tcl_AddErrorInfo(interp, "Error sending input to process");
			return TCL_ERROR;
		}
		buffer_append(interp_context_buffer(), argv[i], strlen(argv[i]), 0);
		if (i != argc-1) {
			if (write_all(job->masterfd, " ") < 0) {
				Tcl_AddErrorInfo(interp, "Error sending input to process");
				return TCL_ERROR;
			}
			buffer_append(interp_context_buffer(), " ", strlen(" "), 0);
		}
	}
	if (write_all(job->masterfd, "\n") < 0) {
		Tcl_AddErrorInfo(interp, "Error sending input to process");
		return TCL_ERROR;
	}
	buffer_append(interp_context_buffer(), "\n", strlen("\n"), 0);

	return TCL_OK;
}

static int teddy_change_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'change' command");
		return TCL_ERROR;
	}

	switch (argc) {
	case 1:
		{
			lpoint_t start, end;
			buffer_get_selection(interp_context_buffer(), &start, &end);
			char *text = buffer_lines_to_text(interp_context_buffer(), &start, &end);
			Tcl_SetResult(interp, text, TCL_VOLATILE);
			free(text);
			return TCL_OK;
		}
	case 2:
		editor_replace_selection(interp_context_editor(), argv[1]);
		return TCL_OK;
	default:
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'selectlines'");
		return TCL_ERROR;
	}
}

static bool move_command_ex(const char *sin, lpoint_t *p, lpoint_t *ref) {
	if (strcmp(sin, "nil") == 0) {
		if (ref == NULL) {
			Tcl_AddErrorInfo(interp, "Attempted to null cursor in 'm' command");
			return false;
		} else {
			p->line = NULL;
			p->glyph = 0;
			return true;
		}
	}

	char *s = strdup(sin);
	alloc_assert(s);

	char *saveptr;
	char *first = strtok_r(s, ":", &saveptr);
	char *second = strtok_r(NULL, ":", &saveptr);
	char *expectfailure = strtok_r(NULL, ":", &saveptr);

	if ((first == NULL) || (second == NULL) || (expectfailure != NULL))
		goto move_command_ex_bad_argument;

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

	if (strcmp(second, "$") == 0) {
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
			colflag = MT_RELW;
			++second;
			break;
		case '-':
			colflag = MT_RELW;
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

	if (ref != NULL) {
		p->line = ref->line;
		p->glyph = ref->glyph;
	}

	if (p->line == NULL) {
		if (lineflag == MT_REL) goto move_command_relative_with_nil;
		if (colflag == MT_REL) goto move_command_relative_with_nil;
	}

	buffer_move_point_line(interp_context_buffer(), p, lineflag, lineno);
	buffer_move_point_glyph(interp_context_buffer(), p, colflag, colno);

	if (interp_context_editor() != NULL)
		editor_complete_move(interp_context_editor(), TRUE);

	free(s);

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

static int teddy_move_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'move' command");
		return TCL_ERROR;
	}

	switch (argc) {
	case 1:
		break;
	case 2:
		if (!move_command_ex(argv[1], &(interp_context_buffer()->cursor), NULL)) {
			return TCL_ERROR;
		}
		break;
	case 3:
		if (!move_command_ex(argv[1], &(interp_context_buffer()->mark), &(interp_context_buffer()->cursor))) {
			return TCL_ERROR;
		}
		if (!move_command_ex(argv[2], &(interp_context_buffer()->cursor), NULL)) {
			return TCL_ERROR;
		}

		copy_lpoint(&(interp_context_buffer()->savedmark), &(interp_context_buffer()->mark));
		
		break;
	default:
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'move' command");
		return TCL_ERROR;
	}

	interp_return_point_pair(&(interp_context_buffer()->mark), &(interp_context_buffer()->cursor));
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
	if (argc == 1) {
		if (interp_context_editor() == NULL) {
			/* automatic version of kill doesn't work unless there is an editor to automatically infer what to do from */
			Tcl_AddErrorInfo(interp, "Short version of 'kill' called without an active editor, provide an editor or more arguments");
			return TCL_ERROR;
		}

		if (interp_context_buffer()->job != NULL) {
			kill(interp_context_buffer()->job->child_pid, SIGTERM);
		} else {
			buffers_close(interp_context_buffer(), gtk_widget_get_toplevel(GTK_WIDGET(interp_context_editor())));
		}

		return TCL_OK;
	} else if (argc == 2) {
		if (strcmp(argv[1], "process") == 0) {
			// kill current process with SIGTERM (check that context_editor is defined and associated buffer has a job)
			if (interp_context_buffer() == NULL) {
				Tcl_AddErrorInfo(interp, "No active buffer");
				return TCL_ERROR;
			}
			if (interp_context_buffer()->job == NULL) {
				Tcl_AddErrorInfo(interp, "No active job");
				return TCL_ERROR;
			}
			kill(interp_context_buffer()->job->child_pid, SIGTERM);
		} else if (strcmp(argv[1], "buffer") == 0) {
			// close buffer (check that context_editor is defined)
			if (interp_context_buffer() == NULL) {
				Tcl_AddErrorInfo(interp, "No active buffer");
				return TCL_ERROR;
			}
			buffers_close(interp_context_buffer(), gtk_widget_get_toplevel(GTK_WIDGET(interp_context_editor())));
		} else if (argv[1][0] == '-') {
			// kill current process with specific signal (check that context_editor is defined and associated buffer has a job)
			if (interp_context_editor() == NULL) {
				Tcl_AddErrorInfo(interp, "No active editor");
				return TCL_ERROR;
			}
			if (interp_context_buffer()->job == NULL) {
				Tcl_AddErrorInfo(interp, "No active job");
				return TCL_ERROR;
			}
			int signum = parse_signum(argv[1]+1);
			if (signum < 0) {
				Tcl_AddErrorInfo(interp, "Can not parse signal specification");
				return TCL_ERROR;
			}
			kill(interp_context_buffer()->job->child_pid, signum);
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
		// kill specified process with specified signal or specific buffer

		if (strcmp(argv[1], "buffer") == 0) {
			buffer_t *buffer = buffer_id_to_buffer(argv[2]);
			if (buffer != NULL) {
				buffers_close(buffer, gtk_widget_get_toplevel(GTK_WIDGET(interp_context_editor())));
			} else {
				Tcl_AddErrorInfo(interp, "Couldn't find specified buffer");
				return TCL_ERROR;
			}
		} else {
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
		}
	} else {
		Tcl_AddErrorInfo(interp, "Too many arguments to 'kill', consult documentation");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int teddy_refresh_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "No editor open, can not execute 'refresh' command");
		return TCL_ERROR;
	}

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments supplied to refresh (accepted: none or a buffer)");
		return TCL_ERROR;
	}

	// do not refresh special buffers
	if (interp_context_buffer()->path[0] == '+') return TCL_OK;

	char *path = strdup(interp_context_buffer()->path);
	alloc_assert(path);

	if (null_buffer() == interp_context_buffer()) return TCL_OK;

	int r = buffers_close(interp_context_buffer(), gtk_widget_get_toplevel(GTK_WIDGET(interp_context_editor())));
	if (r == 0) return TCL_OK;

	enum go_file_failure_reason gffr;
	buffer_t *buffer = go_file(path, false, &gffr);
	if (buffer != NULL) editor_switch_buffer(interp_context_editor(), buffer);

	free(path);

	return TCL_OK;
}

static int teddy_load_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments supplied to 'load'");
		return TCL_ERROR;
	}

	return Tcl_EvalFile(interp, argv[1]);
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
	Tcl_CreateCommand(interp, "refresh", &teddy_refresh_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "cb", &teddy_cb_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "save", &teddy_save_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "next-editor", &teddy_nexteditor_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "undo", &teddy_undo_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "search", &teddy_search_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "bg", &teddy_bg_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "<", &teddy_sendinput_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "rgbcolor", &teddy_rgbcolor_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddyhistory", &teddy_history_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "s", &teddy_research_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "c", &teddy_change_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "m", &teddy_move_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "lexydef-create", &lexy_create_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexydef-append", &lexy_append_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexyassoc", &lexy_assoc_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexy_dump", &lexy_dump_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "buffer", &teddy_buffer_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "load", &teddy_load_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "fdopen", &teddy_fdopen_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fdclose", &teddy_fdclose_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fddup2", &teddy_fddup2_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "fdpipe", &teddy_fdpipe_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixfork", &teddy_posixfork_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixexec", &teddy_posixexec_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixwaitpid", &teddy_posixwaitpid_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "posixexit", &teddy_posixexit_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "fd2channel", &teddy_fd2channel_command, (ClientData)NULL, NULL);

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

int interp_eval(editor_t *editor, const char *command, bool show_ret) {
	int code;

	interp_context_editor_set(editor);

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

const char *interp_eval_command(int count, const char *argv[]) {
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

void interp_context_editor_set(editor_t *editor) {
	the_context_editor = editor;
	the_context_buffer = (editor != NULL) ? editor->buffer : NULL;
}

void interp_context_buffer_set(buffer_t *buffer) {
	the_context_editor = NULL;
	the_context_buffer = buffer;
}

editor_t *interp_context_editor(void) {
	return the_context_editor;
}

buffer_t *interp_context_buffer(void) {
	return the_context_buffer;
}

void interp_return_point_pair(lpoint_t *mark, lpoint_t *cursor) {
	char *r;
	if (mark->line == NULL) {
		asprintf(&r, "nil %d:%d",
			cursor->line->lineno+1, cursor->glyph+1);
	} else {
		asprintf(&r, "%d:%d %d:%d",
			mark->line->lineno+1, mark->glyph+1,
			cursor->line->lineno+1, cursor->glyph+1);
	}
	alloc_assert(r);
	Tcl_SetResult(interp, r, TCL_VOLATILE);
	free(r);
}
