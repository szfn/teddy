#include <stdio.h>
#include <stdlib.h>
#include <fontconfig/fontconfig.h>

#include "global.h"
#include "go.h"
#include "undo.h"
#include "buffers.h"
#include "columns.h"
#include "interp.h"
#include "cmdcompl.h"
#include "jobs.h"
#include "colors.h"
#include "cfg.h"
#include "research.h"
#include "lexy.h"
#include "rd.h"
#include "baux.h"

static gboolean delete_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
	//TODO: terminate all processes
	if (buffers_close_all(widget)) return FALSE;
	return TRUE;
}

int main(int argc, char *argv[]) {
	GtkWidget *window;
	editor_t *editor;
	buffer_t *abuf = NULL;
	int i;

	gtk_init(&argc, &argv);

	FcInit();

	rd_init();
	global_init();
	cfg_init();
	init_colors();

	cmdcompl_init();

	buffer_wordcompl_init_charset();
	compl_init(&word_completer);

	lexy_init();
	interp_init();

	read_conf();

	teddy_font_real_init();

	jobs_init();
	buffers_init();

	enum go_file_failure_reason gffr;
	for (i = 1; i < argc; ++i) {
		buffer_t *buf;
		//printf("Will show: %s\n", argv[i]);
		buf = go_file(NULL, argv[i], true, &gffr);
		if (buf != NULL) {
			abuf = buf;
		}
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), "teddy");
	gtk_window_set_default_size(GTK_WINDOW(window), 1024, 680);

	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_callback), NULL);
	g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	go_init(window);
	columnset = the_columns_new(window);
	research_init(window);


	gtk_widget_show_all(window);

	editor = columns_new(columnset, abuf ? abuf : null_buffer());

	buffer_t *curdir_buf = go_file(NULL, getcwd(NULL, 0), true, &gffr);
	if (curdir_buf != NULL) {
		editor_t *curdir_ed = columns_new(columnset, curdir_buf);
		if (editor == NULL) editor = curdir_ed;
	}

	if (editor == NULL) editor = columns_new(columnset, null_buffer());

	editor_grab_focus(editor, false);

	gtk_main();

	buffers_free();
	columns_free(columnset);
	interp_free();
	cmdcompl_free();
	teddy_font_real_free();

	return 0;
}
