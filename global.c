#include "global.h"

FT_Library library;

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
PangoFontDescription *elements_font_description;

buffer_t *selection_target_buffer = NULL;

GHashTable *keybindings;

config_item_t cfg_main_font;
config_item_t cfg_posbox_font;
config_item_t cfg_focus_follows_mouse;
config_item_t cfg_default_autoindent;

config_item_t cfg_editor_bg_color;
config_item_t cfg_editor_fg_color;
config_item_t cfg_posbox_border_color;
config_item_t cfg_posbox_bg_color;
config_item_t cfg_posbox_fg_color;


history_t *search_history;
history_t *command_history;

int focus_can_follow_mouse = 1;

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
    setcfg(&cfg_posbox_font, "Arial-8");
    setcfg(&cfg_focus_follows_mouse, "1");

    setcfg(&cfg_default_autoindent, "1");
    
    setcfg(&cfg_editor_bg_color, "255");
    setcfg(&cfg_editor_fg_color, "16777215"); // white
    setcfg(&cfg_posbox_border_color, "0");
    setcfg(&cfg_posbox_bg_color, "15654274");
    setcfg(&cfg_posbox_fg_color, "0");

    keybindings = g_hash_table_new(g_str_hash, streq);

    search_history = history_new();
    command_history = history_new();
}

void global_free() {
    FcFini();
}

char *unrealpath(char *absolute_path, const char *relative_path) {
    if (strlen(relative_path) == 0) goto return_relative_path;
    if (relative_path[0] == '/') goto return_relative_path;

    if (relative_path[0] == '~') {
        const char *home = getenv("HOME");
        char *r;
        
        if (home == NULL) goto return_relative_path;

        r = malloc(sizeof(char) * (strlen(relative_path) + strlen(home) + 1));
        strcpy(r, home);
        strcpy(r + strlen(r), relative_path+1);
        return r;
    } else {
        if (absolute_path == NULL) {
            char *cwd = get_current_dir_name();
            char *r = malloc(sizeof(char) * (strlen(relative_path) + strlen(cwd) + 2));

            strcpy(r, cwd);
            r[strlen(r)] = '/';
            strcpy(r + strlen(cwd) + 1, relative_path);

            free(cwd);
            return r;
        } else {
            char *end = strrchr(absolute_path, '/');
            char *r = malloc(sizeof(char) * (strlen(relative_path) + (end - absolute_path) + 2));

            strncpy(r, absolute_path, end-absolute_path+1);
            strcpy(r+(end-absolute_path+1), relative_path);

            return r;
        }
    }

    return NULL;
    
    return_relative_path: {
        char *r = malloc(sizeof(char) * (strlen(relative_path)+1));
        strcpy(r, relative_path);
        return r;
    }
}
