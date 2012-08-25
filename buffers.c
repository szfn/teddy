#include "buffers.h"

#include "global.h"
#include "columns.h"
#include "editor.h"
#include "go.h"

#include "critbit.h"

#include <gdk/gdkkeysyms.h>
#include <glib-object.h>

#include <stdlib.h>
#include <stdio.h>

buffer_t **buffers;
int buffers_allocated;

int process_buffers_counter = 0;

buffer_t *null_buffer(void) {
	return buffers[0];
}

#define SAVE_AND_CLOSE_RESPONSE 1
#define DISCARD_CHANGES_RESPONSE 2
#define CANCEL_ACTION_RESPONSE 3

static int ask_for_closing_and_maybe_save(buffer_t *buffer, GtkWidget *window) {
	GtkWidget *dialog;
	GtkWidget *label;
	char *msg;
	gint result;

	if (!(buffer->has_filename) && (buffer->path[0] == '+')) return 1; /* Trash buffer, can be discarded safely */

	if (buffer->has_filename) {
		dialog = gtk_dialog_new_with_buttons("Close Buffer", (window != NULL) ? GTK_WINDOW(window) : GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Save and close", SAVE_AND_CLOSE_RESPONSE, "Discard changes", DISCARD_CHANGES_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);
	} else {
		dialog = gtk_dialog_new_with_buttons("Close Buffer", (window != NULL) ? GTK_WINDOW(window) : GTK_WINDOW(window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Discard changes", DISCARD_CHANGES_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);
	}

	asprintf(&msg, "Buffer [%s] is modified", buffer->path);
	label = gtk_label_new(msg);
	free(msg);

	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);

	//g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
	gtk_widget_show_all(dialog);
	result = gtk_dialog_run(GTK_DIALOG(dialog));
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

int buffers_close(buffer_t *buffer, GtkWidget *window) {
	if (buffers[0] == buffer) return 1;

	if (buffer->modified) {
		int r = ask_for_closing_and_maybe_save(buffer, window);
		if (r == 0) return 0;
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

	buffer_free(buffer);

	return 1;
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
	{
		free(buffers[0]->path);
		asprintf(&(buffers[0]->path), "+null+");
		load_empty(buffers[0]);
		buffers[0]->editable = 0;
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
}

void buffers_free(void) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] != NULL) {
			buffer_free(buffers[i]);
			buffers[i] = NULL;
		}
	}
}

int buffers_close_all(GtkWidget *window) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (!buffers_close(buffers[i], window)) return 0;
	}
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->modified) return 0;
	}
	return 1;
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

static void tcl_dict_add_string(Tcl_Obj *dict, const char *key, const char *value) {
	Tcl_Obj *kobj = Tcl_NewStringObj(key, strlen(key));
	Tcl_IncrRefCount(kobj);
	Tcl_Obj *vobj;
	if (value == NULL) {
		vobj = Tcl_NewStringObj("", 0);
	} else {
		vobj = Tcl_NewStringObj(value, strlen(value));
	}
	Tcl_IncrRefCount(vobj);

	Tcl_DictObjPut(interp, dict, kobj, vobj);
	Tcl_DecrRefCount(kobj);
	Tcl_DecrRefCount(vobj);
}

int teddy_buffer_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer' command");
		return TCL_ERROR;
	}

	if (interp_context_editor() == NULL) {
		Tcl_AddErrorInfo(interp, "buffer command invoked when no editor is active");
		return TCL_ERROR;
	}

	if (strcmp(argv[1], "make") == 0) {
		if (argc != 3) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer make' command");
			return TCL_ERROR;
		}

		buffer_t *buffer = buffers_create_with_name(strdup(argv[2]));

		char bufferid[20];
		buffer_to_buffer_id(buffer, bufferid);
		Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
	} else if (strcmp(argv[1], "open") == 0) {
		if (argc != 3) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer open' command");
			return TCL_ERROR;
		}

		enum go_file_failure_reason gffr;
		buffer_t *b = go_file(argv[2], false, &gffr);
		if (b != NULL) {
			tframe_t *frame;
			find_editor_for_buffer(interp_context_buffer(), NULL, &frame, NULL);
			heuristic_new_frame(columnset, frame, b);
		}
	} else if (strcmp(argv[1], "select-mode") == 0) {
		//TODO
	} else if (strcmp(argv[1], "scratch") == 0) {
		if (argc != 2) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer scratch' command");
			return TCL_ERROR;
		}

		if (interp_context_editor() == NULL) {
			Tcl_AddErrorInfo(interp, "Can not call 'buffer scratch' when no editor is active");
			return TCL_ERROR;
		}

		int i;

		for (i = 0; i < buffers_allocated; ++i) {
			if (buffers[i] == NULL) continue;
			if (strcmp(buffers[i]->path, "+scrach+") == 0) break;
		}

		buffer_t *buffer;

		if (i >= buffers_allocated) {
			buffer = buffers_create_with_name(strdup("+scratch+"));
		} else {
			buffer = buffers[i];
		}

		go_to_buffer(interp_context_editor(), buffer, false);
	} else if (strcmp(argv[1], "current") == 0) {
		char bufferid[20];
		buffer_to_buffer_id(interp_context_buffer(), bufferid);
		Tcl_SetResult(interp, bufferid, TCL_VOLATILE);
	} else if (strcmp(argv[1], "propget") == 0) {
		if (argc != 4) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer propget' command");
			return TCL_ERROR;
		}

		const char *bufferid = argv[2];
		const char *propname = argv[3];

		buffer_t *buffer = buffer_id_to_buffer(bufferid);

		if (buffer == NULL) {
			Tcl_AddErrorInfo(interp, "Unknown buffer id");
			return TCL_ERROR;
		}

		char *propvalue = g_hash_table_lookup(buffer->props, propname);

		Tcl_SetResult(interp, (propvalue != NULL) ? propvalue : "", TCL_VOLATILE);
	} else if (strcmp(argv[1], "propset") == 0) {
		if (argc != 5) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer propset' command");
			return TCL_ERROR;
		}

		const char *bufferid = argv[2];
		char *propname = strdup(argv[3]);
		char *propvalue = strdup(argv[4]);
		alloc_assert(propvalue);
		alloc_assert(propname);

		buffer_t *buffer = buffer_id_to_buffer(bufferid);

		if (buffer == NULL) {
			Tcl_AddErrorInfo(interp, "Unknown buffer id");
			return TCL_ERROR;
		}

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
	} else if (strcmp(argv[1], "info") == 0) {
		if (argc != 3) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer info'");
			return TCL_ERROR;
		}

		const char *bufferid = argv[2];

		buffer_t *buffer = buffer_id_to_buffer(bufferid);

		if (buffer == NULL) {
			Tcl_AddErrorInfo(interp, "Unknown buffer id");
			return TCL_ERROR;
		}

		Tcl_Obj *ret = Tcl_NewDictObj();
		Tcl_SetObjResult(interp, ret);

		tcl_dict_add_string(ret, "id", bufferid);
		tcl_dict_add_string(ret, "name", buffer->path);
		tcl_dict_add_string(ret, "path", buffer->path);

		return TCL_OK;
	} else if (strcmp(argv[1], "setkeyprocessor") == 0) {
		if (argc != 4) {
			Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'buffer setkeyprocessor'");
			return TCL_ERROR;
		}

		buffer_t *buffer = buffer_id_to_buffer(argv[2]);

		if (buffer->keyprocessor != NULL) free(buffer->keyprocessor);
		buffer->keyprocessor = strdup(argv[3]);

		return TCL_OK;
	} else {
		Tcl_AddErrorInfo(interp, "Unknown subcommmand of 'buffer' command");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int refill_word_completer(const char *entry, void *p) {
	compl_add(&the_word_completer, entry);
	return 1;
}

void word_completer_full_update(void) {
	critbit0_clear(&(the_word_completer.cbt));

	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;

		critbit0_allprefixed(&(buffers[i]->cbt), "", refill_word_completer, NULL);
	}
}
