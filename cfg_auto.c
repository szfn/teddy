#include "cfg.h"
#include "cfg_auto.h"

#include <stdlib.h>

const char *config_names[] = {
	"main_font",
	"main_font_height_reduction",
	"posbox_font",
	"focus_follows_mouse",
	"autoindent",
	"spaceman",
	"warp_mouse",
	"interactive_search_case_sensitive",
	"autowrap",
	"tab_width",
	"editor_bg_color",
	"editor_fg_color",
	"editor_sel_color",
	"editor_bg_cursorline",
	"posbox_border_color",
	"posbox_bg_color",
	"posbox_fg_color",
	"border_color",
	"tag_bg_color",
	"tag_fg_color",
	"tag_font",
	"lexy_nothing",
	"lexy_keyword",
	"lexy_id",
	"lexy_comment",
	"lexy_string",
	"lexy_literal",
	"tags_discard_lineno",
	"autoreload",
};

void config_init_auto_defaults(void) {
	config_init(&global_config, NULL);
	config_set(&global_config, CFG_MAIN_FONT, "Arial-11");
	config_set(&global_config, CFG_MAIN_FONT_HEIGHT_REDUCTION, "0");
	config_set(&global_config, CFG_POSBOX_FONT, "Arial-8");
	config_set(&global_config, CFG_FOCUS_FOLLOWS_MOUSE, "1");
	config_set(&global_config, CFG_AUTOINDENT, "1");
	config_set(&global_config, CFG_SPACEMAN, "1");
	config_set(&global_config, CFG_WARP_MOUSE, "1");
	config_set(&global_config, CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE, "2");
	config_set(&global_config, CFG_AUTOWRAP, "1");
	config_set(&global_config, CFG_TAB_WIDTH, "4");
	config_set(&global_config, CFG_EDITOR_BG_COLOR, "16777215");
	config_set(&global_config, CFG_EDITOR_FG_COLOR, "0");
	config_set(&global_config, CFG_EDITOR_SEL_COLOR, "16777215");
	config_set(&global_config, CFG_EDITOR_BG_CURSORLINE, "13882323");
	config_set(&global_config, CFG_POSBOX_BORDER_COLOR, "0");
	config_set(&global_config, CFG_POSBOX_BG_COLOR, "15654274");
	config_set(&global_config, CFG_POSBOX_FG_COLOR, "0");
	config_set(&global_config, CFG_BORDER_COLOR, "0");
	config_set(&global_config, CFG_TAG_BG_COLOR, "16777215");
	config_set(&global_config, CFG_TAG_FG_COLOR, "0");
	config_set(&global_config, CFG_TAG_FONT, "Arial-11");
	config_set(&global_config, CFG_LEXY_NOTHING, "0");
	config_set(&global_config, CFG_LEXY_KEYWORD, "0");
	config_set(&global_config, CFG_LEXY_ID, "0");
	config_set(&global_config, CFG_LEXY_COMMENT, "0");
	config_set(&global_config, CFG_LEXY_STRING, "0");
	config_set(&global_config, CFG_LEXY_LITERAL, "0");
	config_set(&global_config, CFG_TAGS_DISCARD_LINENO, "1");
	config_set(&global_config, CFG_AUTORELOAD, "1");
}
