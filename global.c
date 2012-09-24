#include "global.h"

#include "cfg.h"
#include "baux.h"

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
PangoFontDescription *elements_font_description;

GdkCursor *cursor_arrow, *cursor_hand, *cursor_xterm, *cursor_fleur, *cursor_top_left_corner;

columns_t *columnset = NULL;

GHashTable *keybindings;

struct history search_history;
struct history command_history;

struct completer the_word_completer;
struct completer the_cmd_completer;

critbit0_tree closed_buffers_critbit;

int focus_can_follow_mouse = 1;

gboolean streq(gconstpointer a, gconstpointer b) {
	return strcmp(a, b) == 0;
}

void global_init() {
	selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	closed_buffers_critbit.root = NULL;

	elements_font_description = pango_font_description_from_string("tahoma,arial,sans-serif 11");

	if (!FcInit()) {
		printf("Error initializing font config library\n");
		exit(EXIT_FAILURE);
	}

	keybindings = g_hash_table_new(g_str_hash, streq);

	cmdcompl_init();
	compl_init(&the_cmd_completer);
	the_cmd_completer.prefix_from_buffer = &buffer_cmdcompl_word_at_cursor;
	the_cmd_completer.recalc = &cmdcompl_recalc;
	the_cmd_completer.tmpdata = strdup("");
	alloc_assert(the_cmd_completer.tmpdata);
	compl_init(&the_word_completer);

	cursor_arrow = gdk_cursor_new(GDK_ARROW);
	cursor_xterm = gdk_cursor_new(GDK_XTERM);
	cursor_hand = gdk_cursor_new(GDK_HAND1);
	cursor_fleur = gdk_cursor_new(GDK_FLEUR);
	cursor_top_left_corner = gdk_cursor_new(GDK_TOP_LEFT_CORNER);
}

void global_free() {
	FcFini();
}

char *unrealpath(char *absolute_path, const char *relative_path) {
	if (strlen(relative_path) == 0) goto return_relative_path;
	if (relative_path[0] == '/') goto return_relative_path;

	if (relative_path[0] == '~') {
		const char *home = getenv("HOME");
		char *r;
		if (home == NULL) goto return_relative_path;

		asprintf(&r, "%s/%s", home, relative_path+1);
		alloc_assert(r);

		return r;
	} else {
		if (absolute_path == NULL) {
			char *cwd = get_current_dir_name();
			alloc_assert(cwd);
			char *r;
			asprintf(&r, "%s/%s", cwd, relative_path);
			alloc_assert(r);
			free(cwd);
			return r;
		} else {
			char *r;
			asprintf(&r, "%s/%s", absolute_path, relative_path);
			alloc_assert(r);
			return r;
		}
	}

	return NULL;

	return_relative_path: {
		char *r = strdup(relative_path);
		alloc_assert(r);
		return r;
	}
}

void set_color_cfg(cairo_t *cr, int color) {
	uint8_t red = (uint8_t)(color);
	uint8_t green = (uint8_t)(color >> 8);
	uint8_t blue = (uint8_t)(color >> 16);

	//printf("Setting color: %d %d %d (%d)\n", red, green, blue, color);

	cairo_set_source_rgb(cr, red/255.0, green/255.0, blue/255.0);
}

static gboolean expose_frame(GtkWidget *widget, GdkEventExpose *event, editor_t *editor) {
	cairo_t *cr = gdk_cairo_create(widget->window);
	GtkAllocation allocation;

	gtk_widget_get_allocation(widget, &allocation);

	set_color_cfg(cr, config_intval(&global_config, CFG_BORDER_COLOR));
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);
	cairo_destroy(cr);

	return TRUE;
}

GtkWidget *frame_piece(gboolean horizontal) {
	GtkWidget *frame = gtk_drawing_area_new();

	if (horizontal)
		gtk_widget_set_size_request(frame, -1, +1);
	else
		gtk_widget_set_size_request(frame, +1, -1);

	g_signal_connect(G_OBJECT(frame), "expose_event", G_CALLBACK(expose_frame), NULL);

	return frame;
}

void place_frame_piece(GtkWidget *table, gboolean horizontal, int position, int length) {
	if (horizontal)
		gtk_table_attach(GTK_TABLE(table), frame_piece(TRUE),
			0, length,
			position, position+1,
			GTK_EXPAND|GTK_FILL, 0,
			0, 0);
	else
		gtk_table_attach(GTK_TABLE(table), frame_piece(FALSE),
			position, position+1,
			0, length,
			0, GTK_EXPAND|GTK_FILL,
			0, 0);
}

void utf32_to_utf8(uint32_t code, char **r, int *cap, int *allocated) {
	int first_byte_pad, first_byte_mask, inc;

	if (code <= 0x7f) {
		inc = 0;
		first_byte_pad = 0x00;
		first_byte_mask = 0x7f;
	} else if (code <= 0x7ff) {
		inc = 1;
		first_byte_pad = 0xc0;
		first_byte_mask = 0x1f;
	} else if (code <= 0xffff) {
		inc = 2;
		first_byte_pad = 0xe0;
		first_byte_mask = 0x0f;
	} else if (code <= 0x1fffff) {
		inc = 3;
		first_byte_pad = 0xf8;
		first_byte_mask = 0x07;
	}

	if (*cap+inc >= *allocated) {
		*allocated *= 2;
		*r = realloc(*r, sizeof(char) * *allocated);
		alloc_assert(r);
	}

	for (int i = inc; i > 0; --i) {
		(*r)[*cap+i] = ((uint8_t)code & 0x3f) + 0x80;
		code >>= 6;
	}

	(*r)[*cap] = ((uint8_t)code & first_byte_mask) + first_byte_pad;

	*cap += inc + 1;

	return;
}

bool inside_allocation(double x, double y, GtkAllocation *allocation) {
	return (x >= allocation->x)
		&& (x <= allocation->x + allocation->width)
		&& (y >= allocation->y)
		&& (y <= allocation->y + allocation->height);
}

char *string_utf16_to_utf8(uint16_t *origin, size_t origin_len) {
	int allocated = origin_len + 1, cap = 0;

	char *r = malloc(sizeof(char) * allocated);
	alloc_assert(r);


	for (int i = 0; i < origin_len; ++i) {
		utf32_to_utf8(origin[i], &r, &cap, &allocated);
	}

	utf32_to_utf8(0, &r, &cap, &allocated);

	return r;
}

static char first_byte_result_to_mask[] = { 0xff, 0x3f, 0x1f, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00 };

static uint8_t utf8_first_byte_processing(uint8_t ch) {
	if (ch <= 127) return 0;

	if ((ch & 0xF0) == 0xF0) {
		if ((ch & 0x08) == 0x00) return 3;
		else return 8; // invalid sequence
	}

	if ((ch & 0xE0) == 0xE0) return 2;
	if ((ch & 0xC0) == 0xC0) return 1;
	if ((ch & 0x80) == 0x80) return 8; // invalid sequence

	return 8; // invalid sequence
}

uint32_t utf8_to_utf32(const char *text, int *src, int len, bool *valid) {
	uint32_t code;
	*valid = true;

	/* get next unicode codepoint in code, advance src */
	if ((uint8_t)text[*src] > 127) {
		uint8_t tail_size = utf8_first_byte_processing(text[*src]);

		if (tail_size >= 8) {
			code = (uint8_t)text[*src];
			++(*src);
			*valid = false;
			return code;
		}

		code = ((uint8_t)text[*src]) & first_byte_result_to_mask[tail_size];
		++(*src);

		/*printf("   Next char: %02x (%02x)\n", (uint8_t)text[src], (uint8_t)text[src] & 0xC0);*/

		int i = 0;
		for (; (((uint8_t)text[*src] & 0xC0) == 0x80) && (*src < len); ++(*src)) {
			code <<= 6;
			code += (text[*src] & 0x3F);
			++i;
		}

		if (i != tail_size) {
			*valid = false;
		}
	} else {
		code = text[*src];
		++(*src);
	}

	return code;
}

void utf8_remove_truncated_characters_at_end(char *text) {
	if (!text) return;

	int src = 0, len = strlen(text);

	for (; src < len; ) {
		bool valid = false;
		char *start = text + src;
		utf8_to_utf32(text, &src, len, &valid);
		if (!valid) {
			*start = '\0';
			return;
		}
	}
}

void alloc_assert(void *p) {
	if (!p) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
}

bool find_editor_for_buffer(buffer_t *buffer, column_t **columnpp, tframe_t **framepp, editor_t **editorpp) {
	bool r = false;
	if (columnpp != NULL) *columnpp = NULL;
	if (framepp != NULL) *framepp = NULL;
	if (editorpp != NULL) *editorpp = NULL;
	if (buffer == NULL)  return false;

	GList *list_cols = gtk_container_get_children(GTK_CONTAINER(columnset));
	for (GList *cur_col = list_cols; cur_col != NULL; cur_col = cur_col->next) {
		if (!GTK_IS_COLUMN(cur_col->data)) continue;

		GList *list_frames = gtk_container_get_children(GTK_CONTAINER(cur_col->data));

		for (GList *cur_frame = list_frames; cur_frame != NULL; cur_frame = cur_frame->next) {
			if (!GTK_IS_TFRAME(cur_frame->data)) continue;

			GtkWidget *content = tframe_content(GTK_TFRAME(cur_frame->data));

			if (!GTK_IS_TEDITOR(content)) continue;

			editor_t *cur_editor = GTK_TEDITOR(content);

			if (cur_editor->buffer == buffer) {
				if (columnpp != NULL) *columnpp = GTK_COLUMN(cur_col->data);
				if (framepp != NULL) *framepp = GTK_TFRAME(cur_frame->data);
				if (editorpp != NULL) *editorpp = cur_editor;
				r = true;
				break;
			}
		}

		g_list_free(list_frames);
		if (r) break;
	}
	g_list_free(list_cols);

	return r;
}

void quick_message(const char *title, const char *msg) {
	GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(columnset))), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *label = gtk_label_new(msg);

	g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);

	gtk_container_add(GTK_CONTAINER(content_area), label);
	gtk_widget_show_all(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
}

uint32_t *utf8_to_utf32_string(const char *text, int *dstlen) {
	int len = strlen(text);
	uint32_t *r = malloc(len*sizeof(uint32_t));
	alloc_assert(r);

	*dstlen = 0;
	for (int i = 0; i < len; ) {
		bool valid = true;
		r[(*dstlen)++] = utf8_to_utf32(text, &i, len, &valid);
	}

	return r;
}

int null_strcmp(const char *a, const char *b) {
	if (a == NULL) {
		if (b == NULL) return 0;
		if (strcmp(b, "") == 0) return 0;
		return -1;
	}

	if (b == NULL) {
		if (strcmp(a, "") == 0) return 0;
		return -1;
	}

	return strcmp(a, b);
}

void gtk_widget_modify_bg_all(GtkWidget *w, GdkColor *c) {
	gtk_widget_modify_bg(w, GTK_STATE_NORMAL, c);
	gtk_widget_modify_bg(w, GTK_STATE_ACTIVE, c);
	gtk_widget_modify_bg(w, GTK_STATE_PRELIGHT, c);
	gtk_widget_modify_bg(w, GTK_STATE_SELECTED, c);
	gtk_widget_modify_bg(w, GTK_STATE_INSENSITIVE, c);
}