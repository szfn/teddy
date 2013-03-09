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
#include "docs.h"

Tcl_Interp *interp;
editor_t *the_context_editor = NULL;
buffer_t *the_context_buffer = NULL;
bool change_directory_back_after_eval;

static void interp_context_editor_set(editor_t *editor) {
	the_context_editor = editor;
	the_context_buffer = (editor != NULL) ? editor->buffer : NULL;
}

void interp_context_buffer_set(buffer_t *buffer) {
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

static int teddy_stickdir_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	change_directory_back_after_eval = false;
	return TCL_OK;
}

static int teddy_in_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc < 3), "in");

	char *wd = get_current_dir_name();

	if (strcmp(argv[1], "!") == 0) {
		HASBUF("in");
		const char *path = interp_context_buffer()->path;
		if (path[strlen(path)-1] == '/') {
			chdir(path);
		} else {
			char *last_slash = strrchr(path, '/');
			if (last_slash == NULL) {
				Tcl_AddErrorInfo(interp, "Internal error (in command, path)");
				return TCL_ERROR;
			}
			char *r = strndup(path, last_slash-path);
			chdir(r);
			free(r);
		}
	} else {
		chdir(argv[1]);
	}

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
	iopen((argc < 2) ? NULL : argv[1]);
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

	top_recoloring();

	if (columnset != NULL) {
		GList *column_list = gtk_container_get_children(GTK_CONTAINER(columnset));
		for (GList *column_cur = column_list; column_cur != NULL; column_cur = column_cur->next) {
			GList *frame_list = gtk_container_get_children(GTK_CONTAINER(column_cur->data));
			for (GList *frame_cur = frame_list; frame_cur != NULL; frame_cur = frame_cur->next) {
				tframe_t *frame = GTK_TFRAME(frame_cur->data);
				tframe_recoloring(frame);
			}
		}

		gtk_widget_queue_draw(GTK_WIDGET(columnset));

		iopen_recoloring();

		gtk_widget_like_editor(&global_config, the_word_completer.tree);
		gtk_widget_like_editor(&global_config, search_history.c.tree);
		gtk_widget_like_editor(&global_config, command_history.c.tree);
		gtk_widget_like_editor(&global_config, input_history.c.tree);
	}

	return TCL_OK;
}

static int teddy_bindkey_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 3), "bindkey")

	char *key = strdup(argv[1]);

	if (argv[1][0] != 'M') {
		char *base = strrchr(argv[1], '-');
		if (base != NULL) {
			key[base - argv[1]] = '\0';
			++base;

			bool super = false, ctrl = false, alt = false, shift = false;
			char *tok, *s;
			for (tok = strtok_r(key, "-", &s); tok != NULL; tok = strtok_r(NULL, "-", &s)) {
				if (strcasecmp(tok, "super") == 0) {
					super = true;
				} else if (strcasecmp(tok, "ctrl") == 0) {
					ctrl = true;
				} else if (strcasecmp(tok, "alt") == 0) {
					alt = true;
				} else if (strcasecmp(tok, "shift") == 0) {
					shift = true;
				}
			}

			key[0] = '\0';
			if (super) strcat(key, "Super-");
			if (ctrl) strcat(key, "Ctrl-");
			if (alt) strcat(key, "Alt-");
			if (shift) strcat(key, "Shift-");
			strcat(key, base);
		}
	}

	char *value = strdup(argv[2]);

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

			char *text = NULL;

			if (strcmp(argv[2], "before") == 0) {
				text = u->before_selection.text;
			} else if (strcmp(argv[2], "after") == 0) {
				text = u->after_selection.text;
			} else {
				Tcl_AddErrorInfo(interp, "Wrong argument to 'undo get', expected before or after");
				return TCL_ERROR;
			}

			Tcl_SetResult(interp, (text != NULL) ? text : "", TCL_VOLATILE);
			return TCL_OK;
		} else if (strcmp(argv[1], "region") == 0) {
			undo_node_t *u = undo_peek(&(interp_context_buffer()->undo));
			int mark = -1, cursor = interp_context_buffer()->cursor;

			if (u != NULL) {
				if (strcmp(argv[2], "before") == 0) {
					mark = u->before_selection.start;
					cursor = u->before_selection.end;
				} else if (strcmp(argv[2], "after") == 0) {
					mark = u->after_selection.start;
					cursor = u->after_selection.end;
				} else {
					Tcl_AddErrorInfo(interp, "Wrong argument to 'undo get', expected before or after");
					return TCL_ERROR;
				}
			}

			interp_return_point_pair(interp_context_buffer(), mark, cursor);
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

static char *concatarg(int argstart, int argc, const char *argv[]) {

	int arglen = 1;

	for (int i = argstart; i < argc; ++i) {
		arglen += strlen(argv[i]) + 1;
	}

	char *argument = malloc(sizeof(char) * arglen);
	argument[0] = '\0';

	for (int i = argstart; i < argc; ++i) {
		strcat(argument, argv[i]);
		strcat(argument, " ");
	}

	return argument;
}

static void shexec(const char *argument) {
	setenv("TERM", "ansi", 1);
	setenv("PAGER", "", 1);
	setenv("EDITOR", "teddy", 1);
	setenv("VISUAL", "teddy", 1);
	const char *sh = getenv("SHELL");
	if (sh == NULL) sh = "/bin/sh";
	execl(sh, sh, "-c", argument, (char *)NULL);
	perror("Error executing inferiror");
	exit(EXIT_FAILURE);
}

static int teddy_shell_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Not enough arguments passed to 'shell'");
		return TCL_ERROR;
	}

	int argstart = 1;

	buffer_t *buffer = buffer_id_to_buffer(argv[argstart]);
	if (buffer != NULL) {
		argstart++;
	} else {
		buffer = buffers_get_buffer_for_process(false);
	}

	if (argstart >= argc) {
		Tcl_AddErrorInfo(interp, "Not enough arguments passed to 'shell'");
		return TCL_ERROR;
	}

	char *argument = concatarg(argstart, argc, argv);

	struct termios term;
	bzero(&term, sizeof(struct termios));

	term.c_iflag = ICRNL | IUTF8;
	term.c_oflag = ONLRET;
	term.c_cflag = B38400 | CS8 | CREAD;
	term.c_lflag = ICANON;

	int masterfd;
	pid_t child = forkpty(&masterfd, NULL, &term, NULL);
	if (child == -1) {
		Tcl_AddErrorInfo(interp, "Fork failed");
		return TCL_ERROR;
	} else if (child != 0) {
		/* parent code */

		if (!jobs_register(child, masterfd, buffer, argument)) {
			Tcl_AddErrorInfo(interp, "Registering job failed, probably exceeded the maximum number of jobs available");
			return TCL_ERROR;
		}

		free(argument);
		return TCL_OK;
	} else {
		/* child code */
		shexec(argument);
		return TCL_ERROR; // <- this is never executed shexec never returns
	}
}

static int closedup(int apipe[2], int jc, int dupfrom, int dupto) {
	if (close(apipe[jc]) < 0) return -1;
	if (dup2(apipe[dupfrom], dupto) < 0) return -1;
	if (close(apipe[dupfrom]) < 0) return -1;
	return 0;
}

struct shellsync_writer_args {
	const char *instr;
	int fd;
};

static void *shellsync_writer(void *args) {
	struct shellsync_writer_args *swa = (struct shellsync_writer_args *)args;

	const char *p = swa->instr;

	while (strlen(p) > 0) {
		ssize_t n = write(swa->fd, p, sizeof(char)*strlen(p));
		if (n < 0) break;
		p += n;
	}

	close(swa->fd);

	return NULL;
}

struct shellsync_reader_args {
	char *outstr;
	int fd;
};

void *shellsync_reader(void *args) {
	struct shellsync_reader_args *sra = (struct shellsync_reader_args *)args;

	int allocated = 10;
	char *p = sra->outstr = malloc(sizeof(char) * allocated);
	int m = allocated;

	for(;;) {
		if (m <= 0) {
			m = allocated;
			allocated += m;
			sra->outstr = realloc(sra->outstr, sizeof(char) * allocated);
			p = sra->outstr + m;
		}

		ssize_t n = read(sra->fd, p, m);
		if (n <= 0) break; // error or end of file (we don't care, it's the same to us)

		m -= n;
		p += n;
	}

	*p = 0; // terminates string

	close(sra->fd);

	return NULL;
}


static int teddy_shellsync_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
#define SSERR(f, lbl) { if ((f) < 0) goto lbl; }

	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Not enough arguments to shellsync");
		return TCL_ERROR;
	}

	const char *instr = argv[1];
	char *argument = concatarg(2, argc, argv);

	int inpipe[2], outpipe[2], errpipe[2];
	pid_t child_pid = 1;
	bool killchild = false;

	SSERR(pipe(inpipe), shellsync_fail);
	SSERR(pipe(outpipe), shellsync_fail);
	SSERR(pipe(errpipe), shellsync_fail);

	child_pid = fork();
	SSERR(child_pid, shellsync_fail);

	if (child_pid == 0) {
		/* child code */
		// redirect stdin, stdout and stderr to the pipes (child side)
		SSERR(closedup(inpipe, 1, 0, 0), shellsync_fail);
		SSERR(closedup(outpipe, 0, 1, 1), shellsync_fail);
		SSERR(closedup(errpipe, 0, 1, 2), shellsync_fail);

		shexec(argument);
	} else {
		/* parent code */

		killchild = true;

		SSERR(close(inpipe[0]), shellsync_fail);
		SSERR(close(outpipe[1]), shellsync_fail);
		SSERR(close(errpipe[1]), shellsync_fail);
		free(argument); // child uses it, it got it's own copy

		struct shellsync_writer_args shellsync_writer_args;
		shellsync_writer_args.instr = instr;
		shellsync_writer_args.fd = inpipe[1];

		struct shellsync_reader_args shellsync_inreader_args, shellsync_errreader_args;
		shellsync_inreader_args.fd = outpipe[0];
		shellsync_inreader_args.outstr = NULL;
		shellsync_errreader_args.fd = errpipe[0];
		shellsync_errreader_args.outstr = NULL;

		pthread_t outthread, inthread, inerrthread;
		SSERR(pthread_create(&outthread, NULL, shellsync_writer, &shellsync_writer_args), shellsync_fail);
		pthread_create(&inthread, NULL, shellsync_reader, &shellsync_inreader_args);
		pthread_create(&inerrthread, NULL, shellsync_reader, &shellsync_errreader_args);

		pthread_join(outthread, NULL);
		pthread_join(inthread, NULL);
		pthread_join(inerrthread, NULL);

		int status;
		SSERR(waitpid(child_pid, &status, 0), shellsync_fail);

		int r = WEXITSTATUS(status);
		if (r == 0) {
			Tcl_SetResult(interp, shellsync_inreader_args.outstr, TCL_VOLATILE);
			free(shellsync_inreader_args.outstr);
			free(shellsync_errreader_args.outstr);
			return TCL_OK;
		} else {
			Tcl_AddErrorInfo(interp, shellsync_errreader_args.outstr);
			free(shellsync_inreader_args.outstr);
			free(shellsync_errreader_args.outstr);
			return TCL_ERROR;
		}
	}

shellsync_fail: ;
#define ERRBUFLEN 128
	char errbuf[ERRBUFLEN];
	const char *perrbuf;
	if (strerror_r(errno, errbuf, ERRBUFLEN) == 0) {
		perrbuf = errbuf;
	} else {
		perrbuf = "Error on shellsync";

	}
	if (child_pid == 0) {
		fputs(perrbuf, stderr);
		exit(EXIT_FAILURE);
	} else {
		Tcl_AddErrorInfo(interp, perrbuf);

		if (killchild) {
			kill(child_pid, SIGKILL);
		}

		return TCL_ERROR;
	}
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

static bool move_command_ex(const char *sin, int *p, int ref, enum movement_type_t default_glyph_motion, bool set_jump) {
	if (strcmp(sin, "nil") == 0) {
		if (ref < 0) {
			Tcl_AddErrorInfo(interp, "Attempted to null cursor in 'm' command");
			return false;
		} else {
			*p = -1;
			return true;
		}
	}

	if (sin[0] == '=') {
		// exact positioning
		int arg = atoi(sin+1);

		if (arg == -1) {
			*p = -1;
		} else {
			*p = 0;
			bool r = buffer_move_point_glyph(interp_context_buffer(), p, MT_ABS, arg+1);
			Tcl_SetResult(interp, r ? "true" : "false", TCL_VOLATILE);
		}

		return true;
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

	if ((lineflag != MT_REL) && set_jump) {
		buffer_record_jump(interp_context_buffer());
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

static int teddy_move_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
#define MOVE_MARK(argument, d) { \
	if (!move_command_ex(argument, &(interp_context_buffer()->mark), interp_context_buffer()->cursor, d, false)) { \
		return TCL_ERROR; \
	} \
}

#define MOVE_CURSOR(argument, d, set_jump) {\
	const char *arg = argument;\
	if (argument[0] == 'm') {\
		++arg;\
		if (interp_context_buffer()->mark >= 0) interp_context_buffer()->cursor = interp_context_buffer()->mark;\
	}\
	if (!move_command_ex(arg, &(interp_context_buffer()->cursor), -1, d, set_jump)) { \
		return TCL_ERROR; \
	} \
}

#define MOVE_MARK_CURSOR(mark_argument, cursor_argument, set_jump) {\
	MOVE_MARK(mark_argument, MT_START);\
	MOVE_CURSOR(cursor_argument, MT_END, set_jump);\
	interp_context_buffer()->savedmark = interp_context_buffer()->mark;\
}

	HASBUF("move");

	switch (argc) {
	case 1:
		interp_return_point_pair(interp_context_buffer(), interp_context_buffer()->mark, interp_context_buffer()->cursor);
		return TCL_OK;
	case 2:
		if (strcmp(argv[1], "all") == 0) {
			MOVE_MARK_CURSOR("1:1", "$:$", false);
		} else if (strcmp(argv[1], "sort") == 0) {
			sort_mark_cursor(interp_context_buffer());
		} else if (strcmp(argv[1], "line") == 0) {
			sort_mark_cursor(interp_context_buffer());
			MOVE_MARK_CURSOR("+0:1", "+0:$", false);
		} else {
			// not a shortcut, actually movement command to execute
			if (strchr(argv[1], ' ') != NULL) {
				char *a = strdup(argv[1]);
				alloc_assert(a);

				char *saveptr;
				char *one = strtok_r(a, " ", &saveptr);
				char *two = strtok_r(NULL, " ", &saveptr);

				MOVE_MARK_CURSOR(one, two, true);

				free(a);
			} else {
				MOVE_MARK("nil", MT_START);
				MOVE_CURSOR(argv[1], MT_START, true);
			}
		}
		break;
	case 3:
		MOVE_MARK_CURSOR(argv[1], argv[2], true);
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
	if (!strcmp(sigspec, "USR1")) return SIGUSR1;
	if (!strcmp(sigspec, "USR2")) return SIGUSR2;

	return -1;
}

static int teddy_kill_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc > 3), "kill");

	bool pid_specified = false;
	pid_t pid;
	int signum = SIGINT;

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
		// by default we send the signal to the controlling group of the job's terminal
		// it may not be the same as the process we spawned
		// NOTE: I'm not sure why the call to tcgetpgrp works, it shouldn't according to the man page but it seems deliberate in the kernel code (https://github.com/mirrors/linux/blob/637704cbc95c02d18741b4a6e7a5d2397f8b28ce/drivers/tty/tty_io.c)
		pid = tcgetpgrp(interp_context_buffer()->job->masterfd);
		if (pid < 0) {
			pid = interp_context_buffer()->job->child_pid;
		}
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

static int teddy_rehash_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc != 1), "teddy::rehash");
	cmdcompl_init(true);
	return TCL_OK;
}

static int teddy_fullscreen_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (columnset != NULL) {
		if (at_fullscreen) {
			gtk_window_unfullscreen(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(columnset))));
		} else {
			gtk_window_fullscreen(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(columnset))));
		}
	} else {
		fullscreen_on_startup = true;
	}
	return TCL_OK;
}

static bool write_doc_file(Tcl_Interp *interp, const char *filename, const unsigned char *text, size_t size) {
	if (access(filename, F_OK) == 0) return true;

	FILE *f = fopen(filename, "wb");
	size_t r = fwrite(text, sizeof(unsigned char), size, f);
	if (r != size) {
		if (interp != NULL) {
			char *msg;
			asprintf(&msg, "Could not write documentation file %s\n", filename);
			alloc_assert(msg);
			Tcl_AddErrorInfo(interp, msg);
			free(msg);
		}
		return false;
	}
	fclose(f);
	return true;
}

static bool write_doc_files(void) {
	int r = mkdir("/dev/shm/teddy/", 0777);
	if (r != 0) {
		if (errno != EEXIST) {
			Tcl_AddErrorInfo(interp, "Could not create /dev/shm/teddy directory");
			return false;
		}
	}

	if (!write_doc_file(interp, "/dev/shm/teddy/index.html", index_doc, index_doc_size)) {
		return false;
	}
	if (!write_doc_file(interp, "/dev/shm/teddy/commands.html", commands_doc, commands_doc_size)) {
		return false;
	}
	if (!write_doc_file(interp, "/dev/shm/teddy/keyboard.html", keyboard_doc, keyboard_doc_size)) {
		return false;
	}
	if (!write_doc_file(interp, "/dev/shm/teddy/mouse.html", mouse_doc, mouse_doc_size)) {
		return false;
	}
	if (!write_doc_file(interp, "/dev/shm/teddy/teddy_frame.png", teddy_frame_png, teddy_frame_png_size)) {
		return false;
	}
	if (!write_doc_file(interp, "/dev/shm/teddy/teddy_link.png", teddy_link_png, teddy_link_png_size)) {
		return false;
	}
	if (!write_doc_file(interp, "/dev/shm/teddy/teddy_window.png", teddy_window_png, teddy_window_png_size)) {
		return false;
	}

	return true;
}

static int teddy_help_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc == 1) {
		if (!write_doc_files()) {
			return TCL_ERROR;
		}
		interp_eval(NULL, NULL, "$teddy::open_cmd /dev/shm/teddy/index.html", false, true);
	} else {
		Tcl_AddErrorInfo(interp, "Too many arguments to 'help'");
		return TCL_ERROR;
	}
	return TCL_OK;
}

static int teddy_cmdlinefocus_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	top_start_command_line(interp_context_editor(), NULL);
	return TCL_OK;
}

static int teddy_wandercount_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'teddy_intl::wandercount");
		return TCL_ERROR;
	}

	int wca = atoi(argv[1]);

	if (interp_context_buffer() != NULL) {
		interp_context_buffer()->wandercount += wca;
	}
	return TCL_OK;
}

void interp_init(void) {
	interp = Tcl_CreateInterp();
	if (interp == NULL) {
		fprintf(stderr, "Couldn't create TCL interpreter\n");
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
	Tcl_CreateCommand(interp, "teddy::stickdir", &teddy_stickdir_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "in", &teddy_in_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "setcfg", &teddy_setcfg_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "bindkey", &teddy_bindkey_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "pwf", &teddy_pwf_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "iopen", &teddy_iopen_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "cb", &teddy_cb_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "undo", &teddy_undo_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "search", &teddy_search_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy_intl::inpath", &teddy_inpath_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy_intl::wandercount", &teddy_wandercount_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "rgbcolor", &teddy_rgbcolor_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy::history", &teddy_history_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy::session", &teddy_session_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy::rehash", &teddy_rehash_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy::newline", &teddy_newline_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "s", &teddy_research_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "c", &teddy_change_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "m", &teddy_move_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "lexydef-create", &lexy_create_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexydef-append", &lexy_append_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexyassoc", &lexy_assoc_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexy_dump", &lexy_dump_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "lexy-token", &lexy_token_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "buffer", &teddy_buffer_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy::tags", &teddy_tags_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "teddy::fullscreen", &teddy_fullscreen_command, (ClientData)NULL, NULL);

	Tcl_CreateCommand(interp, "help", &teddy_help_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "teddy::cmdline-focus", &teddy_cmdlinefocus_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "shell", &teddy_shell_command, (ClientData)NULL, NULL);
	Tcl_CreateCommand(interp, "shellsync", &teddy_shellsync_command, (ClientData)NULL, NULL);

	int code = Tcl_Eval(interp, BUILTIN_TCL_CODE);
	if (code != TCL_OK) {
		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);

		fprintf(stderr, "Internal TCL Error: %s\n", Tcl_GetString(stackTrace));
		exit(EXIT_FAILURE);
	}

	// Without this tcl screws up the newline character on its own output, it tries to output cr + lf and the terminal converts lf again, resulting in an output of cr + cr + lf, other c programs seem to behave correctly
	Tcl_Eval(interp, "fconfigure stdin -translation binary; fconfigure stdout -translation binary; fconfigure stderr -translation binary");
}

void interp_free(void) {
	Tcl_DeleteInterp(interp);
}

static int interp_eval_ex(const char *command, bool show_ret, bool reset_result) {
	int code = Tcl_Eval(interp, command);

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
		if (reset_result) Tcl_ResetResult(interp);
		break;

	case TCL_ERROR: {
		if (reset_result) {
			Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
			Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
			Tcl_Obj *stackTrace;
			Tcl_IncrRefCount(key);
			Tcl_DictObjGet(NULL, options, key, &stackTrace);
			Tcl_DecrRefCount(key);
			quick_message("TCL Error", Tcl_GetString(stackTrace));
			Tcl_ResetResult(interp);
		}
		break;
	}

	case TCL_BREAK:
	case TCL_CONTINUE:
	case TCL_RETURN:
		break;
	}

	return code;
}

int interp_eval(editor_t *editor, buffer_t *buffer, const char *command, bool show_ret, bool reset_result) {
	editor_t *prev_editor = interp_context_editor();
	buffer_t *prev_buffer = interp_context_buffer();

	interp_context_buffer_set(buffer);
	if (editor != NULL) interp_context_editor_set(editor);

	int code = interp_eval_ex(command, show_ret, reset_result);

	if (prev_editor != NULL) interp_context_editor_set(prev_editor);
	else interp_context_buffer_set(prev_buffer);

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

		fprintf(stderr, "TCL Error: %s\n", Tcl_GetString(stackTrace));
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

	interp_context_buffer_set(buffer);
	if (editor != NULL) interp_context_editor_set(editor);

	const char *r = interp_eval_command_ex(count, argv);

	if (prev_editor != NULL) interp_context_editor_set(prev_editor);
	else interp_context_buffer_set(prev_buffer);

	return r;
}

editor_t *interp_context_editor(void) {
	return the_context_editor;
}

buffer_t *interp_context_buffer(void) {
	return the_context_buffer;
}

void interp_return_single_point(buffer_t *buffer, int p) {
	char *r;
	asprintf(&r, "%d:%d", buffer_line_of(buffer, p), buffer_column_of(buffer, p));
	alloc_assert(r);
	Tcl_SetResult(interp, r, TCL_VOLATILE);
	free(r);
}

void interp_return_point_pair(buffer_t *buffer, int mark, int cursor) {
	char *r;
	if (mark < 0) {
		asprintf(&r, "nil %d:%d",
			buffer_line_of(buffer, cursor), buffer_column_of(buffer, cursor));
	} else {
		asprintf(&r, "%d:%d %d:%d",
			buffer_line_of(buffer, mark), buffer_column_of(buffer, mark),
			buffer_line_of(buffer, cursor), buffer_column_of(buffer, cursor));
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
