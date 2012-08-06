#ifndef __ACMACS_GLOBAL_H__
#define __ACMACS_GLOBAL_H__

#include "editor.h"
#include "column.h"
#include "columns.h"
#include "buffer.h"
#include "history.h"
#include "compl.h"
#include "cmdcompl.h"

#include <gtk/gtk.h>
#include <ft2build.h>
#include FT_FREETYPE_H

extern GtkClipboard *selection_clipboard;
extern GtkClipboard *default_clipboard;

extern void quick_message(const char *title, const char *msg);

extern PangoFontDescription *elements_font_description;

extern buffer_t *selection_target_buffer;

extern columns_t *columnset;

extern GHashTable *keybindings;

#define MAX_LINES_HEIGHT_REQUEST 80
#define MIN_LINES_HEIGHT_REQUEST 0
#define MIN_EM_COLUMN_SIZE_ATTEMPTED 50

#define SPACEMAN_SAVE_RADIUS 150

#define INITFILE ".teddy"

extern int focus_can_follow_mouse;

extern struct history search_history;
extern struct history command_history;

extern struct completer word_completer;
extern struct clcompleter cmd_completer;

void global_init();

bool find_editor_for_buffer(buffer_t *buffer, column_t **columnpp, tframe_t **framepp, editor_t **editorpp);

char *unrealpath(char *absolute_path, const char *relative_path);
gboolean streq(gconstpointer a, gconstpointer b);

void set_color_cfg(cairo_t *cr, int color);
GtkWidget *frame_piece(gboolean horizontal);
void place_frame_piece(GtkWidget *table, gboolean horizontal, int positoin, int length);
bool inside_allocation(double x, double y, GtkAllocation *allocation);

void utf32_to_utf8(uint32_t code, char **r, int *cap, int *allocated);
char *string_utf16_to_utf8(uint16_t *origin, size_t origin_len);
uint32_t utf8_to_utf32(const char *text, int *src, int len, bool *valid);
void utf8_remove_truncated_characters_at_end(char *text);

void alloc_assert(void *p);

#endif
