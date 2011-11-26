#ifndef __CFG__
#define __CFG__

#define CONFIG_ITEM_STRING_SIZE 512

typedef struct _config_item_t {
	char strval[CONFIG_ITEM_STRING_SIZE];
	int intval;
} config_item_t;

extern config_item_t config[];
extern const char *config_names[];

extern void cfg_init(void);
void setcfg(config_item_t *ci, const char *val);

#define CONFIG_NUM 15

#define CFG_BORDER_COLOR 0
#define CFG_DEFAULT_AUTOINDENT 1
#define CFG_DEFAULT_SPACEMAN 2
#define CFG_EDITOR_BG_COLOR 3
#define CFG_EDITOR_FG_COLOR 4
#define CFG_EDITOR_SEL_COLOR 5
#define CFG_FOCUS_FOLLOWS_MOUSE 6
#define CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE 7
#define CFG_MAIN_FONT 8
#define CFG_MAIN_FONT_HEIGHT_REDUCTION 9
#define CFG_POSBOX_BG_COLOR 10
#define CFG_POSBOX_BORDER_COLOR 11
#define CFG_POSBOX_FG_COLOR 12
#define CFG_POSBOX_FONT 13
#define CFG_WARP_MOUSE 14

#endif
