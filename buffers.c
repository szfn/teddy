#include "buffers.h"

#include "global.h"
#include "columns.h"
#include "editor.h"
#include "go.h"
#include "baux.h"
#include "tags.h"
#include "top.h"

#include "critbit.h"

#include <gdk/gdkkeysyms.h>
#include <glib-object.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/inotify.h>

buffer_t **buffers;
int buffers_allocated;

int inotify_fd;
GIOChannel *inotify_channel;
guint inotify_channel_source_id;
int tags_wd = -1;

int process_buffers_counter = 0;

buffer_t *null_buffer(void) {
	return buffers[0];
}

#define SAVE_AND_CLOSE_RESPONSE 1
#define DISCARD_CHANGES_RESPONSE 2
#define CANCEL_ACTION_RESPONSE 3
#define KILL_AND_CLOSE_RESPONSE 4

static int ask_for_closing_and_maybe_terminate(buffer_t *buffer, GtkWidget *window) {
	if (buffer->path[strlen(buffer->path)-1] == '/') {
		kill(buffer->job->child_pid, SIGTERM);
		buffer->job->terminating = true;
		buffer->job->buffer = NULL;
		return 1;
	}

	GtkWidget *dialog = gtk_dialog_new_with_buttons("Close Buffer", GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Kill and close", KILL_AND_CLOSE_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);

	char *msg;
	asprintf(&msg, "A process is attached to buffer %s", buffer->path);
	alloc_assert(msg);
	GtkWidget *label = gtk_label_new(msg);
	free(msg);

	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);

	gtk_widget_show_all(dialog);
	gint result = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	switch (result) {
	case KILL_AND_CLOSE_RESPONSE:
		if (buffer->job != NULL) {
			kill(buffer->job->child_pid, SIGTERM);
			buffer->job->terminating = true;
			buffer->job->buffer = NULL;
		}
		break;
	case CANCEL_ACTION_RESPONSE:
	default: return 0;
	}

	return 1;
}

static int ask_for_closing_and_maybe_save(buffer_t *buffer, GtkWidget *window) {
	if (!(buffer->has_filename) && (buffer->path[0] == '+')) return 1; /* Trash buffer, can be discarded safely */

	GtkWidget *dialog;
	if (buffer->has_filename) {
		dialog = gtk_dialog_new_with_buttons("Close Buffer", GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Save and close", SAVE_AND_CLOSE_RESPONSE, "Discard changes", DISCARD_CHANGES_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);
	} else {
		dialog = gtk_dialog_new_with_buttons("Close Buffer", GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Discard changes", DISCARD_CHANGES_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);
	}

	char *msg;
	asprintf(&msg, "Buffer [%s] is modified", buffer->path);
	alloc_assert(msg);
	GtkWidget *label = gtk_label_new(msg);
	free(msg);

	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);

	//g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
	gtk_widget_show_all(dialog);
	gint result = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	//printf("Response is: %d (%d %d %d)\n", result, SAVE_AND_CLOSE_RESPONSE, DISCARD_CHANGES_RESPONSE, CANCEL_ACTION_RESPONSE);

	switch(result) {
	case SAVE_AND_CLOSE_RESPONSE:
		save_to_text_file(buffer);
		break;
	case DISCARD_CHANGES_RESPONSE:
		//printf("Discarding changes to: [%s]\n", buffer->name);
		break;
	case CANCEL_ACTION_RESPONSE: return 0;
	default: return 0; /* This shouldn't happen */
	}

	return 1;
}

int buffers_close(buffer_t *buffer, GtkWidget *window, bool save_critbit) {
	if (buffers[0] == buffer) return 1;

	if (buffer->modified && (buffer->path[strlen(buffer->path)-1] != '/')) {
		int r = ask_for_closing_and_maybe_save(buffer, window);
		if (r == 0) return 0;
	}

	if (buffer->job != NULL) {
		int r = ask_for_closing_and_maybe_terminate(buffer, window);
		if (r == 0) return 0;
	}

	//printf("Removing buffer: <%s>\n", buffer->path);

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
		inotify_rm_watch(inotify_fd, buffer->inotify_wd);
	}

	buffer_free(buffer, save_critbit);

	return 1;
}

static buffer_t *get_buffer_by_inotify(int wd) {
	if (wd < 0) return NULL;
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->inotify_wd == wd) return buffers[i];
	}
	return NULL;
}

static void maybe_stale_buffer(int wd) {
	buffer_t *buffer = get_buffer_by_inotify(wd);

	if (buffer == NULL) return;

	struct stat buf;
	if (stat(buffer->path, &buf) < 0) return;

	//printf("<%s> %ld %ld %ld\n", buffer->path, buf.st_mtime, buffer->mtime, buf.st_size);

	if (buf.st_mtime < buffer->mtime+30) return;
	if (buf.st_size <= 0) return;

	//printf("Checking for staleness <%s>\n", buffer->path);

	if (buffer->path[strlen(buffer->path)-1] == '/') {
		//printf("Refreshing stale buffer: <%s>\n", buffer->path);
		buffers_refresh(buffer);
	} else if (!(buffer->modified) && config_intval(&(buffer->config), CFG_AUTORELOAD)) {
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

int buffers_close_all(GtkWidget *window) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (!buffers_close(buffers[i], window, false)) return 0;
	}
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->modified) return 0;
	}
	return 1;
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

buffer_t *buffers_get_buffer_for_process(void) {
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
		char *bufname;
		asprintf(&bufname, "+bg/%d+", process_buffers_counter);
		++process_buffers_counter;

		buffer = buffers_create_with_name(bufname);
	} else {
		buffer = buffers[i];
	}

	return buffer;
}

static void buffer_to_buffer_id(buffer_t *buffer, char *bufferid) {
	strcpy(bufferid, "@b0");
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == buffer) {
			snprintf(bufferid+2, 15, "%d", i);
			return;
		}
	}
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
		if (!(buffers[i]->has_filename)) continue;
		if (buffers[i]->path == NULL) continue;
		if (strcmp(buffers[i]->path, rp) == 0) {
			r = buffers[i];
			break;
		}
	}

	free(rp);
	return r;
}

static buffer_t *buffers_find_buffer_with_name(const char *name) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (strcmp(buffers[i]->path, name) == 0) {
			return buffers[i];
		}
	}

	return NULL;
}

int teddy_buffer_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	ARGNUM((argc < 2), "buffer");

	if (strcmp(argv[1], "make") == 0) {
		ARGNUM((argc != 3), "buffer make");

		buffer_t *buffer = buffers_find_buffer_with_name(argv[2]);
		if (buffer == NULL) {
			buffer = buffers_create_with_name(strdup(argv[2]));
			if (buffer != NULL) {
				tframe_t *frame;
				find_editor_for_buffer(interp_context_buffer(), NULL, &frame, NULL);
				heuristic_new_frame(columnset, frame, buffer);
			}
		}

		char bufferid[20];
		buffer_to_buffer_id(buffer, bufferid);
		Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
	} else if (strcmp(argv[1], "save") == 0) {
		ARGNUM((argc != 2), "buffer save");
		HASED("buffer");

		editor_save_action(interp_context_editor());
	} else if (strcmp(argv[1], "open") == 0) {
		ARGNUM((argc != 3), "buffer open");
		HASBUF("buffer open");

		enum go_file_failure_reason gffr;
		buffer_t *b = go_file(argv[2], false, &gffr);
		if (b != NULL) {
			tframe_t *frame;
			find_editor_for_buffer(b, NULL, &frame, NULL);
			if (frame == NULL) {
				find_editor_for_buffer(interp_context_buffer(), NULL, &frame, NULL);
				heuristic_new_frame(columnset, frame, b);
			}

			char bufferid[20];
			buffer_to_buffer_id(b, bufferid);
			Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
		} else {
			Tcl_SetResult(interp, "", TCL_VOLATILE);
		}
	} else if (strcmp(argv[1], "focus") == 0) {
		ARGNUM((argc != 3), "buffer focus");

		buffer_t *buffer = buffer_id_to_buffer(argv[2]);
		BUFIDCHECK(buffer);

		editor_t *editor;
		find_editor_for_buffer(buffer, NULL, NULL, &editor);

		if (editor != NULL) editor_grab_focus(editor, true);
	} else if (strcmp(argv[1], "select-mode") == 0) {
		ARGNUM((argc != 3), "buffer select-mode");
		HASBUF("buffer select-mode");

		if (strcmp(argv[2], "normal") == 0) {
			buffer_change_select_type(interp_context_buffer(), BST_NORMAL);
		} else if (strcmp(argv[2], "words") == 0) {
			if (interp_context_buffer()->mark.line == NULL) {
				copy_lpoint(&(interp_context_buffer()->mark), &(interp_context_buffer()->cursor));
			}
			buffer_change_select_type(interp_context_buffer(), BST_WORDS);
		} else if (strcmp(argv[2], "lines") == 0) {
			if (interp_context_buffer()->mark.line == NULL) {
				copy_lpoint(&(interp_context_buffer()->mark), &(interp_context_buffer()->cursor));
			}
			buffer_change_select_type(interp_context_buffer(), BST_LINES);
		} else {
			Tcl_AddErrorInfo(interp, "Bad argument to 'buffer select-mode' command");
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if (strcmp(argv[1], "current") == 0) {
		HASBUF("buffer current");
		char bufferid[20];
		buffer_to_buffer_id(interp_context_buffer(), bufferid);
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
				char bufferid[20];
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
		} else {
			buffer = buffer_id_to_buffer(argv[2]);
			find_editor_for_buffer(buffer, NULL, NULL, &editor);
		}

		BUFIDCHECK(buffer);

		interp_eval(editor, buffer, argv[3], false);
		if (strcmp(argv[2], "temp") == 0) buffer_free(buffer, false);
	} else {
		Tcl_AddErrorInfo(interp, "Unknown subcommmand of 'buffer' command");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int refill_word_completer(const char *entry, void *p) {
	//printf("\t<%s>\n", entry);
	compl_add(&the_word_completer, entry);
	return 1;
}

void word_completer_full_update(void) {
	critbit0_clear(&(the_word_completer.cbt));

	//printf("Filling main word completer:\n");

	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;

		critbit0_allprefixed(&(buffers[i]->cbt), "", refill_word_completer, NULL);
	}

	critbit0_allprefixed(&closed_buffers_critbit, "", refill_word_completer, NULL);
	critbit0_allprefixed(&tags_file_critbit, "", refill_word_completer, NULL);
}

void buffers_refresh(buffer_t *buffer) {
	editor_t *editor;
	find_editor_for_buffer(buffer, NULL, NULL, &editor);

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

	int lineno = 1, glyph = 1;

	if (buffer->cursor.line != NULL) {
		lineno = buffer->cursor.line->lineno+1;
		glyph = buffer->cursor.glyph+1;
	}

	int r = buffers_close(buffer, gtk_widget_get_toplevel(GTK_WIDGET(columnset)), false);
	if (r == 0) return;

	enum go_file_failure_reason gffr;
	buffer_t *new_buffer = go_file(path, false, &gffr);
	if (new_buffer != NULL) {
		buffer_move_point_line(new_buffer, &(new_buffer->cursor), MT_ABS, lineno);
		buffer_move_point_glyph(new_buffer, &(new_buffer->cursor), MT_ABS, glyph);

		editor_switch_buffer(editor, new_buffer);
	}


	free(path);
}

void buffers_register_tags(const char *tags_file) {
	if (tags_wd < 0) inotify_rm_watch(inotify_fd, tags_wd);
	tags_wd = inotify_add_watch(inotify_fd, tags_file, IN_CLOSE_WRITE);
}
