#include "cfg.h"

#include <string.h>
#include <stdlib.h>

config_item_t config[14];
const char *config_names[] = {
	"border_color",
	"default_autoindent",
	"default_spaceman",
	"editor_bg_color",
	"editor_fg_color",
	"editor_sel_color",
	"focus_follows_mouse",
	"interactive_search_case_sensitive",
	"main_font",
	"posbox_bg_color",
	"posbox_border_color",
	"posbox_fg_color",
	"posbox_font",
	"warp_mouse",
};

void setcfg(config_item_t *ci, const char *val) {
    strcpy(ci->strval, val);
    ci->intval = atoi(val);
}

void cfg_init(void) {
	setcfg(config + CFG_BORDER_COLOR, "0");
	setcfg(config + CFG_DEFAULT_AUTOINDENT, "1");
	setcfg(config + CFG_DEFAULT_SPACEMAN, "1");
	setcfg(config + CFG_EDITOR_BG_COLOR, "255");
	setcfg(config + CFG_EDITOR_FG_COLOR, "16777215");
	setcfg(config + CFG_EDITOR_SEL_COLOR, "16777215");
	setcfg(config + CFG_FOCUS_FOLLOWS_MOUSE, "1");
	setcfg(config + CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE, "2");
	setcfg(config + CFG_MAIN_FONT, "Arial-11");
	setcfg(config + CFG_POSBOX_BG_COLOR, "15654274");
	setcfg(config + CFG_POSBOX_BORDER_COLOR, "0");
	setcfg(config + CFG_POSBOX_FG_COLOR, "0");
	setcfg(config + CFG_POSBOX_FONT, "Arial-8");
	setcfg(config + CFG_WARP_MOUSE, "1");
}
