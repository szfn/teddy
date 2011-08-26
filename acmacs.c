#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "undo.h"
#include "buffers.h"
#include "columns.h"
#include "interp.h"

static gboolean delete_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if (buffers_close_all(widget)) return FALSE;
    return TRUE;
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    editor_t *editor;
    int i;

    gtk_init(&argc, &argv);

    global_init();

    interp_init();
    buffers_init();

    for (i = 1; i < argc; ++i) {
        char *rp;
        printf("Will show: %s\n", argv[i]);
        if (buffers_open(NULL, argv[i], &rp) == NULL) {
            printf("Load of [%s] failed\n", (rp == NULL) ? argv[i] : rp);
            free(rp);
        }
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "Acmacs");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 600);

    g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_callback), NULL);
    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    columns_init(window);
    editor = columns_new(buffers_get_replacement_buffer(NULL));

    gtk_widget_show_all(window);

    gtk_widget_grab_focus(GTK_WIDGET(editor->drar));

    gtk_main();

    buffers_free();
    columns_free();
    interp_free();
    
    return 0;
}
