#include "buffers.h"

#include "global.h"
#include "columns.h"
#include "editor.h"
#include "tags.h"
#include "top.h"
#include "interp.h"
#include "lexy.h"
#include "ipc.h"

#include "critbit.h"

#include <gdk/gdkkeysyms.h>
#include <glib-object.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/stat.h>

buffer_t **buffers;
int buffers_allocated;

int inotify_fd;
GIOChannel *inotify_channel;
guint inotify_channel_source_id;
int tags_wd = -1;

int process_buffers_counter = 0;

#define MAXIMUM_FILE_SIZE (32 * 1024 * 1024)

buffer_t *null_buffer(void) {
	return buffers[0];
}

static buffer_t *buffers_find_buffer_with_name(const char *name) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->path== NULL) continue;
		if (strcmp(buffers[i]->path, name) == 0) {
			return buffers[i];
		}
	}

	return NULL;
}

void buffer_to_buffer_id(buffer_t *buffer, char *bufferid) {
	strcpy(bufferid, "@b0");
	if (buffer == NULL) return;
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == buffer) {
			snprintf(bufferid+2, 15, "%d", i);
			return;
		}
	}
}

static buffer_t *buffers_make(const char *name) {
	buffer_t *buffer = buffers_find_buffer_with_name(name);
	if (buffer == NULL) {
		buffer = buffers_create_with_name(strdup(name));
		if (buffer != NULL) {
			tframe_t *frame;
			find_editor_for_buffer(interp_context_buffer(), NULL, &frame, NULL);
			heuristic_new_frame(columnset, frame, buffer);
		}
	}

	if (strncmp(name, "+bg/", 4) == 0) {
		int n = atoi(name+4);
		if (n >= process_buffers_counter) {
			process_buffers_counter = n+1;
		}
	}

	return buffer;
}

static void buffer_close_real(buffer_t *buffer, bool save_critbit) {
	//printf("Removing buffer: <%s>\n", buffer->path);

	ipc_event(buffer, "bufferclose", NULL);

	if (buffer == null_buffer()) return;

	if (buffer->job != NULL) {
		kill(buffer->job->child_pid, SIGTERM);
		buffer->job->terminating = true;
		buffer->job->buffer = NULL;
	}

	{
		editor_t *editor;
		find_editor_for_buffer(buffer, NULL, NULL, &editor);
		if (editor != NULL) {
			editor_switch_buffer(editor, null_buffer());
		}
	}

	int i;
	for(i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == buffer) break;
	}

	if (i < buffers_allocated) {
		buffers[i] = NULL;
	}

	if (buffer->inotify_wd >= 0) {
		int count = 0;
		for (i = 0; i < buffers_allocated; ++i) {
			if (buffers[i] == NULL) continue;
			if (buffers[i]->inotify_wd == buffer->inotify_wd) ++count;
		}
		if (count == 0)
			inotify_rm_watch(inotify_fd, buffer->inotify_wd);
	}

	buffer_free(buffer, save_critbit);
}

enum dirty_reason_t {
	DR_NOT_DIRTY = 0,
	DR_DIRJOB = 1,
	DR_JOB = 2,
	DR_CHANGED = 3,

};

static enum dirty_reason_t buffer_dirty_reason(buffer_t *buffer) {
	if (buffer->job != NULL) {
		if (buffer->path[strlen(buffer->path)-1] == '/') {
			return DR_DIRJOB;
		} else {
			return DR_JOB;
		}
	}

	if (buffer_modified(buffer)) {
		if (buffer->path[0] == '+') return DR_NOT_DIRTY;
		if (buffer->path[strlen(buffer->path)-1] == '/') return DR_NOT_DIRTY;
		return DR_CHANGED;
	}

	return DR_NOT_DIRTY;
}

static bool buffers_close_set(buffer_t **bufset, int n, bool force) {
	bool anydirty = false;
	if (!force) {
		for (int i = 0; i < n; ++i) {
			if (bufset[i] == NULL) continue;

			enum dirty_reason_t dr = buffer_dirty_reason(bufset[i]);

			if ((dr != DR_NOT_DIRTY) && (dr != DR_DIRJOB)) anydirty = true;
		}
	}

	if (anydirty) {
		buffer_t *errbuf = buffers_make("+Errors+");
		if (errbuf == NULL) return false;

		int start, end;
		buffer_get_extremes(errbuf, &start, &end);
		errbuf->mark = start; errbuf->cursor = end;
		buffer_replace_selection(errbuf, "");

		for (int i = 0; i < n; ++i) {
			if (bufset[i] == NULL) continue;

			char bufferid[20];
			buffer_to_buffer_id(bufset[i], bufferid);

			enum dirty_reason_t dr = buffer_dirty_reason(bufset[i]);
			switch (dr) {
			case DR_NOT_DIRTY: continue;
			case DR_DIRJOB: continue;
			case DR_JOB:
				buffer_replace_selection(errbuf, "# Buffer ");
				buffer_replace_selection(errbuf, bufset[i]->path);
				buffer_replace_selection(errbuf, " has a running job attached.\n# Evaluate the following line to kill attached process\n");
				buffer_replace_selection(errbuf, "buffer eval ");
				buffer_replace_selection(errbuf, bufferid);
				buffer_replace_selection(errbuf, " { kill }\n\n");
				break;
			case DR_CHANGED:
				buffer_replace_selection(errbuf, "# Buffer ");
				buffer_replace_selection(errbuf, bufset[i]->path);
				buffer_replace_selection(errbuf, " has unsaved changes.\n# Evaluate the following line to save changes:\n");
				buffer_replace_selection(errbuf, "buffer eval ");
				buffer_replace_selection(errbuf, bufferid);
				buffer_replace_selection(errbuf, " { buffer save }\n# Evaluate the following line to discard changes:\n");
				buffer_replace_selection(errbuf, "buffer force-close ");
				buffer_replace_selection(errbuf, bufferid);
				buffer_replace_selection(errbuf, "\n\n");
				break;
			}
			if (dr == DR_NOT_DIRTY) continue;
			if (dr == DR_DIRJOB) continue;
		}

		errbuf->cursor = -1;

		return false;
	} else {
		for (int i = 0; i < n; ++i) {
			if (bufset[i] == NULL) continue;
			enum dirty_reason_t dr = buffer_dirty_reason(bufset[i]);
			if (dr == DR_DIRJOB) {
				kill(bufset[i]->job->child_pid, SIGTERM);
				bufset[i]->job->terminating = true;
				bufset[i]->job->buffer = NULL;
			}
			buffer_close_real(bufset[i], true);
		}

		return true;
	}
}

bool buffers_close(buffer_t *buffer, bool save_critbit, bool force) {
	return buffers_close_set(&buffer, 1, force);
}

static void maybe_stale_buffer(int wd) {
	if (wd < 0) return;
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->inotify_wd != wd) continue;
		buffer_t *buffer = buffers[i];

		struct stat buf;
		if (stat(buffer->path, &buf) < 0) continue;

		//printf("[%d/%d] <%s> %ld %ld %ld\n", wd, i, buffer->path, buf.st_mtime, buffer->mtime, buf.st_size);

		if (buf.st_mtime < buffer->mtime) continue;
		if (buf.st_size <= 0) continue;

		//printf("Checking for staleness <%s>\n", buffer->path);

		if (buffer->path[strlen(buffer->path)-1] == '/') {
			//printf("Refreshing stale buffer: <%s>\n", buffer->path);
			buffers_refresh(buffer);
		} else if (!buffer_modified(buffer) && config_intval(&(buffer->config), CFG_AUTORELOAD)) {
			buffers_refresh(buffer);
		} else {
			buffer->editable = false;
			buffer->stale = true;
		}

		editor_t *editor;
		if (find_editor_for_buffer(buffer, NULL, NULL, &editor)) {
			gtk_widget_queue_draw(GTK_WIDGET(editor));
		}
	}
}

static gboolean inotify_input_watch_function(GIOChannel *source, GIOCondition condition, gpointer data) {
#define INOTIFY_EVENT_SIZE (sizeof (struct inotify_event))
#define INOTIFY_BUF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))
	char buf[INOTIFY_BUF_LEN];
	gsize len;

	GIOStatus r = g_io_channel_read_chars(source, buf, INOTIFY_BUF_LEN, &len, NULL);

	switch (r) {
	case G_IO_STATUS_NORMAL:
		//continue
		break;
	case G_IO_STATUS_ERROR:
		return FALSE;
	case G_IO_STATUS_EOF:
		return FALSE;
	case G_IO_STATUS_AGAIN:
		return TRUE;
	default:
		return FALSE;
	}

	int i = 0;
	while (i < len) {
		struct inotify_event *event = (struct inotify_event *)(buf+i);

		//printf("mask = %d (%d)\n", event->mask, event->wd);

		if (event->wd == tags_wd) {
			tags_load(top_working_directory());
		}

		maybe_stale_buffer(event->wd);

		i += INOTIFY_EVENT_SIZE + event->len;
	}

	return TRUE;
}

void buffers_init(void) {
	process_buffers_counter = 0;

	buffers_allocated = 10;
	buffers = malloc(sizeof(buffer_t *) * buffers_allocated);
	alloc_assert(buffers);

	for (int i = 0; i < buffers_allocated; ++i) {
		buffers[i] = NULL;
	}

	buffers[0] = buffer_create();
	free(buffers[0]->path);
	asprintf(&(buffers[0]->path), "+null+");
	load_empty(buffers[0]);
	buffers[0]->editable = 0;

	inotify_fd = inotify_init();
	if (inotify_fd < 0)
		fprintf(stderr, "Could not start inotify\n");
	else {
		inotify_channel = g_io_channel_unix_new(inotify_fd);
		g_io_channel_set_encoding(inotify_channel, NULL, NULL);
		GError *error = NULL;
		g_io_channel_set_flags(inotify_channel, g_io_channel_get_flags(inotify_channel)|G_IO_FLAG_NONBLOCK, &error);
		if (error != NULL) { fprintf(stderr, "There was a strange error (2)"); g_error_free(error); error = NULL; }
		inotify_channel_source_id = g_io_add_watch(inotify_channel, G_IO_IN|G_IO_HUP, (GIOFunc)(inotify_input_watch_function), NULL);
	}
}

static void buffers_grow() {
	int i;
	buffers = realloc(buffers, sizeof(buffer_t *) * buffers_allocated * 2);
	alloc_assert(buffers);
	for (i = buffers_allocated; i < buffers_allocated * 2; ++i) {
		buffers[i] = NULL;
	}
	buffers_allocated *= 2;
}

void buffers_add(buffer_t *b) {
	int i;
	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) {
			buffers[i] = b;
			break;
		}
	}

	if (i >= buffers_allocated) {
		buffers_grow();
		buffers_add(b);
	}

	if ((b->path[0] != '+') && (inotify_fd >= 0)) {
		b->inotify_wd = inotify_add_watch(inotify_fd, b->path, IN_CLOSE_WRITE);
	}

	word_completer_full_update();
}

void buffers_free(void) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] != NULL) {
			buffer_free(buffers[i], false);
			buffers[i] = NULL;
		}
	}

	if (inotify_fd >= 0) {
		g_source_remove(inotify_channel_source_id);
		g_io_channel_shutdown(inotify_channel, FALSE, NULL);
		g_io_channel_unref(inotify_channel);
		close(inotify_fd);
	}
}

bool buffers_close_all(bool force) {
	return buffers_close_set(buffers, buffers_allocated, force);
}

buffer_t *buffers_create_with_name(char *name) {
	buffer_t *buffer = buffer_create();
	if (buffer->path != NULL) {
		free(buffer->path);
	}
	buffer->path = name;
	load_empty(buffer);

	buffers_add(buffer);

	return buffer;
}

buffer_t *buffers_get_buffer_for_process(bool create) {
	buffer_t *buffer;
	int i;
	// look for a buffer with a name starting by +bg/ that doesn't have a process
	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->path== NULL) continue;
		if (strncmp(buffers[i]->path, "+bg/", 4) != 0) continue;
		if (buffers[i]->job != NULL) continue;
		break;
	}

	if (i >= buffers_allocated) {
		if (!create) return NULL;

		char *bufname;
		asprintf(&bufname, "+bg/%d+", process_buffers_counter);
		alloc_assert(bufname);
		++process_buffers_counter;

		buffer = buffers_create_with_name(bufname);
	} else {
		buffer = buffers[i];
	}

	return buffer;
}

buffer_t *buffer_id_to_buffer(const char *bufferid) {
	if (strncmp(bufferid, "@b", 2) != 0) return NULL;

	long bid = strtol(bufferid+2, NULL, 10);
	if (bid < 0) return NULL;
	if (bid >= buffers_allocated) return NULL;

	return buffers[bid];
}

#define BUFIDCHECK(buffer) { \
	if (buffer == NULL) { \
		Tcl_AddErrorInfo(interp, "Unknown buffer id");\
		return TCL_ERROR;\
	}\
}

buffer_t *buffers_find_buffer_from_path(const char *urp) {
	char *rp = realpath(urp, NULL);
	buffer_t *r = NULL;
	int i;

	if (rp == NULL) {
		return NULL;
	}

	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->path == NULL) continue;
		if (strcmp(buffers[i]->path, rp) == 0) {
			r = buffers[i];
			break;
		}
		//check if buffers[i]->path is like rp plus a slash at the end
		//printf("Check <%s> <%s> %d %d %d\n", rp, buffers[i]->path, strncmp(buffers[i]->path, rp, strlen(rp)) == 0, strlen(rp) == strlen(buffers[i]->path), buffers[i]->path[strlen(buffers[i]->path)-1] == '/');
		if ((strncmp(buffers[i]->path, rp, strlen(rp)) == 0) && (strlen(rp) == strlen(buffers[i]->path) - 1) && (buffers[i]->path[strlen(buffers[i]->path)-1] == '/')) {
			r = buffers[i];
			break;
		}
	}

	free(rp);
	return r;
}

#define SINGLE_ARGUMENT_BUFFER_SUBCOMMAND(name) editor_t *editor; buffer_t *buffer; {\
	if (argc == 2) { \
		HASBUF(name); \
		buffer = interp_context_buffer(); \
	} else if (argc == 3) { \
		buffer = buffer_id_to_buffer(argv[2]);\
		BUFIDCHECK(buffer);\
	} else {\
		ARGNUM(false, name);\
	}\
	find_editor_for_buffer(buffer, NULL, NULL, &editor);\
}

int teddy_buffer_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc < 2), "buffer");
	char bufferid[20];

	if (strcmp(argv[1], "make") == 0) {
		ARGNUM((argc != 3), "buffer make");

		buffer_t *buffer = buffers_make(argv[2]);

		buffer_to_buffer_id(buffer, bufferid);
		Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
	} else if (strcmp(argv[1], "dbg") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer dbg");
		char *msg;
		asprintf(&msg, "lexy_running = %d, lexy_start = %d", buffer->lexy_running, buffer->lexy_start);
		alloc_assert(msg);
		quick_message("DBG", msg);
		free(msg);
	} else if (strcmp(argv[1], "find") == 0) {
		ARGNUM((argc != 3), "buffer find");
		buffer_t *buffer = buffers_find_buffer_with_name(argv[2]);
		buffer_to_buffer_id(buffer, bufferid);
		Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
	} else if (strcmp(argv[1], "coc") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer coc");
		if (editor != NULL) editor_include_cursor(editor, ICM_MID, ICM_MID);
	} else if (strcmp(argv[1], "lexy") == 0) {
		ARGNUM((argc != 2), "buffer lexy");
		for (int i = 0; i < buffers_allocated; ++i) {
			if (buffers[i] == NULL) continue;
			lexy_update_starting_at(buffers[i], -1, false);
		}
	} else if (strcmp(argv[1], "save") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer save");
		if (editor != NULL) editor_save_action(editor); else save_to_text_file(buffer);
	} else if (strcmp(argv[1], "open") == 0) {
		ARGNUM((argc != 3), "buffer open");
		//HASBUF("buffer open");

		enum go_file_failure_reason gffr;
		buffer_t *b = go_file(argv[2], false, false, &gffr);
		if (b != NULL) {
			tframe_t *frame;
			find_editor_for_buffer(b, NULL, &frame, NULL);
			if (frame == NULL) {
				if (interp_context_buffer() != NULL) {
					find_editor_for_buffer(interp_context_buffer(), NULL, &frame, NULL);
				}
				heuristic_new_frame(columnset, frame, b);
			}

			buffer_to_buffer_id(b, bufferid);
			Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
		} else {
			Tcl_SetResult(interp, "", TCL_VOLATILE);
		}
	} else if (strcmp(argv[1], "focus") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer focus");
		if (editor != NULL) {
			editor_include_cursor(editor, ICM_MID, ICM_MID);
			editor_grab_focus(editor, true);
		}
	} else if (strcmp(argv[1], "dup") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer dup");
		if (buffer->path[0] == '+') {
			// create new buffer, copy text over
		} else {
			enum go_file_failure_reason gffr;
			buffer_t *b = go_file(interp_context_buffer()->path, false, true, &gffr);
			go_to_buffer(interp_context_editor(), b, false);
		}
	} else if (strcmp(argv[1], "current") == 0) {
		if (interp_context_buffer() != NULL) {
			buffer_to_buffer_id(interp_context_buffer(), bufferid);
		} else {
			buffer_to_buffer_id(null_buffer(), bufferid);
		}
		Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
	} else if (strcmp(argv[1], "propget") == 0) {
		ARGNUM((argc != 4), "buffer propget");

		const char *bufferid = argv[2];
		const char *propname = argv[3];

		buffer_t *buffer = buffer_id_to_buffer(bufferid);
		BUFIDCHECK(buffer);

		char *propvalue = g_hash_table_lookup(buffer->props, propname);

		Tcl_SetResult(interp, (propvalue != NULL) ? propvalue : "", TCL_VOLATILE);
	} else if (strcmp(argv[1], "propset") == 0) {
		ARGNUM((argc != 5), "buffer propset");

		const char *bufferid = argv[2];
		char *propname = strdup(argv[3]);
		char *propvalue = strdup(argv[4]);
		alloc_assert(propvalue);
		alloc_assert(propname);

		buffer_t *buffer = buffer_id_to_buffer(bufferid);
		BUFIDCHECK(buffer);

		g_hash_table_insert(buffer->props, propname, propvalue);
	} else if (strcmp(argv[1], "ls") == 0) {
		Tcl_Obj **retval = malloc(sizeof(Tcl_Obj *) * buffers_allocated);
		alloc_assert(retval);

		int cap = 0;
		for (int i = 0; i < buffers_allocated; ++i) {
			if (buffers[i] != NULL) {
				snprintf(bufferid, 15, "@b%d", i);
				retval[cap] = Tcl_NewStringObj(bufferid, strlen(bufferid));
				Tcl_IncrRefCount(retval[cap]);
				++cap;
			}
		}

		Tcl_Obj *retlist = Tcl_NewListObj(cap, retval);
		Tcl_SetObjResult(interp, retlist);

		for (int i = 0; i < cap; ++i) {
			Tcl_DecrRefCount(retval[i]);
		}

		free(retval);
	} else if (strcmp(argv[1], "setkeyprocessor") == 0) {
		ARGNUM((argc != 4), "buffer setkeyprocessor");

		buffer_t *buffer = buffer_id_to_buffer(argv[2]);
		BUFIDCHECK(buffer);

		if (buffer->keyprocessor != NULL) free(buffer->keyprocessor);
		buffer->keyprocessor = strdup(argv[3]);

		return TCL_OK;
	} else if (strcmp(argv[1], "eval") == 0) {
		ARGNUM((argc != 4), "buffer eval");

		buffer_t *buffer = NULL;
		editor_t *editor = NULL;

		if (strcmp(argv[2], "temp") == 0) {
			buffer = buffer_create();
			load_empty(buffer);
		} else if (strcmp(argv[2], "cmdline") == 0) {
			buffer = cmdline_buffer;
		} else {
			buffer = buffer_id_to_buffer(argv[2]);
			find_editor_for_buffer(buffer, NULL, NULL, &editor);
		}

		BUFIDCHECK(buffer);

		int r = interp_eval(editor, buffer, argv[3], false, false);
		if (strcmp(argv[2], "temp") == 0) buffer_free(buffer, false);
		return r;
	} else if (strcmp(argv[1], "close") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer close");
		tframe_t *tf = NULL;
		column_t *col = NULL;
		find_editor_for_buffer(buffer, &col, &tf, NULL);
		if ((tf != NULL) && (col != NULL)) {
			columns_column_remove(columnset, col, tf, false, true);
		} else {
			if (buffers_close_set(&buffer, 1, false))
				if (buffer == interp_context_buffer())
					top_context_editor_gone();
		}

		if ((buffer != NULL) && (interp_context_buffer() == buffer)) {
			interp_context_buffer_set(NULL);
			top_context_editor_gone();
		}
	} else if (strcmp(argv[1], "force-close") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer force-close");
		buffer_close_real(buffer, true);
		if (buffer == interp_context_buffer())
			top_context_editor_gone();
	} else if (strcmp(argv[1], "closeall") == 0) {
		ARGNUM((argc != 2), "buffer closeall");
		if (buffers_close_all(false)) {
			GList *column_list = gtk_container_get_children(GTK_CONTAINER(columnset));
			for (GList *cur = column_list; cur != NULL; cur = cur->next) {
				column_close(GTK_COLUMN(cur->data));
				gtk_container_remove(GTK_CONTAINER(columnset), cur->data);
			}
			g_list_free(column_list);
			top_context_editor_gone();
		}
	} else if (strcmp(argv[1], "column-setup") == 0) {
		ARGNUM((argc < 3), "buffer column-setup");

		column_t *column = column_new(0);
		columns_append(columnset, column, false);
		column_fraction_set(column, atof(argv[2]));

		for (int i = 3; i < argc; i += 2) {
			if (i+1 >= argc) {
				Tcl_AddErrorInfo(interp, "Odd number of arguments to buffer column-setup");
				return TCL_ERROR;
			}

			editor_t *editor = new_editor(null_buffer(), false);
			tframe_t *frame = tframe_new("", GTK_WIDGET(editor), columnset);
			column_append(column, frame, false);
			tframe_fraction_set(frame, atof(argv[i]));

			enum go_file_failure_reason gffr;
			buffer_t *buffer;
			if (argv[i+1][0] == '+') {
				buffer = buffers_create_with_name(strdup(argv[i+1]));
			} else {
				buffer = go_file(argv[i+1], false, false, &gffr);
			}

			if (buffer != NULL) go_to_buffer(editor, buffer, true);
		}
		return TCL_OK;
	} else if (strcmp(argv[1], "rename") == 0) {
		HASBUF("buffer rename");
		ARGNUM((argc != 3), "buffer rename");

		if (interp_context_buffer()->has_filename) {
			Tcl_AddErrorInfo(interp, "Can not rename a buffer attached to a filename");
			return TCL_ERROR;
		}

		if (interp_context_buffer()->path[0] != '+') {
			Tcl_AddErrorInfo(interp, "Can not rename a buffer attached to a filename");
			return TCL_ERROR;
		}

		free(interp_context_buffer()->path);
		interp_context_buffer()->path = strdup(argv[2]);
		alloc_assert(interp_context_buffer()->path);
	} else if (strcmp(argv[1], "name") == 0) {
		SINGLE_ARGUMENT_BUFFER_SUBCOMMAND("buffer name");
		Tcl_SetResult(interp, buffer->path, TCL_VOLATILE);
		return TCL_OK;
	} else {
		Tcl_AddErrorInfo(interp, "Unknown subcommmand of 'buffer' command");
		return TCL_ERROR;
	}

	return TCL_OK;
}

void buffers_refresh(buffer_t *buffer) {
	editor_t *editor;
	find_editor_for_buffer(buffer, NULL, NULL, &editor);

	//printf("Refreshing buffer <%s>\n", buffer->path);

	if (buffer->path[strlen(buffer->path)-1] == '/') {
		buffer_aux_clear(buffer);
		const char *cmd[] = { "teddy_intl::dir", buffer->path };
		interp_eval_command(NULL, NULL, 2, cmd);
		buffer->mtime = time(NULL);
		return;
	}

	// do not refresh special buffers
	if (buffer->path[0] == '+') return;

	char *path = strdup(buffer->path);
	alloc_assert(path);

	if (null_buffer() == buffer) return;

	int cursor = buffer->cursor;

	int r = buffers_close(buffer, false, false);
	if (r == 0) return;

	enum go_file_failure_reason gffr;
	buffer_t *new_buffer = go_file(path, false, true, &gffr);
	if (new_buffer != NULL) {
		if (cursor < BSIZE(new_buffer)) new_buffer->cursor = cursor;
		editor_switch_buffer(editor, new_buffer);
	}

	free(path);
}

void buffers_register_tags(const char *tags_file) {
	if (tags_wd < 0) inotify_rm_watch(inotify_fd, tags_wd);
	tags_wd = inotify_add_watch(inotify_fd, tags_file, IN_CLOSE_WRITE);
}

buffer_t *go_file(const char *filename, bool create, bool skip_search, enum go_file_failure_reason *gffr) {
	char *urp = unrealpath(filename, false);

	*gffr = GFFR_OTHER;

	if (urp == NULL) {
		return NULL;
	}

	//printf("going file\n");

	buffer_t *buffer;

	if (!skip_search) {
		buffer = buffers_find_buffer_from_path(urp);
		if (buffer != NULL) goto go_file_return;
	}

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

	if (S_ISDIR(s.st_mode)) {
		buffer = buffer_create();
		load_dir(buffer, urp);
		buffers_add(buffer);
		const char *cmd[] = { "teddy_intl::dir", urp };
		interp_eval_command(NULL, NULL, 2, cmd);
		buffer->mtime = time(NULL);
	} else if (s.st_size > MAXIMUM_FILE_SIZE) {
		buffer = NULL;
		goto go_file_return;
	} else {
		buffer = buffer_create();
		int r = load_text_file(buffer, urp);
		if (r != 0) {
			if (r == -2) {
				*gffr = GFFR_BINARYFILE;
			}
			buffer_free(buffer, false);
			buffer = NULL;
		}

		if (buffer != NULL) {
			buffers_add(buffer);
		}
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
