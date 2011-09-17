#ifndef __ACMACS_GLOBAL_H__
#define __ACMACS_GLOBAL_H__

#include "editor.h"
#include "column.h"
#include "buffer.h"
#include "history.h"

#include <gtk/gtk.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define CONFIG_ITEM_STRING_SIZE 512

typedef struct _config_item_t {
    char strval[CONFIG_ITEM_STRING_SIZE];
    int intval;
} config_item_t;

extern GtkClipboard *selection_clipboard;
extern GtkClipboard *default_clipboard;

extern FT_Library library;

extern void quick_message(editor_t *editor, const char *title, const char *msg);

extern PangoFontDescription *elements_font_description;

extern buffer_t *selection_target_buffer;

extern GHashTable *keybindings;

#define MAX_LINES_HEIGHT_REQUEST 80
#define MIN_LINES_HEIGHT_REQUEST 3
#define MIN_EM_COLUMN_SIZE_ATTEMPTED 50

extern config_item_t cfg_main_font;
extern config_item_t cfg_posbox_font;
extern config_item_t cfg_focus_follows_mouse;
extern config_item_t cfg_default_autoindent;

extern config_item_t cfg_editor_bg_color;
extern config_item_t cfg_editor_fg_color;
extern config_item_t cfg_editor_sel_color;
extern config_item_t cfg_posbox_border_color;
extern config_item_t cfg_posbox_bg_color;
extern config_item_t cfg_posbox_fg_color;

extern int focus_can_follow_mouse;

extern history_t *search_history;
extern history_t *command_history;

void global_init();
void setcfg(config_item_t *ci, const char *val);
char *unrealpath(char *absolute_path, const char *relative_path);
gboolean streq(gconstpointer a, gconstpointer b);

#endif
