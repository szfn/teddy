#include "global.h"

FT_Library library;

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;

void global_init() {
    int error = FT_Init_FreeType(&library);
    if (error) {
        printf("Freetype initialization error\n");
        exit(EXIT_FAILURE);
    }
    
    selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
}
