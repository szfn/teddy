#include "global.h"

FT_Library library;

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
PangoFontDescription *elements_font_description;

buffer_t *selection_target_buffer = NULL;

GHashTable *keybindings;

void setcfg(config_item_t *ci, const char *val) {
    strcpy(ci->strval, val);
    ci->intval = atoi(val);
}

gboolean streq(gconstpointer a, gconstpointer b) {
    return (strcmp(a, b) == 0);
}

void global_init() {
    int error = FT_Init_FreeType(&library);
    if (error) {
        printf("Freetype initialization error\n");
        exit(EXIT_FAILURE);
    }
    
    selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    elements_font_description = pango_font_description_from_string("tahoma,arial,sans-serif 11");

    if (!FcInit()) {
        printf("Error initializing font config library\n");
        exit(EXIT_FAILURE);
    }

    setcfg(&cfg_main_font, "Arial-11");
    setcfg(&cfg_posbox_font, "Arial-9");

    keybindings = g_hash_table_new(g_str_hash, streq);
}

void global_free() {
    FcFini();
}
