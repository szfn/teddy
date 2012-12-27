#include "tags.h"

#include <stdlib.h>

#include "buffers.h"
#include "global.h"

critbit0_tree tags_file_critbit;

struct tag_entry *tag_entries;
int allocated;
int tag_entries_cap;

void tags_init(void) {
	allocated = 10;
	tag_entries = malloc(allocated * sizeof(struct tag_entry));
	alloc_assert(tag_entries);
	for (int i = 0; i < allocated; ++i) tag_entries[i].tag = NULL;
	tag_entries_cap = 0;
	tags_file_critbit.root = NULL;
}

static void tags_grow(void) {
	tag_entries = realloc(tag_entries, 2 * allocated * sizeof(struct tag_entry));
	alloc_assert(tag_entries);
	for (int i = allocated; i < 2*allocated; ++i) tag_entries[i].tag = NULL;
	allocated *= 2;
}

static void tags_free(void) {
	for (int i = 0; i < tag_entries_cap; ++i) {
		if (tag_entries[i].tag != NULL) free(tag_entries[i].tag);
		if (tag_entries[i].path != NULL) free(tag_entries[i].path);
		if (tag_entries[i].search != NULL) free(tag_entries[i].search);
	}

	tag_entries_cap = 0;
}

void tags_load(char *wd) {
#define BUF_SIZE 1024
	char buf[BUF_SIZE];

	tags_free();

	critbit0_clear(&tags_file_critbit);

	char *tags_file;
	asprintf(&tags_file, "%s/%s", wd, "tags");
	alloc_assert(tags_file);

	FILE *f = fopen(tags_file, "r");

	if (f == NULL) {
		buffers_register_tags(NULL);
		free(tags_file);
		return;
	} else {
		buffers_register_tags(tags_file);
	}

	free(tags_file);

	while (fgets(buf, BUF_SIZE, f) != NULL) {
		if (buf[strlen(buf)-1] == '\n') buf[strlen(buf)-1] = '\0';
		if (strcmp(buf, "") == 0) continue;
		if (buf[0] == '!') continue;
		if (strlen(buf) > 512) continue;

		char *toks;
		char *tag = strtok_r(buf, "\t", &toks);
		if (tag == NULL) continue;
		char *path = strtok_r(NULL, "\t", &toks);
		if (path == NULL) continue;
		char *search = strtok_r(NULL, "", &toks);
		if (search == NULL) continue;

		critbit0_insert(&tags_file_critbit, tag);

		char *d = strstr(search, ";\"\t");
		if (d != NULL) *d = '\0';

		if (tag_entries_cap >= allocated) tags_grow();
		if (tag_entries_cap >= allocated) break;

		tag_entries[tag_entries_cap].tag = strdup(tag);
		alloc_assert(tag_entries[tag_entries_cap].tag);
		tag_entries[tag_entries_cap].path = strdup(path);
		alloc_assert(tag_entries[tag_entries_cap].path);

		bool is_search = false;

		if (search[0] == '/') {
			is_search = true;
			search++;
			search[strlen(search)-1] = '\0';
		} else {
			is_search = false;
		}

		if (search[0] == '^') {
			search++;
		}

		if (search[strlen(search)-1] == '$') {
			search[strlen(search)-1] = '\0';
		}

		if (is_search) {
			// replace \/ with /
			int src = 0, dst = 0;
			while (src < strlen(search)) {
				if ((src+1 < strlen(search)) && (search[src] == '\\')) {
					++src;
				} else {
					search[dst] = search[src];
					++src; ++dst;
				}
			}
			search[dst] = '\0';
		}

		if (is_search) {
			tag_entries[tag_entries_cap].search = strdup(search);
			alloc_assert(tag_entries[tag_entries_cap].search);
		} else {
			tag_entries[tag_entries_cap].search = NULL;
			tag_entries[tag_entries_cap].lineno = atoi(search);
		}

		//printf("search: <%s>\n", tag_entries[tag_entries_cap].search);

		if (config_intval(&global_config, CFG_TAGS_DISCARD_LINENO) != 0) {
			if (tag_entries[tag_entries_cap].search == NULL) {
				free(tag_entries[tag_entries_cap].tag);
				free(tag_entries[tag_entries_cap].path);
				//printf("\tdiscarded\n");
				--tag_entries_cap;
			}
		}

		++tag_entries_cap;
	}

	//printf("loaded: %d\n", tag_entries_cap);
	fclose(f);

	word_completer_full_update();
}

bool tags_loaded(void) {
	return tag_entries_cap > 0;
}

int teddy_tags_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'tags' command");
		return TCL_ERROR;
	}

	if (tag_entries_cap == 0) {
		Tcl_AddErrorInfo(interp, "No tags loaded");
		return TCL_ERROR;
	}

	Tcl_Obj *retlist = Tcl_NewListObj(0, NULL);
	Tcl_IncrRefCount(retlist);

	for (int i = 0; i < tag_entries_cap; ++i) {
		if (strcmp(tag_entries[i].tag, argv[1]) == 0) {
			if (i > 0) {
				// skip identical entries
				if ((strcmp(tag_entries[i].path, tag_entries[i-1].path) == 0) &&
					(strcmp(tag_entries[i].search, tag_entries[i-1].search) == 0)) {
					continue;
				}
			}

			char *text;
			asprintf(&text, "%s\t/%s/", tag_entries[i].path, tag_entries[i].search!=NULL ? tag_entries[i].search : "");
			alloc_assert(text);

			Tcl_Obj *text_obj = Tcl_NewStringObj(text, strlen(text));
			Tcl_ListObjAppendElement(interp, retlist, text_obj);

			free(text);
		}
	}

	Tcl_SetObjResult(interp, retlist);
	Tcl_DecrRefCount(retlist);

	return TCL_OK;
}