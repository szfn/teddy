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

const char **external_commands = NULL;
int external_commands_cap;
int external_commands_allocated;

void cmdcompl_init(void) {
	char *path, *saveptr, *dir;

	external_commands_allocated = 10;
	external_commands_cap = 0;
	external_commands = malloc(sizeof(const char *) * external_commands_allocated);
	alloc_assert(external_commands);

	/*** Getting all executable names ***/

	asprintf(&path, "%s", getenv("PATH"));

	for (dir = strtok_r(path, ":", &saveptr); dir != NULL; dir = strtok_r(NULL, ":", &saveptr)) {
		DIR *dh = opendir(dir);
		if (dh == NULL) continue;
		struct dirent *den;
		for (den = readdir(dh); den != NULL; den = readdir(dh)) {
			struct stat den_stat;

			if (den->d_type == DT_REG) {
				char *den_path;
				asprintf(&den_path, "%s/%s", dir, den->d_name);
				alloc_assert(den_path);

				memset(&den_stat, 0, sizeof(den_stat));
				if (stat(den_path, &den_stat) == 0) {
					if (S_ISREG(den_stat.st_mode) && (den_stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
						if (external_commands_cap >= external_commands_allocated) {
							external_commands_allocated *= 2;
							external_commands = realloc(external_commands, sizeof(const char *) * external_commands_allocated);
							alloc_assert(external_commands);
						}
						char *r = strdup(den->d_name);
						alloc_assert(r);
						external_commands[external_commands_cap++] = r;
					}
				}

				free(den_path);
			}
		}
		closedir(dh);
	}

	free(path);
}

static int cmdcompl_find_rightmost_slash(const char *text) {
	int rlidx;
	for (rlidx = strlen(text)-1; rlidx >= 0; --rlidx) {
		if (text[rlidx] == '/') break;
	}
	return rlidx;
}

static void load_directory_completions(struct completer *c, const char *absdir, const char *reldir) {
	DIR *dh = opendir(absdir);
	if (dh != NULL) {
		struct dirent *den;
		for (den = readdir(dh); den != NULL; den = readdir(dh)) {
			if (den->d_name[0] != '.') {
				char *relname;
				asprintf(&relname, "%s%s%s", reldir, den->d_name, (den->d_type == DT_DIR) ? "/" : "");
				alloc_assert(relname);
				critbit0_insert(&(c->cbt), relname);
				free(relname);
			}
		}
		closedir(dh);
	}
}

void cmdcompl_recalc(struct completer *c, const char *prefix) {
	char *working_directory = top_working_directory();
	char *reldir = NULL;
	char *absdir = NULL;

	int rlidx = cmdcompl_find_rightmost_slash(prefix);
	if (rlidx != -1) {
		reldir = malloc(sizeof(char) * (rlidx + 2));
		strncpy(reldir, prefix, rlidx+1);
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

	if (strcmp(absdir, c->tmpdata) == 0) {
		// same directory as before, do nothing
		free(reldir);
		free(absdir);
		return;
	}

	// we actually need to recalculate
	free(c->tmpdata);
	c->tmpdata = absdir;

	critbit0_clear(&(c->cbt));

	for (int i = 0; i < sizeof(list_internal_commands) / sizeof(const char *); ++i) {
		compl_add(c, list_internal_commands[i]);
	}

	for (int i = 0; i < external_commands_cap; ++i) {
		compl_add(c, external_commands[i]);
	}

	load_directory_completions(c, absdir, reldir);

	free(reldir);
}

bool in_external_commands(const char *arg) {
	for (int i = 0; i < external_commands_cap; ++i) {
		if (strcmp(arg, external_commands[i]) == 0) return true;
	}

	return access(arg, F_OK) == 0;
}
