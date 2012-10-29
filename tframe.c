#include "tframe.h"

#include "columns.h"
#include "buffers.h"
#include "global.h"
#include "foundry.h"
#include "top.h"
#include "cfg.h"

#include <math.h>

GdkPixbuf *maximize_pixbuf = NULL, *close_pixbuf = NULL;

static const guint8 align_just_icon[] __attribute__ ((__aligned__ (4))) =
{ ""
  /* Pixbuf magic (0x47646b50) */
  "GdkP"
  /* length: header (24) + pixel_data (1024) */
  "\0\0\4\30"
  /* pixdata_type (0x1010002) */
  "\1\1\0\2"
  /* rowstride (64) */
  "\0\0\0@"
  /* width (16) */
  "\0\0\0\20"
  /* height (16) */
  "\0\0\0\20"
  /* pixel_data: */
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0""000\20"
  "111\357222\377222\377222\377222\377222\377222\377222\377222\377222\377"
  "222\377222\377222\377222\377111\357000\20///\377///\377///\377///\377"
  "///\377///\377///\377///\377///\377///\377///\377///\377///\377///\377"
  "///\377\0\0\0\0+++\357+++\377+++\377+++\377+++\377+++\377+++\377+++\377"
  "+++\377+++\377+++\377+++\377+++\377+++\377+++\357\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\34\34\34\20\35\35\35\357\36\36\36\377\36\36"
  "\36\377\36\36\36\377\36\36\36\377\36\36\36\377\36\36\36\377\36\36\36"
  "\377\36\36\36\377\36\36\36\377\36\36\36\377\36\36\36\377\36\36\36\377"
  "\36\36\36\377\35\35\35\357\33\33\33\20\32\32\32\377\32\32\32\377\32\32"
  "\32\377\32\32\32\377\32\32\32\377\32\32\32\377\32\32\32\377\32\32\32"
  "\377\32\32\32\377\32\32\32\377\32\32\32\377\32\32\32\377\32\32\32\377"
  "\32\32\32\377\32\32\32\377\0\0\0\0\27\27\27\357\27\27\27\377\27\27\27"
  "\377\27\27\27\377\27\27\27\377\27\27\27\377\27\27\27\377\27\27\27\377"
  "\27\27\27\377\27\27\27\377\27\27\27\377\27\27\27\377\27\27\27\377\27"
  "\27\27\377\27\27\27\357\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\10"
  "\10\10\20\11\11\11\357\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11"
  "\377\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11\377"
  "\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11\357\7\7"
  "\7\20\6\6\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6\6"
  "\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6\6\6\377\6"
  "\6\6\377\6\6\6\377\0\0\0\0\2\2\2\357\2\2\2\377\2\2\2\377\2\2\2\377\2"
  "\2\2\377\2\2\2\377\2\2\2\377\2\2\2\377\2\2\2\377\2\2\2\377\2\2\2\377"
  "\2\2\2\377\2\2\2\377\2\2\2\377\2\2\2\357"};

static const guint8 delete_icon[] __attribute__ ((__aligned__ (4))) =
{ ""
  /* Pixbuf magic (0x47646b50) */
  "GdkP"
  /* length: header (24) + pixel_data (1024) */
  "\0\0\4\30"
  /* pixdata_type (0x1010002) */
  "\1\1\0\2"
  /* rowstride (64) */
  "\0\0\0@"
  /* width (16) */
  "\0\0\0\20"
  /* height (16) */
  "\0\0\0\20"
  /* pixel_data: */
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0""0000111\357111\317000\20\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0""0000111\357111\237\0\0\0\0\0\0\0\0\0\0\0\0---0...\357///\377"
  "///\377...\317---\20\0\0\0\0\0\0\0\0\0\0\0\0---0...\357///\377///\377"
  "...\237\0\0\0\0\0\0\0\0***\357+++\377+++\377+++\377+++\377***\317)))"
  "\20\0\0\0\0)))0***\357+++\377+++\377+++\377+++\377***\217\0\0\0\0((("
  "\317(((\377(((\377(((\377(((\377(((\377'''\317&&&@'''\357(((\377(((\377"
  "(((\377(((\377(((\377(((\217\0\0\0\0%%%\20$$$\317$$$\377$$$\377$$$\377"
  "$$$\377$$$\377$$$\377$$$\377$$$\377$$$\377$$$\377$$$\377$$$\237\0\0\0"
  "\0\0\0\0\0\0\0\0\0\"\"\"\20!!!\317!!!\377!!!\377!!!\377!!!\377!!!\377"
  "!!!\377!!!\377!!!\377!!!\377!!!\237\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\36\36\36\20\35\35\35\317\35\35\35\377\35\35\35\377\35\35\35\377"
  "\35\35\35\377\35\35\35\377\35\35\35\377\35\35\35\377\35\35\35\237\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\31\31\31@\32\32"
  "\32\377\32\32\32\377\32\32\32\377\32\32\32\377\32\32\32\377\32\32\32"
  "\377\31\31\31\357\30\30\30\20\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\25\25\25""0\26\26\26\357\26\26\26\377\26\26\26\377\26\26\26"
  "\377\26\26\26\377\26\26\26\377\26\26\26\377\26\26\26\377\26\26\26\317"
  "\25\25\25\20\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\21\21\21""0\22\22\22\357"
  "\23\23\23\377\23\23\23\377\23\23\23\377\23\23\23\377\23\23\23\377\23"
  "\23\23\377\23\23\23\377\23\23\23\377\23\23\23\377\22\22\22\317\21\21"
  "\21\20\0\0\0\0\0\0\0\0\16\16\16""0\17\17\17\357\17\17\17\377\17\17\17"
  "\377\17\17\17\377\17\17\17\377\17\17\17\377\17\17\17\357\17\17\17\377"
  "\17\17\17\377\17\17\17\377\17\17\17\377\17\17\17\377\17\17\17\317\16"
  "\16\16\20\0\0\0\0\13\13\13\357\14\14\14\377\14\14\14\377\14\14\14\377"
  "\14\14\14\377\14\14\14\377\14\14\14\237\15\15\15\20\14\14\14\317\14\14"
  "\14\377\14\14\14\377\14\14\14\377\14\14\14\377\14\14\14\377\13\13\13"
  "\257\0\0\0\0\10\10\10\237\10\10\10\377\10\10\10\377\10\10\10\377\10\10"
  "\10\377\10\10\10\237\0\0\0\0\0\0\0\0\11\11\11\20\10\10\10\317\10\10\10"
  "\377\10\10\10\377\10\10\10\377\10\10\10\377\11\11\11`\0\0\0\0\0\0\0\0"
  "\5\5\5\237\5\5\5\377\5\5\5\377\5\5\5\237\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\6\6\6\20\5\5\5\317\5\5\5\377\5\5\5\377\5\5\5`\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\1\1\1\217\1\1\1\217\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\2\2\2\20\1\1\1\257\2\2\2`\0\0\0\0\0\0\0\0"};


typedef struct _tframe_t {
	GtkTable table;
	GtkWidget *content;

	struct _columns_t *columns;

	GtkWidget *tag;
	GtkWidget *resdr;
	GtkWidget *drarla;

	char *title;

	double fraction;
	bool modified;

	double origin_x, origin_y;
	bool moving;
	bool column_resize_resistence_overcome;
	struct _column_t *motion_col, *motion_prev_col;
} tframe_t;

typedef struct _tframe_class {
	GtkTableClass parent_class;
} tframe_class;


static void gtk_tframe_class_init(tframe_class *klass);
static void gtk_tframe_init(tframe_t *tframe);

GType gtk_tframe_get_type(void) {
	static GType tframe_type = 0;

	if (!tframe_type) {
		static const GTypeInfo tframe_info = {
			sizeof(tframe_class),
			NULL,
			NULL,
			(GClassInitFunc)gtk_tframe_class_init,
			NULL,
			NULL,
			sizeof(tframe_t),
			0,
			(GInstanceInitFunc)gtk_tframe_init,
		};

		tframe_type = g_type_register_static(GTK_TYPE_TABLE, "tframe_t", &tframe_info, 0);
	}

	return tframe_type;
}

static void gtk_tframe_class_init(tframe_class *class) {
	// override methods here
}

static void gtk_tframe_init(tframe_t *tframe) {
}

static gboolean reshandle_expose_callback(GtkWidget *widget, GdkEventExpose *event, tframe_t *tf) {
	cairo_t *cr = gdk_cairo_create(widget->window);
	GtkAllocation allocation;

	gtk_widget_get_allocation(widget, &allocation);

	set_color_cfg(cr, config_intval(&global_config, CFG_TAG_BG_COLOR));
	//cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, 136.0/256, 136.0/256, 204.0/256);

	cairo_rectangle(cr, 2, 2, allocation.width-4, allocation.height-4);
	cairo_fill(cr);

	cairo_rectangle(cr, 4, 4, allocation.width - 8, allocation.height - 8);
	if (tf->modified) {
		cairo_set_source_rgb(cr, 0.0, 0.0, 153.0/256);
	} else {
		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	}

	cairo_fill(cr);

	cairo_destroy(cr);

	return TRUE;
}

static gboolean reshandle_map_callback(GtkWidget *widget, GdkEvent *event, tframe_t *tf) {
	gdk_window_set_cursor(gtk_widget_get_window(widget), cursor_top_left_corner);
	return FALSE;
}

static gboolean reshandle_button_press_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
		column_t *col, *prev_col;

		if (!columns_find_frame(tf->columns, tf, &prev_col, &col, NULL, NULL, NULL)) return TRUE;
		tf->origin_x = event->x;
		tf->origin_y = event->y;
		tf->moving = true;
		tf->column_resize_resistence_overcome = false;

		tf->motion_col = col;
		tf->motion_prev_col = prev_col;

		return TRUE;
	} else {
		column_t *col;
		if (!columns_find_frame(tf->columns, tf, NULL, &col, NULL, NULL, NULL)) return FALSE;
		column_expand_frame(col, tf);
	}

	return FALSE;
}

static void find_motion_frames(tframe_t *tf, tframe_t **prev, tframe_t **cur, bool increasing_prev) {
	if (tf->motion_col == NULL) return;

	GList *list = gtk_container_get_children(GTK_CONTAINER(tf->motion_col));
	bool before = true;
	for (GList *it = list; it != NULL; it = it->next) {
		if (!GTK_IS_TFRAME(it->data)) continue;
		tframe_t *itf = GTK_TFRAME(it->data);

		if (itf == tf) {
			before = false;
		}

		if (before) {
			if (increasing_prev) {
				*prev = itf;
			} else { // increasing_cur
				if (tframe_fraction(itf) > 0.0001) {
					*prev = itf;
				}
			}
		} else {
			if (increasing_prev) {
				if (tframe_fraction(itf) > 0.0001) {
					*cur = itf;
					break;
				}
			} else {	// increasing_cur
				*cur = itf;
				break;
			}
		}
	}
	g_list_free(list);
}

static gboolean reshandle_motion_callback(GtkWidget *widget, GdkEventMotion *event, tframe_t *tf) {
	if (!tf->moving) return TRUE;

	double changey = event->y - tf->origin_y;
	double changex = event->x - tf->origin_x;

	if (tf->motion_col != NULL) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(tf->motion_col), &allocation);
		double change_fraction = changey / allocation.height * 10.0;

		for (;;) {
			tframe_t *prevtf = NULL, *curtf = NULL;
			find_motion_frames(tf, &prevtf, &curtf, change_fraction > 0);
			if (prevtf == NULL) break;
			if (curtf == NULL) break;

			tframe_fraction_set(curtf, tframe_fraction(curtf) - change_fraction);
			tframe_fraction_set(prevtf, tframe_fraction(prevtf) + change_fraction);

			if (tframe_fraction(curtf) < 0) {
				change_fraction = -tframe_fraction(curtf);
				tframe_fraction_set(prevtf, tframe_fraction(prevtf) + tframe_fraction(curtf));
				tframe_fraction_set(curtf, 0.0);
			} else if (tframe_fraction(prevtf) < 0) {
				change_fraction = tframe_fraction(prevtf);
				tframe_fraction_set(tf, tframe_fraction(curtf) + tframe_fraction(prevtf));
				tframe_fraction_set(prevtf, 0.0);
			} else {
				break;
			}
		}

		gtk_column_size_allocate(GTK_WIDGET(tf->motion_col), &(GTK_WIDGET(tf->motion_col)->allocation));
	}

	if (!tf->column_resize_resistence_overcome) {
		if ((fabs(changex) > 28) && (tf->motion_prev_col != NULL)) {
			tf->column_resize_resistence_overcome = true;
		}
	}

	if (tf->column_resize_resistence_overcome) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(tf->columns), &allocation);
		double change_fraction = changex / allocation.width * 10.0;

		column_fraction_set(tf->motion_col, column_fraction(tf->motion_col) - change_fraction);
		column_fraction_set(tf->motion_prev_col, column_fraction(tf->motion_prev_col) + change_fraction);

		if (column_fraction(tf->motion_col) < 0) {
			column_fraction_set(tf->motion_prev_col, column_fraction(tf->motion_prev_col) + column_fraction(tf->motion_col));
			column_fraction_set(tf->motion_col, 0.0);
		}

		if (column_fraction(tf->motion_prev_col) < 0) {
			column_fraction_set(tf->motion_col, column_fraction(tf->motion_col) + column_fraction(tf->motion_prev_col));
			column_fraction_set(tf->motion_prev_col, 0.0);
		}

		gtk_columns_size_allocate(GTK_WIDGET(tf->columns), &(GTK_WIDGET(tf->columns)->allocation));
	}

	gtk_widget_queue_draw(GTK_WIDGET(tf->columns));

	return TRUE;
}

static void column_resize_frame_pair(column_t *column, tframe_t *above, double new_above_size, tframe_t *below, double new_below_size) {
	double total_fraction = tframe_fraction(above) + tframe_fraction(below);
	double total_height = new_above_size + new_below_size;

	tframe_fraction_set(above, total_fraction * (new_above_size / total_height));
	tframe_fraction_set(below, total_fraction * (new_below_size / total_height));

	gtk_column_size_allocate(GTK_WIDGET(column), &(GTK_WIDGET(column)->allocation));
	gtk_widget_queue_draw(GTK_WIDGET(column));
}

static gboolean reshandle_button_release_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	tf->moving = false;
	return TRUE;
}

bool dragging = false;

static gboolean label_motion_callback(GtkWidget *widget, GdkEventMotion *event, tframe_t *tf) {
	dragging = true;
	return FALSE;
}

static gboolean label_expose_callback(GtkWidget *widget, GdkEventExpose *event, tframe_t *tf) {
	cairo_t *cr = gdk_cairo_create(widget->window);
	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	set_color_cfg(cr, config_intval(&global_config, CFG_TAG_BG_COLOR));
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	set_color_cfg(cr, config_intval(&global_config, CFG_TAG_FG_COLOR));

	cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config_strval(&global_config, CFG_TAG_FONT), 0));

	int start = 0;
	const char *ellipsis = "…";

	cairo_font_extents_t ext;
	cairo_scaled_font_extents(fontset_get_cairofont_by_name(config_strval(&global_config, CFG_TAG_FONT), 0), &ext);

	cairo_text_extents_t titlext, ellipsext;
	cairo_text_extents(cr, ellipsis, &ellipsext);

	const char *title;
	if (strstr(tf->title, top_working_directory()) == tf->title) {
		title = tf->title + strlen(top_working_directory());
		if (title[0] == '/') title++;
	} else {
		title = tf->title;
	}

	if (title[0] == '\0') title = ".";

	do {
		cairo_text_extents(cr, title+start, &titlext);
		double width = titlext.width + ellipsext.width + 4.0;

		if (width < allocation.width) break;

		double tocut = width - allocation.width;
		int tocut_chars = tocut / titlext.width * strlen(title+start);
		start += tocut_chars + 1;
		if (start > strlen(title)) start = strlen(title);
	} while (start < strlen(title));

	cairo_move_to(cr, 4.0, ext.ascent);
	if (start != 0) cairo_show_text(cr, ellipsis);
	cairo_show_text(cr, title+start);

	cairo_destroy(cr);

	return TRUE;
}

static gboolean label_map_callback(GtkWidget *widget, GdkEvent *event, tframe_t *tf) {
	gdk_window_set_cursor(gtk_widget_get_window(widget), cursor_fleur);
	return FALSE;
}

static gboolean label_button_press_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *frame) {
	column_t *col;
	if (!columns_find_frame(columnset, frame, NULL, &col, NULL, NULL, NULL)) return TRUE;

	if (event->button == 1) {
		if (event->type == GDK_2BUTTON_PRESS) {
			dragging = false;
			column_hide_others(col, frame);
			return TRUE;
		} else {
			dragging = false;
		}
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 2)) {
		columns_column_remove(columnset, col, frame, false, true);
		return TRUE;
	}

	return FALSE;
}

static void tag_drag_behaviour(tframe_t *source, tframe_t *target, double y) {
	if (source == NULL) return;
	if (target == NULL) return;

	tframe_t *before_tf;
	column_t *source_col;

	if (!columns_find_frame(columnset, source, NULL, &source_col, NULL, &before_tf, NULL)) return;

	buffer_t *sbuf = NULL;
	if (GTK_IS_TEDITOR(source->content)) {
		sbuf = GTK_TEDITOR(source->content)->buffer;
	}

	buffer_t *tbuf = NULL;
	if (GTK_IS_TEDITOR(target->content)) {
		tbuf = GTK_TEDITOR(target->content)->buffer;
	}

	if ((target == source) || (target == before_tf)) {
		// moving an editor inside itself or inside the editor above it means we want to resize it

		if (before_tf == NULL) return; // attempted resize but there is nothing above the source editor

		GtkAllocation a_above;
		gtk_widget_get_allocation(GTK_WIDGET(before_tf), &a_above);

		GtkAllocation a_source;
		gtk_widget_get_allocation(GTK_WIDGET(source), &a_source);

		double new_above_size = y - a_above.y;
		double new_source_size = a_above.height - new_above_size + a_source.height;

		column_resize_frame_pair(source_col, before_tf, new_above_size, source, new_source_size);
	} else if ((tbuf == null_buffer()) && (sbuf != null_buffer())) {
		// we dragged into a null buffer, take it over
		editor_switch_buffer(GTK_TEDITOR(source->content), null_buffer());
		editor_switch_buffer(GTK_TEDITOR(target->content), sbuf);

		// this extra three lines are only needed when dragging the sole frame of a column into a null buffer
		// they insure that the source frame is correctly refreshed
		tframe_set_title(source, null_buffer()->path);
		tframe_set_modified(source, false);
		gtk_widget_queue_draw(GTK_WIDGET(source));

		columns_column_remove(columnset, source_col, source, false, false);
	} else {
		// actually moving source somewhere else

		column_t *target_col = NULL;
		if (!columns_find_frame(columnset, target, NULL, &target_col, NULL, NULL, NULL)) return;

		g_object_ref(source);
		column_remove(source_col, source, true, false);
		column_add_after(target_col, target, source, true);
		g_object_unref(source);

		GtkAllocation tallocation;
		gtk_widget_get_allocation(GTK_WIDGET(target), &tallocation);

		double new_target_size = y - tallocation.y;
		double new_source_size = tallocation.height - new_target_size;

		column_resize_frame_pair(target_col, target, new_target_size, source, new_source_size);

		if (column_frame_number(source_col) <= 0) {
			editor_t *nulleditor = new_editor(null_buffer(), false);
			tframe_t *nullframe = tframe_new(null_buffer()->path, GTK_WIDGET(nulleditor), columnset);
			column_add_after(source_col, NULL, nullframe, true);
		}
	}
}

static gboolean label_button_release_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	if (event->button != 1) return FALSE;
	if (!dragging) {
		column_t *col;
		if (!columns_find_frame(tf->columns, tf, NULL, &col, NULL, NULL, NULL)) return FALSE;
		column_expand_frame(col, tf);
		return FALSE;
	}

	GtkAllocation allocation;
	gtk_widget_get_allocation(gtk_widget_get_parent(tf->tag), &allocation);

	double x = event->x + allocation.x;
	double y = event->y + allocation.y;

	dragging = false;

	bool ontag = false;
	tframe_t *target = columns_get_frame_from_position(columnset, x, y, &ontag);

	tag_drag_behaviour(tf, target, y);

	return TRUE;
}

static gboolean close_box_button_press_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	column_t *col;
	if (!columns_find_frame(tf->columns, tf, NULL, &col, NULL, NULL, NULL)) return FALSE;
	columns_column_remove(columnset, col, tf, false, true);
	return TRUE;
}

static gboolean magnify_box_button_press_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	column_t *col;
	if (!columns_find_frame(tf->columns, tf, NULL, &col, NULL, NULL, NULL)) return FALSE;
	column_hide_others(col, tf);
	return TRUE;
}

tframe_t *tframe_new(const char *title, GtkWidget *content, columns_t *columns) {
	GtkWidget *tframe_widget = g_object_new(GTK_TYPE_TFRAME, NULL);
	tframe_t *r = GTK_TFRAME(tframe_widget);
	GtkTable *t = GTK_TABLE(r);

	r->content = content;
	r->columns = columns;

	r->fraction = 1.0;
	r->modified = false;

	r->moving = false;

	gtk_table_set_homogeneous(t, FALSE);

	r->title = strdup((title != NULL) ? title : "");
	alloc_assert(r->title);

	GdkColor c;
	set_gdk_color_cfg(&global_config, CFG_TAG_BG_COLOR, &c);

	r->tag = gtk_hbox_new(FALSE, 0);

	r->drarla = gtk_drawing_area_new();
	r->resdr = gtk_drawing_area_new();
	gtk_widget_set_size_request(r->resdr, 14, 14);

	if (maximize_pixbuf == NULL) maximize_pixbuf = gdk_pixbuf_scale_simple(gdk_pixbuf_new_from_inline(sizeof(align_just_icon), align_just_icon, FALSE, NULL), 14, 14, GDK_INTERP_HYPER);
	if (close_pixbuf == NULL) close_pixbuf = gdk_pixbuf_scale_simple(gdk_pixbuf_new_from_inline(sizeof(delete_icon), delete_icon, FALSE, NULL), 14, 14, GDK_INTERP_HYPER);

	GtkWidget *close_box = gtk_event_box_new();
	gtk_widget_modify_bg_all(close_box, &c);
	gtk_container_add(GTK_CONTAINER(close_box), gtk_image_new_from_pixbuf(close_pixbuf));

	GtkWidget *magnify_box = gtk_event_box_new();
	gtk_widget_modify_bg_all(magnify_box, &c);
	gtk_container_add(GTK_CONTAINER(magnify_box), gtk_image_new_from_pixbuf(maximize_pixbuf));

	gtk_widget_add_events(close_box, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(magnify_box, GDK_BUTTON_PRESS_MASK);

	cairo_font_extents_t tag_font_extents;
	cairo_scaled_font_extents(fontset_get_cairofont_by_name(config_strval(&global_config, CFG_TAG_FONT), 0), &tag_font_extents);
	gtk_widget_set_size_request(r->drarla, 1, tag_font_extents.ascent + tag_font_extents.descent);

	gtk_widget_add_events(r->resdr, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK|GDK_STRUCTURE_MASK);

	gtk_widget_add_events(r->drarla, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_STRUCTURE_MASK|GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);

	g_signal_connect(G_OBJECT(r->resdr), "expose_event", G_CALLBACK(reshandle_expose_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "map_event", G_CALLBACK(reshandle_map_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "button_press_event", G_CALLBACK(reshandle_button_press_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "motion_notify_event", G_CALLBACK(reshandle_motion_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "button_release_event", G_CALLBACK(reshandle_button_release_callback), r);

	g_signal_connect(G_OBJECT(r->drarla), "expose_event", G_CALLBACK(label_expose_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "map_event", G_CALLBACK(label_map_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "button-press-event", G_CALLBACK(label_button_press_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "motion_notify_event", G_CALLBACK(label_motion_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "button-release-event", G_CALLBACK(label_button_release_callback), r);
	g_signal_connect(G_OBJECT(close_box), "button_press_event", G_CALLBACK(close_box_button_press_callback), r);
	g_signal_connect(G_OBJECT(magnify_box), "button_press_event", G_CALLBACK(magnify_box_button_press_callback), r);

	gtk_box_pack_start(GTK_BOX(r->tag), r->resdr, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r->tag), r->drarla, TRUE, TRUE, 0);

	gtk_box_pack_end(GTK_BOX(r->tag), close_box, FALSE, TRUE, 2);
	gtk_box_pack_end(GTK_BOX(r->tag), magnify_box, FALSE, TRUE, 2);

	place_frame_piece(GTK_WIDGET(t), TRUE, 0, 2);
	place_frame_piece(GTK_WIDGET(t), FALSE, 1, 4);

	GtkWidget *colorbox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(colorbox), r->tag);
	gtk_widget_modify_bg_all(colorbox, &c);

	gtk_table_attach(t, colorbox, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);
//	gtk_table_attach(t, r->tag, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	place_frame_piece(GTK_WIDGET(t), TRUE, 2, 1);

	gtk_table_attach(t, content, 0, 1, 3, 4, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);

	return r;
}

void tframe_set_title(tframe_t *tframe, const char *title) {
	free(tframe->title);
	tframe->title = strdup((title != NULL) ? title : "");
	alloc_assert(tframe->title);
}

void tframe_set_modified(tframe_t *tframe, bool modified) {
	tframe->modified = modified;
}

double tframe_fraction(tframe_t *tframe) {
	return tframe->fraction;
}

void tframe_fraction_set(tframe_t *tframe, double fraction) {
	tframe->fraction = fraction;
}

GtkWidget *tframe_content(tframe_t *frame) {
	return frame->content;
}

bool tframe_close(tframe_t *tframe, bool resist) {
	if (GTK_IS_TEDITOR(tframe->content)) {
		bool was_null_buffer = GTK_TEDITOR(tframe->content)->buffer == null_buffer();
		bool r = buffers_close(GTK_TEDITOR(tframe->content)->buffer, true);
		if (resist) return was_null_buffer; else return r;
	} else {
		return false;
	}
}
