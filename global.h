#ifndef __ACMACS_GLOBAL_H__
#define __ACMACS_GLOBAL_H__

#include "editor.h"
#include "column.h"

#include <gtk/gtk.h>
#include <ft2build.h>
#include FT_FREETYPE_H

extern GtkClipboard *selection_clipboard;
extern GtkClipboard *default_clipboard;

extern FT_Library library;

extern void quick_message(editor_t *editor, const char *title, const char *msg);

extern PangoFontDescription *elements_font_description;

#define MAX_LINES_HEIGHT_REQUEST 25
#define MIN_LINES_HEIGHT_REQUEST 3

void global_init();

#endif
