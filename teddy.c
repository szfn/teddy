#include <stdio.h>
#include <stdlib.h>
#include <fontconfig/fontconfig.h>

#include "git.date.h"
#include "global.h"
#include "go.h"
#include "undo.h"
#include "buffers.h"
#include "columns.h"
#include "interp.h"
#include "jobs.h"
#include "colors.h"
#include "cfg.h"
#include "research.h"
#include "lexy.h"
#include "rd.h"
#include "baux.h"
#include "autoconf.h"
#include "foundry.h"

static gboolean delete_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
	//TODO: terminate all processes
	if (buffers_close_all(widget)) return FALSE;
	return TRUE;
}

void autoconf_maybe(void) {
	const char *home = getenv("HOME");
	char *name;

	asprintf(&name, "%s/%s", home, INITFILE);
	FILE *f = fopen(name, "r");

	if (f) {
		fclose(f);
		free(name);
		return;
	}

	f = fopen(name, "w");
	if (f) {
		fprintf(f, "%s\n", AUTOCONF_TEDDY);
		//printf("autoconf is: <%s>\n", AUTOCONF_TEDDY);
		fclose(f);
	}

	free(name);
}

int main(int argc, char *argv[]) {
	GtkWidget *window;

	autoconf_maybe();

	gtk_init(&argc, &argv);

	foundry_init();
	rd_init();
	global_init();
	cfg_init();
	init_colors();

	history_init(&command_history);
	history_init(&search_history);

	cmdcompl_init(&cmd_completer);
	buffer_wordcompl_init_charset();
	compl_init(&word_completer);

	lexy_init();
	interp_init();

	read_conf();

	jobs_init();
	buffers_init();

	{
		const char *loadhistory[] = { "loadhistory" };
		interp_eval_command(1, loadhistory);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), GIT_COMPILATION_DATE);
	gtk_window_set_default_size(GTK_WINDOW(window), 1024, 680);

	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_callback), NULL);
	g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	go_init(window);
	columnset = columns_new();
	research_init(window);

	gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(columnset));

	column_t *col1 = column_new(0);
	column_t *col2 = column_new(0);

	columns_add_after(columnset, NULL, col1);
	columns_add_after(columnset, col1, col2);

	heuristic_new_frame(columnset, NULL, null_buffer());
	heuristic_new_frame(columnset, NULL, null_buffer());

	for (int i = 1; i < argc; ++i) {
		buffer_t *buffer = buffer_create();
		load_text_file(buffer, argv[i]);
		heuristic_new_frame(columnset, NULL, buffer);
	}

	gtk_widget_show_all(window);

	gtk_main();

	buffers_free();
	interp_free();
	compl_free(&word_completer);
	cmdcompl_free(&cmd_completer);
	foundry_free();

	history_free(&search_history);
	history_free(&command_history);

	return 0;
}
