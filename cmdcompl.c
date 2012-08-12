#include "cmdcompl.h"

#include "baux.h"
#include "global.h"
#include "top.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

const char *list_internal_commands[] = {
	// tcl default commands (and redefined tcl default commands)
	"error", "lappend", "platform",
	"append",	"eval",	"lassign",	"platform::shell",
	"apply",	"exec",	"lindex",	"proc",
	"array",	"exit",	"linsert",	"puts",
	"auto_execok",	"expr",	"list",	"pwd",
	"auto_import",	"fblocked",	"llength",	"re_syntax",	"tcltest",
	"auto_load",	"fconfigure",	"load",	"read",	"tclvars",
	"auto_mkindex",	"fcopy",	"lrange",	"refchan",	"tell",
	"auto_mkindex_old",	"file",	"lrepeat",	"regexp",	"time",
	"auto_qualify",	"fileevent",	"lreplace",	"registry",	"tm",
	"auto_reset",	"filename",	"lreverse",	"regsub",	"trace",
	"bgerror",	"flush",	"lsearch",	"rename",	"unknown",
	"binary",	"for",	"lset",	"return",	"unload",
	"break",	"foreach",	"lsort",	"unset",
	"catch",	"format",	"mathfunc",	"scan",	"update",
	"gets",	"mathop",	"seek",	"uplevel",
	"chan",	"glob",	"memory",	"set",	"upvar",
	"clock",	"global",	"msgcat",	"socket",	"variable",
	"close",	"history",	"namespace",	"source",	"vwait",
	"concat",	"http",	"open",	"split",	"while",
	"continue",	"if",	"package",	"string",
	"dde",	"incr",	"parray",	"subst",
	"dict",	"info",	"pid",	"switch",
	"encoding",	"interp",	"pkg::create",	"Tcl",
	"eof",	"join",	"pkg_mkIndex",

	// special commands
	"setcfg", "bindkey", "new", "pwf", "pwd", "go", "mark", "cb", "save",
	"bufman", "undo", "search", "focuscmd", "move", "gohome", "bg", "<",
	"rgbcolor", "teddyhistory", "interactarg", "s", "c", "cursor", "bindent",
	"teddy-hack-resize", "kill", "refresh", "buffer", "load",

	// lexy
	"lexydef-create", "lexydef-append", "lexassoc", "lexycfg",

	// debug commands
	"wordcompl_dump", "lexy_dump"
};

void cmdcompl_init(struct clcompleter *c, struct history *h) {
	int i;
	char *path, *saveptr, *dir;

	compl_init(&(c->c));
	c->cbt.root = NULL;

	c->h = h;

	for (i = 0; i < sizeof(list_internal_commands) / sizeof(const char *); ++i) {
		compl_add(&(c->c), list_internal_commands[i]);
	}

	/*** Getting all executable names ***/

	asprintf(&path, "%s", getenv("PATH"));

	for (dir = strtok_r(path, ":", &saveptr); dir != NULL; dir = strtok_r(NULL, ":", &saveptr)) {
		DIR *dh = opendir(dir);
		if (dh == NULL) continue;
		struct dirent *den;
		for (den = readdir(dh); den != NULL; den = readdir(dh)) {
			struct stat den_stat;
			char *den_path;

			if (den->d_type == DT_REG) {
				den_path = malloc(sizeof(char) * (strlen(dir) + strlen(den->d_name) + 2));

				strcpy(den_path, dir);
				strcat(den_path, "/");
				strcat(den_path, den->d_name);

				memset(&den_stat, 0, sizeof(den_stat));

				if (stat(den_path, &den_stat) == 0) {

					if (S_ISREG(den_stat.st_mode) && (den_stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
						compl_add(&(c->c), den->d_name);
					}
				}

				free(den_path);
			}
		}
		closedir(dh);
	}

	free(path);

	c->absdir = strdup("");
	alloc_assert(c->absdir);
}

void cmdcompl_free(struct clcompleter *c) {
	compl_free(&(c->c));
	critbit0_clear(&(c->cbt));
}

static int cmdcompl_find_rightmost_slash(const char *text) {
	int rlidx;
	for (rlidx = strlen(text)-1; rlidx >= 0; --rlidx) {
		if (text[rlidx] == '/') break;
	}
	return rlidx;
}

static void load_directory_completions(struct clcompleter *c, const char *text, const char *working_directory) {
	char *reldir = NULL;
	char *absdir = NULL;

	int rlidx = cmdcompl_find_rightmost_slash(text);
	if (rlidx != -1) {
		reldir = malloc(sizeof(char) * (rlidx + 2));
		strncpy(reldir, text, rlidx+1);
		reldir[rlidx+1] = '\0';
	}

	if (reldir != NULL) {
		absdir = unrealpath((char *)working_directory, reldir);
	} else {
		reldir = malloc(sizeof(char));
		*reldir = '\0';
		absdir = malloc(sizeof(char) * (strlen(working_directory) + 1));
		strcpy(absdir, working_directory);
	}

	if (strcmp(absdir, c->absdir) == 0) {
		free(reldir);
		free(absdir);
		return;
	}

	free(c->absdir);
	c->absdir = absdir;
	critbit0_clear(&(c->cbt));

	DIR *dh = opendir(absdir);
	if (dh != NULL) {
		struct dirent *den;
		for (den = readdir(dh); den != NULL; den = readdir(dh)) {
			if (den->d_name[0] != '.') {
				char *relname = malloc(sizeof(char) * (strlen(reldir) + strlen(den->d_name) + 2));
				relname[0] = '\0';

				strcat(relname, reldir);
				strcat(relname, den->d_name);
				if (den->d_type == DT_DIR) {
					strcat(relname, "/");
				}

				critbit0_insert(&(c->cbt), relname);

				free(relname);
			}
		}
		closedir(dh);
	}

	free(reldir);
}

char *cmdcompl_complete(struct clcompleter *c, const char *text) {
	load_directory_completions(c, text, top_working_directory());

	char *rcmd = compl_complete(&(c->c), text);
	char *rdir = critbit0_common_suffix_for_prefix(&(c->cbt), text);

	utf8_remove_truncated_characters_at_end(rdir);

	if (rcmd == NULL) {
		return rdir;
	} else {
		if (rdir == NULL) {
			return rcmd;
		} else {
			free(rcmd);
			free(rdir);
			char *r = strdup("");
			alloc_assert(r);
			return r;
		}
	}
}

static int cmdcompl_wnd_fill_extra(const char *entry, void *p) {
	struct clcompleter *c = (struct clcompleter *)p;
	compl_add_to_list(&(c->c), entry);
	return 1;
}

void cmdcompl_wnd_show(struct clcompleter *c, const char *text, double x, double y, double alty, GtkWidget *parent) {
	char *working_directory = top_working_directory();
	compl_wnd_show(&(c->c), text, x, y, alty, parent, true, false);

	load_directory_completions(c, text, working_directory);

	critbit0_allprefixed(&(c->cbt), text, cmdcompl_wnd_fill_extra, (void *)c);

	if (c->c.size <= 0) {
		compl_wnd_hide(&(c->c));
	}
}

static void generic_cmdcompl_wnd_up(void *this) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->c))) {
		compl_wnd_up(&(c->c));
	} else if (compl_wnd_visible(&(c->h->c))) {
		compl_wnd_up(&(c->h->c));
	}
}

static void generic_cmdcompl_wnd_down(void *this) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->c))) {
		compl_wnd_down(&(c->c));
	} else if (compl_wnd_visible(&(c->h->c))) {
		compl_wnd_down(&(c->h->c));
	}
}

static char *generic_cmdcompl_wnd_get(void *this, bool all) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->c))) {
		return compl_wnd_get(&(c->c), all);
	} else if (compl_wnd_visible(&(c->h->c))) {
		return compl_wnd_get(&(c->h->c), all);
	}
	return NULL;
}

static bool generic_cmdcompl_wnd_visible(void *this) {
	struct clcompleter *c = (struct clcompleter *)this;
	return compl_wnd_visible(&(c->c)) || compl_wnd_visible(&(c->h->c));
}

static void generic_cmdcompl_wnd_hide(void *this) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->c))) {
		return compl_wnd_hide(&(c->c));
	} else if (compl_wnd_visible(&(c->h->c))) {
		return compl_wnd_hide(&(c->h->c));
	}
}

static char *generic_cmdcompl_common_suffix(void *this) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->c))) {
		return c->c.common_suffix;
	} else if (compl_wnd_visible(&(c->h->c))) {
		return c->h->c.common_suffix;
	}
	return "";
}

static char *generic_cmdcompl_complete(void *this, const char *text) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->h->c))) {
		return compl_complete(&(c->h->c), text);
	} else {
		return cmdcompl_complete(c, text);
	}
}

static void generic_cmdcompl_wnd_show(void *this, const char *text, double x, double y, double alty, GtkWidget *parent) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->h->c))) {
		compl_wnd_show(&(c->h->c), text, x, y, alty, parent, false, true);
	} else {
		cmdcompl_wnd_show(c, text, x, y, alty, parent);
	}
}

static uint16_t *generic_cmdcompl_prefix_from_buffer(void *this, buffer_t *buffer, size_t *prefix_len) {
	struct clcompleter *c = (struct clcompleter *)this;
	if (compl_wnd_visible(&(c->h->c))) {
		return buffer_historycompl_word_at_cursor(buffer, prefix_len);
	} else {
		return buffer_cmdcompl_word_at_cursor(buffer, prefix_len);
	}
}

void cmdcompl_as_generic_completer(struct clcompleter *c, generic_completer_t *gc) {
	gc->this = c;
	gc->complete = generic_cmdcompl_complete;
	gc->wnd_show = generic_cmdcompl_wnd_show;
	gc->wnd_up = generic_cmdcompl_wnd_up;
	gc->wnd_down = generic_cmdcompl_wnd_down;
	gc->wnd_get = generic_cmdcompl_wnd_get;
	gc->wnd_hide = generic_cmdcompl_wnd_hide;
	gc->wnd_visible = generic_cmdcompl_wnd_visible;
	gc->common_suffix = generic_cmdcompl_common_suffix;
	gc->prefix_from_buffer = generic_cmdcompl_prefix_from_buffer;
}
