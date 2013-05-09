#include "global.h"

#include <fontconfig/fontconfig.h>

#include "cfg.h"
#include "top.h"
#include "interp.h"
#include "buffers.h"
#include "ipc.h"
#include "mq.h"

#define MAX_GLOBAL_EVENT_WATCHERS 20

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;

char *tied_session;

bool fullscreen_on_startup = false;
bool at_fullscreen = false;

struct multiqueue global_event_watchers;

GdkCursor *cursor_blank, *cursor_arrow, *cursor_hand, *cursor_xterm, *cursor_fleur, *cursor_top_left_corner;

columns_t *columnset = NULL;

GHashTable *keybindings;

struct history search_history;
struct history command_history;
struct history input_history;

struct completer the_word_completer;

critbit0_tree closed_buffers_critbit;

int focus_can_follow_mouse = 1;

gboolean streq(gconstpointer a, gconstpointer b) {
	return strcmp(a, b) == 0;
}

void global_init() {
	selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	tied_session = NULL;
	closed_buffers_critbit.root = NULL;

	if (!FcInit()) {
		printf("Error initializing font config library\n");
		exit(EXIT_FAILURE);
	}

	keybindings = g_hash_table_new(g_str_hash, streq);

	cursor_blank = gdk_cursor_new(GDK_BLANK_CURSOR);
	cursor_arrow = gdk_cursor_new(GDK_ARROW);
	cursor_xterm = gdk_cursor_new(GDK_XTERM);
	cursor_hand = gdk_cursor_new(GDK_HAND1);
	cursor_fleur = gdk_cursor_new(GDK_FLEUR);
	cursor_top_left_corner = gdk_cursor_new(GDK_TOP_LEFT_CORNER);

	mq_alloc(&global_event_watchers, MAX_GLOBAL_EVENT_WATCHERS);
}

void global_free() {
	FcFini();
}

char *unrealpath(const char *basedir, const char *relative_path, bool empty_too) {
	if (!empty_too) {
		if (strlen(relative_path) == 0) {
			char *r = strdup(relative_path);
			alloc_assert(r);
			return r;
		}
	}
	if (relative_path[0] == '/') {
		char *r = realpath(relative_path, NULL);
		//alloc_assert(r);
		return r;
	}

	if (relative_path[0] == '~') {
		const char *home = getenv("HOME");
		char *r;
		if (home == NULL) return realpath(relative_path, NULL);

		asprintf(&r, "%s/%s", home, relative_path+1);
		alloc_assert(r);
		char *rr = realpath(r, NULL);
		free(r);

		return rr;
	} else {
		char *r;
		asprintf(&r, "%s/%s", basedir, relative_path);
		alloc_assert(r);
		char *rr = realpath(r, NULL);
		if (rr != NULL) {
			free(r);
			return rr;
		} else {
			return r;
		}
	}

	return NULL;
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

char *utf32_to_utf8_string(uint32_t *text, int len) {
	int allocated = 10;
	int cap = 0;
	char *r = malloc(sizeof(char) * allocated);
	alloc_assert(r);

	for (int i = 0; i < len; ++i) {
		utf32_to_utf8(text[i], &r, &cap, &allocated);
	}

	utf32_to_utf8(0, &r, &cap, &allocated);

	return r;
}

static void utf32_to_utf8_sizing(uint32_t code, int *first_byte_pad, int *first_byte_mask, int *inc) {
	if (code <= 0x7f) {
		*inc = 0;
		*first_byte_pad = 0x00;
		*first_byte_mask = 0x7f;
	} else if (code <= 0x7ff) {
		*inc = 1;
		*first_byte_pad = 0xc0;
		*first_byte_mask = 0x1f;
	} else if (code <= 0xffff) {
		*inc = 2;
		*first_byte_pad = 0xe0;
		*first_byte_mask = 0x0f;
	} else if (code <= 0x1fffff) {
		*inc = 3;
		*first_byte_pad = 0xf8;
		*first_byte_mask = 0x07;
	} else {
		*inc = 0;
		*first_byte_pad = '?';
		*first_byte_mask = 0x00;
	}
}

void utf32_to_utf8(uint32_t code, char **r, int *cap, int *allocated) {
	int first_byte_pad, first_byte_mask, inc;

	utf32_to_utf8_sizing(code, &first_byte_pad, &first_byte_mask, &inc);

	if (*cap+inc+1 >= *allocated) {
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

static char first_byte_result_to_mask[] = { 0xff, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x00, 0x00, 0x00 };

uint8_t utf8_first_byte_processing(uint8_t ch) {
	if (ch <= 127) return 0;

	if ((ch & 0xF0) == 0xF0) {
		if ((ch & 0x0C) == 0x0C) return 5;
		if ((ch & 0x08) == 0x08) return 4;
		if ((ch & 0x08) == 0x00) return 3;
		else return 8; // invalid sequence
	}

	if ((ch & 0xE0) == 0xE0) return 2;
	if ((ch & 0xC0) == 0xC0) return 1;
	if ((ch & 0x80) == 0x80) return 8; // invalid sequence

	return 8; // invalid sequence
}

int utf8_excision(char *buf, int n) {
	int end = n-8;
	if (end < 0) end = 0;
	for (int i = n-1; i >= end; --i) {
		if (utf8_first_byte_processing(buf[i]) != 8) return i;
	}
	return n-1;
}

uint32_t utf8_to_utf32(const char *text, int *src, int len, bool *valid) {
	uint32_t code;
	*valid = true;

	/* get next unicode codepoint in code, advance src */
	if ((uint8_t)text[*src] <= 127) {
		code = text[*src];
		++(*src);
		return code;
	}

	uint8_t tail_size = utf8_first_byte_processing(text[*src]);

	if (tail_size >= 8) {
		// unrecognized first byte, replace with a 0xfffd code
		//code = (uint8_t)text[*src];
		code = 0xfffd;
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

	// deconding ends here, following a list of checks for safe utf8 compliance.
	// while it may appear that some of this checks could be done before decoding that's not the case:
	// safe unicode compliance requires decoding invalid sequences and then discarding them as a single item

	if (i != tail_size) {
		// either end of text reached or we resynchronized early on a non continuation character
		// either way we got crap, replacing with a question mark
		code = 0xfffd;
		*valid = false;
		return code;
	}

	if (tail_size > 3) {
		// this was either an overlong sequence (ie. non-minimal representation of a codepoint)
		// or a representation of a codepoint beyond the last astral plane
		// either way it's invalid
		*valid = false;
		code = 0xfffd;
		return code;
	}

	if (((code&0x00ffff) == 0x00ffff) || ((code&0x00ffff) == 0x00fffe)) {
		// unicode non-characters
		*valid = false;
		code = 0xfffd;
		return code;
	}

	if ((code >= 0xfdd0) && (code <= 0xfdef)) {
		// this range of characters contains the "eye of the basilisk" character
		// this character effect is to instantly kill anyone who may happen to see it
		// because of this the entire range must be treated as invalid
		*valid = false;
		code = 0xfffd;
		return code;
	}

	if ((code >= 0xD800) && (code <= 0xDBFF)) {
		// high surrogates
		*valid = false;
		code = 0xfffd;
		return code;
	}

	if ((code >= 0xDC00) && (code <= 0xDFFF)) {
		// low surrogates
		*valid = false;
		code = 0xfffd;
		return code;
	}

	if (code != 0) {
		int first_byte_pad, first_byte_mask, inc;
		utf32_to_utf8_sizing(code, &first_byte_pad, &first_byte_mask, &inc);

		if (tail_size != inc) {
			// this is a overlong sequence (ie. non-minimal representation of a codepoint)
			// unicode (after 2.0) says they are unsafe
			// we only accept overlong sequences for 0x000000 because it is useful to represent 0x000000 non-minimally to simplify manipulation with C code (java calls it modified utf8)
			*valid = false;
			code = 0xfffd;
		}
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
	if (columnset == NULL) return false;

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
	uint32_t *r = malloc((len+1)*sizeof(uint32_t));
	alloc_assert(r);

	*dstlen = 0;
	for (int i = 0; i < len; ) {
		bool valid = true;
		r[(*dstlen)++] = utf8_to_utf32(text, &i, len, &valid);
	}

	r[*dstlen] = 0;

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

void gtk_widget_like_editor(config_t *config, GtkWidget *w) {
	GdkColor fg, bg, bg_selected;
	set_gdk_color_cfg(config, CFG_EDITOR_BG_COLOR, &bg);

	int bgc = config_intval(config, CFG_EDITOR_BG_COLOR);
	int clc = config_intval(config, CFG_EDITOR_BG_CURSORLINE);
	int selected_name = CFG_EDITOR_BG_CURSORLINE;
	if (bgc == clc) {
		selected_name = CFG_EDITOR_SEL_COLOR;
	}

	set_gdk_color_cfg(config, selected_name, &bg_selected);
	set_gdk_color_cfg(config, CFG_EDITOR_FG_COLOR, &fg);

	gtk_widget_modify_base(w, GTK_STATE_NORMAL, &bg);
	gtk_widget_modify_base(w, GTK_STATE_ACTIVE, &bg_selected);
	gtk_widget_modify_base(w, GTK_STATE_PRELIGHT, &bg_selected);
	gtk_widget_modify_base(w, GTK_STATE_SELECTED, &bg_selected);
	gtk_widget_modify_base(w, GTK_STATE_INSENSITIVE, &bg);

	gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &bg);
	gtk_widget_modify_bg(w, GTK_STATE_ACTIVE, &bg_selected);
	gtk_widget_modify_bg(w, GTK_STATE_PRELIGHT, &bg_selected);
	gtk_widget_modify_bg(w, GTK_STATE_SELECTED, &bg_selected);
	gtk_widget_modify_bg(w, GTK_STATE_INSENSITIVE, &bg);

	gtk_widget_modify_text(w, GTK_STATE_NORMAL, &fg);
	gtk_widget_modify_text(w, GTK_STATE_ACTIVE, &fg);
	gtk_widget_modify_text(w, GTK_STATE_PRELIGHT, &fg);
	gtk_widget_modify_text(w, GTK_STATE_SELECTED, &fg);
	gtk_widget_modify_text(w, GTK_STATE_INSENSITIVE, &fg);
}

void gtk_widget_modify_bg_all(GtkWidget *w, GdkColor *c) {
	gtk_widget_modify_bg(w, GTK_STATE_NORMAL, c);
	gtk_widget_modify_bg(w, GTK_STATE_ACTIVE, c);
	gtk_widget_modify_bg(w, GTK_STATE_PRELIGHT, c);
	gtk_widget_modify_bg(w, GTK_STATE_SELECTED, c);
	gtk_widget_modify_bg(w, GTK_STATE_INSENSITIVE, c);
}

char *session_directory(void) {
	char *sessiondir;
	char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home != NULL) {
		asprintf(&sessiondir, "%s", xdg_config_home);
	} else {
		asprintf(&sessiondir, "%s/.config/teddy", getenv("HOME"));
	}
	alloc_assert(sessiondir);

	return sessiondir;
}

static char *tied_session_file(void) {
	if (tied_session == NULL) return NULL;

	char *sessionfile;
	char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home != NULL) {
		asprintf(&sessionfile, "%s/%s.session", xdg_config_home, tied_session);
	} else {
		asprintf(&sessionfile, "%s/.config/teddy/%s.session", getenv("HOME"), tied_session);
	}
	alloc_assert(sessionfile);

	return sessionfile;
}

void save_tied_session(void) {
	char *sessionfile = tied_session_file();
	if (sessionfile == NULL) return;

	ipc_link_to(tied_session);

	FILE *f = fopen(sessionfile, "w");
	if (f == NULL) {
		free(tied_session);
		tied_session = NULL;
		quick_message("Session error", "Could not create file");
		free(sessionfile);
		return;
	}

	fprintf(f, "# %lld\n", (long long)time(NULL));

	fprintf(f, "teddy::stickdir\n");
	fprintf(f, "cd %s\n", top_working_directory());
	fprintf(f, "buffer closeall\n");
	fprintf(f, "setcfg -global main_font \"%s\"\n", config_strval(&global_config, CFG_MAIN_FONT));
	GList *column_list = gtk_container_get_children(GTK_CONTAINER(columnset));
	for (GList *column_cur = column_list; column_cur != NULL; column_cur = column_cur->next) {
		fprintf(f, "buffer column-setup %g", column_fraction(GTK_COLUMN(column_cur->data)));
		GList *frame_list = gtk_container_get_children(GTK_CONTAINER(column_cur->data));
		for (GList *frame_cur = frame_list; frame_cur != NULL; frame_cur = frame_cur->next) {
			tframe_t *frame = GTK_TFRAME(frame_cur->data);
			GtkWidget *cur_content = tframe_content(frame);
			if (!GTK_IS_TEDITOR(cur_content)) {
				fprintf(f, " %g +null+", tframe_fraction(frame));
			} else {
				editor_t *editor = GTK_TEDITOR(cur_content);
				fprintf(f, " %g {%s}", tframe_fraction(frame), editor->buffer->path);
			}
		}
		fprintf(f, "\n");
		g_list_free(frame_list);
	}
	g_list_free(column_list);

	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;

		if (buffers[i]->job !=NULL) {
			if (buffers[i]->wd != NULL) {
				fprintf(f, "in {%s} shell [buffer make {%s}] {%s}\n", buffers[i]->wd, buffers[i]->path, buffers[i]->job->command);
			} else {
				fprintf(f, "shell [buffer make {%s}] {%s}\n", buffers[i]->path, buffers[i]->job->command);
			}
		} else if (buffers[i]->path[0] == '+') {
			char *text = buffer_all_lines_to_text(buffers[i]);
			alloc_assert(text);

			if (strlen(text) < 2 * 1024 * 1024) {
				fprintf(f, "buffer eval [buffer find {%s}] { ", buffers[i]->path);
				const char *cargv[] = { "c", text };
				char *c = Tcl_Merge(2, cargv);
				fprintf(f, "%s", c);
				Tcl_Free(c);
				fprintf(f, " }\n");
			}

			free(text);

		}

		fprintf(f, "set b [buffer find {%s}]\n", buffers[i]->path);
		fprintf(f, "buffer eval $b { m ");
		if (buffers[i]->mark < 0) {
			fprintf(f, "=%d ", buffers[i]->mark);
		} else {
			fprintf(f, "nil ");
		}
		fprintf(f, "=%d", buffers[i]->cursor);
		fprintf(f, " }\n");
		fprintf(f, "buffer coc $b\n");
	}

	fclose(f);
}

void load_tied_session(void) {
	char *sessionfile = tied_session_file();
	if (sessionfile == NULL) return;

	ipc_link_to(tied_session);

	const char *argv[] = { "teddy_intl::loadsession", sessionfile };
	if (interp_eval_command(NULL, NULL, 2, argv) == NULL) {
		quick_message("Session error", "Could not load tied session file");
		free(tied_session);
		tied_session = NULL;
	}

	free(sessionfile);
}

void set_gdk_color_cfg(config_t *config, int name, GdkColor *c) {
	int color = config_intval(config, name);
	c->pixel = 0xff;
	c->red = (uint8_t)(color) / 255.0 * 65535;
	c->green = (uint8_t)(color >> 8) / 255.0 * 65535;
	c->blue = (uint8_t)(color >> 16) / 255.0 * 65535;
}

#define ROUNDBOXPAD 8
void roundbox(cairo_t *cr, GtkAllocation *allocation, const char *text) {
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);

	double y = 5;
	double x = allocation->width - (ext.x_advance + 2*ROUNDBOXPAD + 5);

	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.9);

	double a = x, b = x + ext.x_advance + 2 * ROUNDBOXPAD;
	double c = y, d = y + ext.height + 2 * ROUNDBOXPAD;

	double radius = ROUNDBOXPAD;
	double pi = 3.141592653589793;

	/*cairo_rectangle(cr, x, y, ext.x_advance + 2*ROUNDBOXPAD, ext.height + 2*ROUNDBOXPAD);
	cairo_fill(cr);*/

	cairo_arc(cr, a + radius, c + radius, radius, 2*(pi/2), 3*(pi/2));
    cairo_arc(cr, b - radius, c + radius, radius, 3*(pi/2), 4*(pi/2));
    cairo_arc(cr, b - radius, d - radius, radius, 0*(pi/2), 1*(pi/2));
    cairo_arc(cr, a + radius, d - radius, radius, 1*(pi/2), 2*(pi/2));
    cairo_line_to(cr, a, c+radius);
    cairo_close_path(cr);

	cairo_fill(cr);

	cairo_move_to(cr, x+ROUNDBOXPAD, y+ext.height+ROUNDBOXPAD);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_show_text(cr, text);
}
