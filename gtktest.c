#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "global.h"
#include "buffers.h"
#include "buffer.h"
#include "editor.h"

FT_Library library;

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
editor_t *editor;

int main(int argc, char *argv[]) {
    GtkWidget *window;
    int error;
    buffer_t *buffer;

    gtk_init(&argc, &argv);

    error = FT_Init_FreeType(&library);
    if (error) {
        printf("Freetype initialization error\n");
        exit(EXIT_FAILURE);
    }

    if (argc <= 1) {
        printf("Nothing to show\n");
        exit(EXIT_SUCCESS);
    }
    
    selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    buffers_init();

    printf("Will show: %s\n", argv[1]);

    buffer = buffer_create(&library);

    if (load_text_file(buffer, argv[1]) != 0) {
        perror("Load failed\n");
        exit(EXIT_FAILURE);
    }

    buffers_add(buffer);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "Acmacs");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 600);

    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    editor = new_editor(window, buffer);

    gtk_container_add(GTK_CONTAINER(window), editor->table);

    gtk_widget_show_all(window);

    gtk_widget_grab_focus(GTK_WIDGET(editor->drar));
    editor_post_show_setup(editor);

    gtk_main();

    editor_free(editor);

    buffers_free();

    return 0;
}
