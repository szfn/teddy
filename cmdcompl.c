#include "cmdcompl.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMPLETION_REQUEST_LENGTH 256
#define MAX_NUMBER_OF_COMPLETIONS 128

const char *found_completions[MAX_NUMBER_OF_COMPLETIONS];
int num_found_completions;
int found_completions_is_incomplete;

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
    "setcfg", "bindkey", "new", "pwf", "go", "mark", "cb", "save",
    "bufman", "undo", "search", "focuscmd", "move", "gohome",
};

char **list_external_commands;
int external_commands_allocated;
int external_commands_cap;

static int qsort_strcmp_wrap(const void *a, const void *b) {
    const char **sa = (const char **)a;
    const char **sb = (const char **)b;

    return strcmp(*sa, *sb);
}

void cmdcompl_rehash(void) {
    char *path, *saveptr, *dir;

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
                        char *d_name_copy;
                        asprintf(&d_name_copy, "%s", den->d_name);
                        if (external_commands_cap >= external_commands_allocated) {
                            external_commands_allocated *= 2;
                            list_external_commands = realloc(list_external_commands, sizeof(char *) * external_commands_allocated);
                            if (list_external_commands == NULL) {
                                perror("Out of memory");
                                exit(EXIT_FAILURE);
                            }
                        }
                        list_external_commands[external_commands_cap++] = d_name_copy;
                    }
                }
                
                free(den_path);
            }
        }
        closedir(dh);
    }

    free(path);

    /*** Alphabetical sorting ***/

    qsort(list_external_commands, external_commands_cap, sizeof(char *), qsort_strcmp_wrap);

    /*** Removing duplicates ***/

    {
        int src, dst;
        char *last = NULL;
        
        for(src = dst = 0; src < external_commands_cap; ++src) {
            if ((last != NULL) && (strcmp(list_external_commands[src], last) == 0)) {
                //printf("Duplicate %s\n", list_external_commands[src]);
                free(list_external_commands[src]);
            } else {
                last = list_external_commands[src];
                list_external_commands[dst] = list_external_commands[src];
                ++dst;
            }
        }

        //printf("Deduplication %d -> %d\n", external_commands_cap, dst);

        external_commands_cap = dst;
    }

    /*** Alphabetical sorting for internal commands ***/

    qsort(list_internal_commands, sizeof(list_internal_commands) / sizeof(const char *), sizeof(const char *), qsort_strcmp_wrap);
}

void cmdcompl_init(void) {
    external_commands_cap = 0;
    external_commands_allocated = 10;
    list_external_commands = malloc(external_commands_allocated * sizeof(char *));
    if (list_external_commands == NULL) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }

    num_found_completions = 0;
    found_completions_is_incomplete = 0;

    cmdcompl_rehash();
}

void cmdcompl_free(void) {
    int i;
    for (i = 0; i < external_commands_cap; ++i) {
        free(list_external_commands[i]);
    }
    free(list_external_commands);
}

static void cmdcompl_add_matches(const char **list, int listlen, const char *text, int textlen) {
    int i;
    for (i = 0; i < listlen; ++i) {
        if (strncmp(list[i], text, textlen) == 0) {
            if (num_found_completions >= MAX_NUMBER_OF_COMPLETIONS) {
                found_completions_is_incomplete = 1;
            }
            found_completions[num_found_completions++] = list[i];
        }
    }
}

static void cmdcompl_reset(void) {
    num_found_completions = 0;
    found_completions_is_incomplete = 0;

    // TODO: clean up everything that was allocated from previous completions
}

static void cmdcompl_start(const char *text, int length) {
    cmdcompl_reset();

    if (length <= 0) return;

    cmdcompl_add_matches(list_internal_commands, sizeof(list_internal_commands) / sizeof(const char *), text, length);
    if (num_found_completions < MAX_NUMBER_OF_COMPLETIONS) {
        cmdcompl_add_matches((const char **)list_external_commands, external_commands_cap, text, length);
    }
}

void cmdcompl_complete(const char *text, int length) {
    if (length > MAX_COMPLETION_REQUEST_LENGTH) {
        cmdcompl_reset();
        return;
    }

    //TODO: if the last completion request was a prefix of this completion request
    // - and the completion directory is the same
    // - and last completion set was complete
    // then just filter the partial results
    cmdcompl_start(text, length);
}
