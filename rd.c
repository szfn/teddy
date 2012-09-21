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

char type_identifier[0xff+1];

static void rd_list(buffer_t *buffer, DIR *dir) {
	struct dirent entry;
	struct dirent *result;

	for (;;) {
		int r = readdir_r(dir, &entry, &result);
		if (r != 0) {
			buffer_replace_selection(buffer, "Error reading directory\n");
			break;
		}
		if (result == NULL) {
			// end of stream
			break;
		}
		if (entry.d_name[0] == '.') continue;

		buffer_replace_selection(buffer, entry.d_name);

		char id = type_identifier[entry.d_type];
		if (id != ' ') {
			char z[] = { id, 0 };
			buffer_replace_selection(buffer, z);
		}

		buffer_replace_selection(buffer, "\n");
	}
}

void rd_init(void) {
	for (int i = 0; i <= 0xff; ++i) {
		type_identifier[i] = ' ';
	}

	type_identifier[DT_BLK] = '%';
	type_identifier[DT_CHR] = '^';
	type_identifier[DT_DIR] = '/';
	type_identifier[DT_FIFO] = '|';
	type_identifier[DT_LNK] = '@';
	type_identifier[DT_REG] = ' ';
	type_identifier[DT_SOCK] = '=';
	type_identifier[DT_UNKNOWN] = '?';
}

void rd(DIR *dir, buffer_t *buffer) {
	rd_list(buffer, dir);
	buffer->cursor.line = buffer->real_line;
	buffer->cursor.glyph = 0;

	//rdrs(buffer, "\n", L_NOTHING);

	buffer->editable = 0;
}
