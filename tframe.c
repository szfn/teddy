#include "tframe.h"

#include "columns.h"
#include "buffers.h"
#include "global.h"
#include "foundry.h"
#include "cfg.h"

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
	struct _column_t *motion_col, *motion_prev_col;
	struct _tframe_t *motion_prev_tf;
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
	gdk_window_set_cursor(gtk_widget_get_window(widget), gdk_cursor_new(GDK_TOP_LEFT_CORNER));
	return FALSE;
}

static gboolean reshandle_button_press_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
		column_t *col, *prev_col;
		tframe_t *prev_tf;

		if (!columns_find_frame(tf->columns, tf, &prev_col, &col, NULL, &prev_tf, NULL)) return TRUE;

		tf->origin_x = event->x;
		tf->origin_y = event->y;
		tf->moving = true;

		tf->motion_col = col;
		tf->motion_prev_col = prev_col;
		tf->motion_prev_tf = prev_tf;

		return TRUE;
	}

	return FALSE;
}

static gboolean reshandle_motion_callback(GtkWidget *widget, GdkEventMotion *event, tframe_t *tf) {
	if (!tf->moving) return TRUE;

	double changey = event->y - tf->origin_y;
	double changex = event->x - tf->origin_x;

	if (tf->motion_prev_tf != NULL) {
		GtkAllocation allocation;

		gtk_widget_get_allocation(GTK_WIDGET(tf->motion_col), &allocation);
		double change_fraction = changey / allocation.height * 10.0;

		tframe_fraction_set(tf, tframe_fraction(tf) - change_fraction);
		tframe_fraction_set(tf->motion_prev_tf, tframe_fraction(tf->motion_prev_tf) + change_fraction);

		if (tframe_fraction(tf) < 0) {
			tframe_fraction_set(tf->motion_prev_tf, tframe_fraction(tf->motion_prev_tf) + tframe_fraction(tf));
			tframe_fraction_set(tf, 0.0);
		}

		if (tframe_fraction(tf->motion_prev_tf) < 0) {
			tframe_fraction_set(tf, tframe_fraction(tf) + tframe_fraction(tf->motion_prev_tf));
			tframe_fraction_set(tf->motion_prev_tf, 0.0);
		}

		gtk_column_size_allocate(GTK_WIDGET(tf->motion_col), &(GTK_WIDGET(tf->motion_col)->allocation));
	}

	if (tf->motion_prev_col != NULL) {
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

	do {
		cairo_text_extents(cr, tf->title+start, &titlext);
		double width = titlext.width + ellipsext.width + 4.0;

		if (width < allocation.width) break;

		double tocut = width - allocation.width;
		int tocut_chars = tocut / titlext.width * strlen(tf->title+start);
		start += tocut_chars + 1;
		if (start > strlen(tf->title)) start = strlen(tf->title);
	} while (start < strlen(tf->title));

	cairo_move_to(cr, 4.0, ext.ascent);
	if (start != 0) cairo_show_text(cr, ellipsis);
	cairo_show_text(cr, tf->title+start);

	cairo_destroy(cr);

	return TRUE;
}

static gboolean label_map_callback(GtkWidget *widget, GdkEvent *event, tframe_t *tf) {
	gdk_window_set_cursor(gtk_widget_get_window(widget), gdk_cursor_new(GDK_FLEUR));
	return FALSE;
}

bool dragging = false;

static gboolean label_button_press_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *frame) {
	column_t *col;
	if (!columns_find_frame(columnset, frame, NULL, &col, NULL, NULL, NULL)) return TRUE;

	if (event->button == 1) {
		if (event->type == GDK_2BUTTON_PRESS) {
			dragging = false;

			if (column_remove_others(col, frame) == 0) {
				columns_remove_others(columnset, col);
			}
			return TRUE;
		} else {
			dragging = true;
		}
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 2)) {
		columns_column_remove(columnset, col, frame);
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
		editor_switch_buffer(GTK_TEDITOR(target), sbuf);
		columns_column_remove(columnset, source_col, source);
	} else {
		// actually moving source somewhere else

		column_t *target_col = NULL;
		if (!columns_find_frame(columnset, target, NULL, &target_col, NULL, NULL, NULL)) return;

		g_object_ref(source);
		columns_column_remove(columnset, source_col, source);
		column_add_after(target_col, target, source);
		g_object_unref(source);

		GtkAllocation tallocation;
		gtk_widget_get_allocation(GTK_WIDGET(target), &tallocation);

		double new_target_size = y - tallocation.y;
		double new_source_size = tallocation.height - new_target_size;

		column_resize_frame_pair(target_col, target, new_target_size, source, new_source_size);
	}
}

static gboolean label_button_release_callback(GtkWidget *widget, GdkEventButton *event, tframe_t *tf) {
	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	double x = event->x + allocation.x;
	double y = event->y + allocation.y;

	if (event->button != 1) return FALSE;
	if (!dragging) return FALSE;

	dragging = false;

	bool ontag = false;
	tframe_t *target = columns_get_frame_from_position(columnset, x, y, &ontag);

	tag_drag_behaviour(tf, target, y);

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

	r->tag = gtk_hbox_new(FALSE, 0);

	r->drarla = gtk_drawing_area_new();
	r->resdr = gtk_drawing_area_new();
	gtk_widget_set_size_request(r->resdr, 14, 14);

	cairo_font_extents_t tag_font_extents;
	cairo_scaled_font_extents(fontset_get_cairofont_by_name(config_strval(&global_config, CFG_TAG_FONT), 0), &tag_font_extents);
	gtk_widget_set_size_request(r->drarla, 1, tag_font_extents.ascent + tag_font_extents.descent);

	gtk_widget_add_events(r->resdr, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK|GDK_STRUCTURE_MASK);

	gtk_widget_add_events(r->drarla, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_STRUCTURE_MASK);

	g_signal_connect(G_OBJECT(r->resdr), "expose_event", G_CALLBACK(reshandle_expose_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "map_event", G_CALLBACK(reshandle_map_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "button_press_event", G_CALLBACK(reshandle_button_press_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "motion_notify_event", G_CALLBACK(reshandle_motion_callback), r);
	g_signal_connect(G_OBJECT(r->resdr), "button_release_event", G_CALLBACK(reshandle_button_release_callback), r);

	g_signal_connect(G_OBJECT(r->drarla), "expose_event", G_CALLBACK(label_expose_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "map_event", G_CALLBACK(label_map_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "button-press-event", G_CALLBACK(label_button_press_callback), r);
	g_signal_connect(G_OBJECT(r->drarla), "button-release-event", G_CALLBACK(label_button_release_callback), r);

	gtk_container_add(GTK_CONTAINER(r->tag), r->resdr);
	gtk_container_add(GTK_CONTAINER(r->tag), r->drarla);

	gtk_box_set_child_packing(GTK_BOX(r->tag), r->resdr, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(r->tag), r->drarla, TRUE, TRUE, 0, GTK_PACK_START);

	place_frame_piece(GTK_WIDGET(t), TRUE, 0, 2);
	place_frame_piece(GTK_WIDGET(t), FALSE, 1, 4);

	gtk_table_attach(t, r->tag, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);

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
