#include "global.h"

#include "cfg.h"

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
PangoFontDescription *elements_font_description;

buffer_t *selection_target_buffer = NULL;

columns_t *columnset = NULL;

GHashTable *keybindings;

history_t *search_history;
history_t *command_history;

int focus_can_follow_mouse = 1;

gboolean streq(gconstpointer a, gconstpointer b) {
	return (strcmp(a, b) == 0);
}

void global_init() {
	selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	elements_font_description = pango_font_description_from_string("tahoma,arial,sans-serif 11");

	if (!FcInit()) {
		printf("Error initializing font config library\n");
		exit(EXIT_FAILURE);
	}

	keybindings = g_hash_table_new(g_str_hash, streq);

	search_history = history_new();
	command_history = history_new();
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

		r = malloc(sizeof(char) * (strlen(relative_path) + strlen(home) + 1));
		strcpy(r, home);
		strcpy(r + strlen(r), relative_path+1);
		return r;
	} else {
		if (absolute_path == NULL) {
			char *cwd = get_current_dir_name();
			char *r = malloc(sizeof(char) * (strlen(relative_path) + strlen(cwd) + 2));

			strcpy(r, cwd);
			r[strlen(r)] = '/';
			strcpy(r + strlen(cwd) + 1, relative_path);

			free(cwd);
			return r;
		} else {
			char *end = strrchr(absolute_path, '/');
			char *r = malloc(sizeof(char) * (strlen(relative_path) + (end - absolute_path) + 2));

			strncpy(r, absolute_path, end-absolute_path+1);
			strcpy(r+(end-absolute_path+1), relative_path);

			return r;
		}
	}

	return NULL;

	return_relative_path: {
		char *r = malloc(sizeof(char) * (strlen(relative_path)+1));
		strcpy(r, relative_path);
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

	set_color_cfg(cr, config[CFG_BORDER_COLOR].intval);
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
		(*r)[*cap+i] = ((uint8_t)code & 0x2f) + 0x80;
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
