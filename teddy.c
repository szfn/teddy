#include <stdio.h>
#include <stdlib.h>
#include <fontconfig/fontconfig.h>

#include "git.date.h"
#include "global.h"
#include "undo.h"
#include "buffers.h"
#include "columns.h"
#include "interp.h"
#include "jobs.h"
#include "colors.h"
#include "cfg.h"
#include "research.h"
#include "lexy.h"
#include "foundry.h"
#include "iopen.h"
#include "top.h"
#include "tags.h"

static gboolean delete_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
	if (buffers_close_all()) return FALSE;
	return TRUE;
}

static void setup_initial_columns(int argc, char *argv[]) {
	column_t *col1 = column_new(0);
	column_t *col2 = column_new(0);

	columns_add_after(columnset, NULL, col1, true);
	columns_add_after(columnset, col1, col2, true);

	heuristic_new_frame(columnset, NULL, null_buffer());

	enum go_file_failure_reason gffr;
	buffer_t *cur_dir_buffer = go_file(".", false, false, &gffr);
	tframe_t *dirframe = heuristic_new_frame(columnset, NULL, cur_dir_buffer);

	tframe_t *frame = NULL;

	for (int i = 1; i < argc; ++i) {
		buffer_t *buffer = buffer_create();
		if (load_text_file(buffer, argv[i]) < 0) {
			buffer_free(buffer, false);
		} else {
			buffers_add(buffer);
			tframe_t *f = heuristic_new_frame(columnset, NULL, buffer);
			if (frame == NULL) frame = f;
		}
	}

	if (frame == NULL) frame = dirframe;

	if (frame != NULL) {
		GtkWidget *w = tframe_content(frame);
		if (w != NULL) {
			if (GTK_IS_TEDITOR(w)) {
				editor_grab_focus(GTK_TEDITOR(w), false);
			}
		}
	}
}

static void setup_loading_session(const char *session_name) {
	const char *sessioncmd[] = { "teddy::session", "load", session_name };
	interp_eval_command(NULL, NULL, 3, sessioncmd);
}

int main(int argc, char *argv[]) {
	GtkWidget *window;

	gtk_init(&argc, &argv);

	foundry_init();


	global_init();
	config_init_auto_defaults();
	init_colors();

	buffer_wordcompl_init_charset();

	lexy_init();
	interp_init();

	read_conf();
	
	history_init(&command_history);
	history_init(&search_history);

	compl_init(&the_word_completer);

	cmdcompl_init();
	the_word_completer.recalc = &cmdcompl_recalc;
	the_word_completer.tmpdata = strdup("");
	alloc_assert(the_word_completer.tmpdata);

	jobs_init();
	buffers_init();

	{
		const char *loadhistory[] = { "teddy_intl::loadhistory" };
		interp_eval_command(NULL, NULL, 1, loadhistory);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), GIT_COMPILATION_DATE);
	gtk_window_set_default_size(GTK_WINDOW(window), 1024, 680);

	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_callback), NULL);
	g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	columnset = columns_new();
	iopen_init(window);

	tags_init();
	GtkWidget *top = top_init();

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(columnset), TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	if ((argc == 2) && (argv[1][0] == '@')) {
		setup_loading_session(argv[1]+1);
	} else {
		setup_initial_columns(argc, argv);
	}

	gtk_widget_show_all(window);

	gtk_main();

	buffers_free();
	interp_free();
	compl_free(&the_word_completer);
	foundry_free();

	history_free(&search_history);
	history_free(&command_history);

	return 0;
}
