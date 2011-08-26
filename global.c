#include "global.h"

FT_Library library;

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
PangoFontDescription *elements_font_description;

void global_init() {
    int error = FT_Init_FreeType(&library);
    if (error) {
        printf("Freetype initialization error\n");
        exit(EXIT_FAILURE);
    }
    
    selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    /*
    elements_font_description = pango_font_description_new();
    pango_font_description_set_family(elements_font_description, "sans-serif");
    pango_font_description_set_style(elements_font_description, PANGO_STYLE_NORMAL);
    pango_font_description_set_variant(elements_font_description, PANGO_VARIANT_NORMAL);
    pango_font_description_set_weight(elements_font_description, PANGO_WEIGHT_THIN);
    pango_font_description_set_size(elements_font_description, 10);*/
    elements_font_description = pango_font_description_from_string("tahoma,arial,sans-serif 9");
    
}
