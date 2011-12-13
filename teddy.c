#include <stdio.h>
#include <stdlib.h>

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
#include "wordcompl.h"
#include "lexy.h"

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

	global_init();
	cfg_init();
	init_colors();

	cmdcompl_init();
	interp_init();
	lexy_init();

	read_conf();

	jobs_init();
	buffers_init();
	wordcompl_init();

	for (i = 1; i < argc; ++i) {
		char *rp;
		buffer_t *buf;
		//printf("Will show: %s\n", argv[i]);
		buf = buffers_open(NULL, argv[i], &rp);
		if (buf == NULL) {
			fprintf(stderr, "Load of [%s] failed\n", (rp == NULL) ? argv[i] : rp);
		} else {
			abuf = buf;
		}
		free(rp);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), "teddy");
	gtk_window_set_default_size(GTK_WINDOW(window), 1024, 680);

	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_callback), NULL);
	g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	go_init(window);
	columns_init(window);
	research_init(window);
	editor = columns_new((abuf == NULL) ? null_buffer() : abuf);

	gtk_widget_show_all(window);

	editor_grab_focus(editor);

	gtk_main();

	buffers_free();
	columns_free();
	interp_free();
	cmdcompl_free();

	return 0;
}
