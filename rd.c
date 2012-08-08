#include "rd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <stdbool.h>

#include "global.h"
#include "baux.h"
#include "lexy.h"
#include "go.h"
#include "editor_cmdline.h"

struct entry {
	char *name;
	ino_t inode;
	unsigned char type;
};

struct entries {
	struct entry *v;
	size_t cur;
	size_t allocated;
};

static void entry_init(struct entries *es, size_t initial_allocation) {
	es->allocated = initial_allocation;
	es->v = malloc(sizeof(struct entry) * initial_allocation);
	alloc_assert(es->v);
	es->cur = 0;
}

static void entry_free(struct entries *es) {
	for (int i = 0; i < es->cur; ++i) {
		free(es->v[i].name);
	}
	free(es->v);
}

static void entry_append(struct entries *es, const char *name, ino_t inode, unsigned char d_type) {
	if (es->cur >= es->allocated) {
		es->allocated *= 2;
		es->v = realloc(es->v, sizeof(struct entry) * es->allocated);
		alloc_assert(es->v);
	}

	es->v[es->cur].name = strdup(name);
	alloc_assert(es->v[es->cur].name);
	es->v[es->cur].inode = inode;
	es->v[es->cur].type = d_type;
	++(es->cur);
}

unsigned char order_by_d_type[0xff+1];

static int entry_cmp(const void *pa, const void *pb) {
	struct entry *a = (struct entry *)pa;
	struct entry *b = (struct entry *)pb;

	int d = order_by_d_type[a->type] - order_by_d_type[b->type];
	if (d != 0) return d;

	return strcmp(a->name, b->name);
}

static void entry_sort(struct entries *es) {
	qsort(es->v, es->cur, sizeof(struct entry), entry_cmp);
}

char type_identifier[0xff+1];

static void rdrs(buffer_t *buffer, const char *c, uint8_t color) {
	buffer->default_color = color;
	buffer_replace_selection(buffer, c);
	buffer->default_color = 0;
}

static void rdrr(buffer_t *buffer, const char *c, uint8_t color, lpoint_t *start, lpoint_t *end) {
	buffer->default_color = color;
	buffer_replace_region(buffer, c, start, end);
	buffer->default_color = 0;
}

static void rd_insert(buffer_t *buffer, DIR *dir, int depth) {
	struct entries es;
	entry_init(&es, 100);

	struct dirent entry;
	struct dirent *result;

	for (;;) {
		int r = readdir_r(dir, &entry, &result);
		if (r != 0) {
			rdrs(buffer, "Error reading directory\n", CFG_LEXY_COMMENT - CFG_LEXY_NOTHING);
			break;
		}
		if (result == NULL) {
			// end of stream
			break;
		}
		if (entry.d_name[0] == '.') continue;

		entry_append(&es, entry.d_name, entry.d_ino, entry.d_type);
	}

	entry_sort(&es);

	for (int i = 0; i < es.cur; ++i) {
		for (int d = 0; d < depth-1; ++d) {
			//rdrs(buffer, "\u2502\ue652", L_NOTHING);
			rdrs(buffer, "\u2502", 0);
		}

		if (depth > 0) {
			if (i == es.cur-1) {
				rdrs(buffer, "\u2515", 0);
			} else {
				rdrs(buffer, "\u251d", 0);
			}
		}

		rdrs(buffer, (es.v[i].type == DT_DIR) ? "\ue650" : "\ue652", CFG_LEXY_STRING - CFG_LEXY_NOTHING);
		rdrs(buffer, es.v[i].name, (es.v[i].type == DT_DIR) ? (CFG_LEXY_STRING-CFG_LEXY_NOTHING) : 0);

		char id = type_identifier[es.v[i].type];
		if (id != ' ') {
			char z[] = { 0, 0 };
			z[0] = id;
			rdrs(buffer, z, CFG_LEXY_COMMENT - CFG_LEXY_NOTHING);
		}

		if (i != es.cur-1) rdrs(buffer, "\n", 0);
	}

	entry_free(&es);
}

void rd_init(void) {
	for (int i = 0; i <= 0xff; ++i) {
		order_by_d_type[i] = 0xff;
		type_identifier[i] = ' ';
	}

	order_by_d_type[DT_BLK] = 50; type_identifier[DT_BLK] = '%';
	order_by_d_type[DT_CHR] = 60; type_identifier[DT_CHR] = '^';
	order_by_d_type[DT_DIR] = 10; type_identifier[DT_DIR] = '/';
	order_by_d_type[DT_FIFO] = 30; type_identifier[DT_FIFO] = '|';
	order_by_d_type[DT_LNK] = 20; type_identifier[DT_LNK] = '@';
	order_by_d_type[DT_REG] = 20; type_identifier[DT_REG] = ' ';
	order_by_d_type[DT_SOCK] = 40; type_identifier[DT_SOCK] = '=';
	order_by_d_type[DT_UNKNOWN] = 70; type_identifier[DT_UNKNOWN] = '?';
}

void rd(DIR *dir, buffer_t *buffer) {
	rdrs(buffer, buffer->path, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
	rdrs(buffer, ":\n", CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);

	rd_insert(buffer, dir, 0);
	rdrs(buffer, "\n", 0);
	buffer->cursor.line = buffer->real_line;
	buffer->cursor.glyph = 0;

	//rdrs(buffer, "\n", L_NOTHING);

	buffer->editable = 0;
}

static int find_start(real_line_t *line) {
	int i;
	for (i = line->cap-1; i >= 0; --i) {
		if ((line->glyph_info[i].code == 0xe650) || (line->glyph_info[i].code == 0xe651) || (line->glyph_info[i].code == 0xe652)) {
			break;
		}
	}

	return i;
}

char *rd_get_path(buffer_t *buffer, real_line_t *cursor_line, int i) {
	lpoint_t start, end;
	start.line = end.line = cursor_line;

	start.glyph = i+1;
	end.glyph = cursor_line->cap;

	char *text = buffer_lines_to_text(buffer, &start, &end);

	if (i == 0) {

		char *path = malloc(sizeof(char) * (1+strlen(buffer->path) + strlen(text)));

		strcpy(path, buffer->path);
		strcat(path, text);

		free(text);

		return path;
	} else {
		real_line_t *directory_line;
		for (directory_line = cursor_line->prev; directory_line != NULL; directory_line = directory_line->prev) {
			if (i-1 >= directory_line->cap) continue;
			if (directory_line->glyph_info[i-1].code == 0xe651) {
				char *directory_path = rd_get_path(buffer, directory_line, i-1);

				char *path = malloc(sizeof(char) * (1 + strlen(directory_path) + strlen(text)));

				strcpy(path, directory_path);
				strcat(path, text);

				free(directory_path);
				free(text);

				return path;
			}
		}
	}

	free(text);
	return NULL;
}

void rd_open(editor_t *editor) {
	if (editor->buffer == NULL) return;

	real_line_t *cursor_line = editor->buffer->cursor.line;
	if (cursor_line == NULL) return;

	int i = find_start(cursor_line);

	if (i >= cursor_line->cap) return;

	if (cursor_line->glyph_info[i].code == 0xe650) {
		lpoint_t start, end;
		start.line = end.line = cursor_line;
		start.glyph = i;
		end.glyph = i+1;

		editor->buffer->editable = 1;

		rdrr(editor->buffer, "\ue651", CFG_LEXY_STRING - CFG_LEXY_NOTHING, &start, &end);

		char *path = rd_get_path(editor->buffer, cursor_line, i);

		if (path == NULL) return;

		//printf("Expanding: <%s> <%d>\n", path, i+1);

		DIR *dir = opendir(path);
		free(path);

		if (dir != NULL) {
			editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
			rdrs(editor->buffer, "\n", 0);
			rd_insert(editor->buffer, dir, i+1);
			closedir(dir);
		}

		editor->buffer->editable = 0;
		editor->buffer->modified = 0;

		editor->buffer->cursor = start;
	} else if (cursor_line->glyph_info[i].code == 0xe651) {
		lpoint_t start, end;

		start.line = end.line = cursor_line;
		start.glyph = i;
		end.glyph = i+1;

		editor->buffer->editable = 1;

		rdrr(editor->buffer, "\ue650", CFG_LEXY_STRING-CFG_LEXY_NOTHING, &start, &end);

		start.glyph = end.glyph = 0;
		start.line = cursor_line->next;

		if (start.line != NULL) {
			for (end.line = start.line->next; end.line != NULL; end.line = end.line->next) {
				if (i >= end.line->cap) continue;
				int c = end.line->glyph_info[i].code;
				if ((c == 0xe650) || (c == 0xe651) || (c == 0xe652)) break;
			}

			if (end.line != NULL) {
				buffer_replace_region(editor->buffer, "", &start, &end);
			}
		}


		editor->buffer->editable = 0;
		editor->buffer->modified = 0;

		editor->buffer->cursor.line = cursor_line; editor->buffer->cursor.glyph = i;
		editor->buffer->mark.line = NULL; editor->buffer->mark.glyph = 0;
	} else {
		char *path = rd_get_path(editor->buffer, cursor_line, i);
		//printf("File selected should go <%s>\n", path);

		enum go_file_failure_reason gffr;
		buffer_t *target_buffer = go_file(editor->buffer, path, false, &gffr);

		if (target_buffer != NULL) go_to_buffer(editor, target_buffer, -1);
		else if (gffr == GFFR_BINARYFILE) {
			char *ex = malloc(sizeof(char) * (strlen(path) + 4));
			alloc_assert(ex);

			strcpy(ex, " {");
			strcat(ex, path);
			strcat(ex, "}");

			editor->ignore_next_entry_keyrelease = true;
			//TODO
			//editor_add_to_command_line(editor, ex);

			free(ex);
		}

		free(path);
	}
}
