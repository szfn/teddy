#include "go.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "interp.h"
#include "baux.h"
#include "ctype.h"
#include "editor.h"
#include "buffers.h"
#include "global.h"
#include "columns.h"
#include "lexy.h"
#include "rd.h"
#include "top.h"

buffer_t *go_file(const char *filename, bool create, enum go_file_failure_reason *gffr) {
	char *p = unrealpath(top_working_directory(), filename);
	char *urp = realpath(p, NULL);
	free(p);

	*gffr = GFFR_OTHER;

	if (urp == NULL) {
		return NULL;
	}

	//printf("going file\n");

	buffer_t *buffer = buffers_find_buffer_from_path(urp);
	if (buffer != NULL) goto go_file_return;

	//printf("path: <%s>\n", urp);

	struct stat s;
	if (stat(urp, &s) != 0) {
		if (create) {
			FILE *f = fopen(urp, "w");
			if (f) { fclose(f); }
			if (stat(urp, &s) != 0) {
				buffer = NULL;
				goto go_file_return;
			}
		} else {
			buffer = NULL;
			goto go_file_return;
		}
	}

	buffer = buffer_create();

	if (S_ISDIR(s.st_mode)) {
		if (load_dir(buffer, urp) != 0) {
			buffer_free(buffer);
			buffer = NULL;
		}
	} else {
		int r = load_text_file(buffer, urp);
		if (r != 0) {
			if (r == -2) {
				*gffr = GFFR_BINARYFILE;
			}
			buffer_free(buffer);
			buffer = NULL;
		}
	}

	if (buffer != NULL) {
		buffers_add(buffer);
	}

go_file_return:
	free(urp);
	return buffer;
}

editor_t *go_to_buffer(editor_t *editor, buffer_t *buffer, bool take_over) {
	editor_t *target = NULL;
	find_editor_for_buffer(buffer, NULL, NULL, &target);

	if (target != NULL) {
		editor_grab_focus(target, true);
		return target;
	}

	if (take_over && (editor != NULL)) {
		editor_switch_buffer(editor, buffer);
		return editor;
	} else {
		tframe_t *spawning_frame;

		if (editor != NULL) find_editor_for_buffer(editor->buffer, NULL, &spawning_frame, NULL);
		else spawning_frame = NULL;

		tframe_t *target_frame = heuristic_new_frame(columnset, spawning_frame, buffer);

		if (target_frame != NULL) {
			target = GTK_TEDITOR(tframe_content(target_frame));
			editor_grab_focus(target, true);
		}

		return target;
	}

	return editor;
}

/*
 examples:
  bim/bam/bla.c:7,32:
  bim/bam/bla.c(2:2):
  bim/bam/bla.c:[2,3]:
  bim/bam/bla.c[2:3]
  bim/bam/bla.c:2:
  bim/bam/bla.c(2)
  bim/bam/bla.c:[2]
  bla.c

  TODO: check python and perl formats
*/
