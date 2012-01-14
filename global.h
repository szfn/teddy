#ifndef __ACMACS_GLOBAL_H__
#define __ACMACS_GLOBAL_H__

#include "editor.h"
#include "column.h"
#include "columns.h"
#include "buffer.h"
#include "history.h"

#include <gtk/gtk.h>
#include <ft2build.h>
#include FT_FREETYPE_H

extern GtkClipboard *selection_clipboard;
extern GtkClipboard *default_clipboard;

extern void quick_message(editor_t *editor, const char *title, const char *msg);

extern PangoFontDescription *elements_font_description;

extern buffer_t *selection_target_buffer;

extern columns_t *columnset;

extern GHashTable *keybindings;

#define MAX_LINES_HEIGHT_REQUEST 80
#define MIN_LINES_HEIGHT_REQUEST 3
#define MIN_EM_COLUMN_SIZE_ATTEMPTED 50

#define SPACEMAN_SAVE_RADIUS 150

extern int focus_can_follow_mouse;

extern history_t *search_history;
extern history_t *command_history;

void global_init();
char *unrealpath(char *absolute_path, const char *relative_path);
gboolean streq(gconstpointer a, gconstpointer b);

void set_color_cfg(cairo_t *cr, int color);
GtkWidget *frame_piece(gboolean horizontal);
void place_frame_piece(GtkWidget *table, gboolean horizontal, int positoin, int length);
void utf32_to_utf8(uint32_t code, char **r, int *cap, int *allocated);

#endif
