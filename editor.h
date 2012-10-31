#ifndef __EDITOR_H__
#define __EDITOR_H__

#include "buffer.h"
#include "compl.h"
#include "research.h"

#include <gtk/gtk.h>

#define LOCKED_COMMAND_LINE_SIZE 256
#define AUTOSCROLL_TIMO 100

#define GTK_TYPE_TEDITOR (gtk_teditor_get_type())
#define GTK_TEDITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_TEDITOR, editor_t))
#define GTK_TEDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_TEDITOR, editor_class))
#define GTK_IS_TEDITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_TEDITOR))
#define GTK_TEDITOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_TEDITOR, editor_class))

typedef struct _editor_t {
	GtkTable table;

	buffer_t *buffer;

	GtkObject *adjustment, *hadjustment;
	GtkWidget *drar;
	GtkWidget *drarscroll;
	GtkWidget *drarhscroll;
	GtkIMContext *drarim;
	gboolean cursor_visible;
	int mouse_marking;

	GtkWidget *search_entry, *search_box;
	GtkWidget *stale_box;
	GtkWidget *context_menu;

	gulong current_entry_handler_id;

	gboolean ignore_next_entry_keyrelease;
	gboolean center_on_cursor_after_next_expose;
	gboolean warp_mouse_after_next_expose;

	glong selection_scroll_timer;

	bool dirty_line;

	bool single_line;
	void (*single_line_escape)(struct _editor_t *editor);
	void (*single_line_return)(struct _editor_t *editor);
	bool (*single_line_other_keys)(struct _editor_t *editor, bool shift, bool ctrl, bool alt, bool super, guint keyval);

	char locked_command_line[LOCKED_COMMAND_LINE_SIZE];

	struct completer *completer;
	struct completer *alt_completer;

	struct research_t research;
	GtkWidget *prev_search_button, *execute_search_button, *execute_all_search_button;
} editor_t;

typedef struct _editor_class {
	GtkTableClass parent_class;
} editor_class;

GType gtk_teditor_get_type(void) G_GNUC_CONST;

void set_label_text(editor_t *editor);

editor_t *new_editor(buffer_t *buffer, bool single_line);
void editor_free(editor_t *editor);
void editor_switch_buffer(editor_t *editor, buffer_t *buffer);

enum include_cursor_mode {
	ICM_TOP = 0,
	ICM_MID = 1,
	ICM_BOT = 2
};

void editor_include_cursor(editor_t *editor, enum include_cursor_mode above, enum include_cursor_mode below);

void editor_start_search(editor_t *editor, enum search_mode_t search_mode, const char *initial_search_term);
void editor_save_action(editor_t *editor);
void editor_undo_action(editor_t *editor, bool redo);

void editor_complete_move(editor_t *editor, gboolean should_move_origin);

void editor_grab_focus(editor_t *editor, bool warp);

void editor_cursor_position(editor_t *editor, double *x, double *y, double *alty);

#endif
