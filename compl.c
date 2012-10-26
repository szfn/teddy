#include "compl.h"

#include "global.h"
#include "top.h"
#include "buffers.h"
#include "tags.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <unicode/uchar.h>


static gboolean compl_wnd_expose_callback(GtkWidget *widget, GdkEventExpose *event, struct completer *c) {
	GtkAllocation allocation;

	gtk_widget_get_allocation(c->window, &allocation);

	if (allocation.width > 640) {
		allocation.width = 640;
		gtk_widget_set_allocation(c->window, &allocation);
	}

	gint wpos_x, wpos_y;
	gdk_window_get_position(gtk_widget_get_window(c->window), &wpos_x, &wpos_y);

	double x = wpos_x + allocation.width, y = wpos_y + allocation.height;

	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *screen = gdk_display_get_default_screen(display);

	gint screen_width = gdk_screen_get_width(screen);
	gint screen_height = gdk_screen_get_height(screen);

	//printf("bottom right corner (%g, %g) screen width: %d height: %d\n", x, y, screen_width, screen_height);

	if (x > screen_width) {
		gtk_window_move(GTK_WINDOW(c->window), allocation.x + screen_width - x - 5, allocation.y);
	}

	if (y > screen_height) {
		gtk_window_move(GTK_WINDOW(c->window), allocation.x, c->alty - allocation.height);
	}

	return FALSE;
}

void compl_init(struct completer *c) {
	c->cbt.root = NULL;
	c->list = gtk_list_store_new(1, G_TYPE_STRING);
	c->tree = gtk_tree_view_new();
	c->common_suffix = NULL;
	c->prefix_from_buffer = &buffer_wordcompl_word_at_cursor;
	c->recalc = NULL;
	c->tmpdata = NULL;

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(c->tree), -1, "Completion", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(c->tree), GTK_TREE_MODEL(c->list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(c->tree), FALSE);

	c->window = gtk_window_new(GTK_WINDOW_POPUP);

	gtk_window_set_decorated(GTK_WINDOW(c->window), FALSE);

	g_signal_connect(G_OBJECT(c->window), "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(G_OBJECT(c->tree), "expose_event", G_CALLBACK(compl_wnd_expose_callback), c);

	{
		GtkWidget *frame = gtk_table_new(0, 0, FALSE);

		gtk_container_add(GTK_CONTAINER(c->window), frame);

		place_frame_piece(frame, TRUE, 0, 3); // top frame
		place_frame_piece(frame, FALSE, 0, 3); // left frame
		place_frame_piece(frame, FALSE, 2, 3); // right frame
		place_frame_piece(frame, TRUE, 2, 3); // bottom frame

		GtkWidget *scroll_view = gtk_scrolled_window_new(NULL, NULL);

		gtk_container_add(GTK_CONTAINER(scroll_view), c->tree);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_view), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		gtk_table_attach(GTK_TABLE(frame), scroll_view, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	}

	gtk_window_set_default_size(GTK_WINDOW(c->window), -1, 150);
}

void compl_reset(struct completer *c) {
	if (c == NULL) return;
	critbit0_clear(&(c->cbt));
}

void compl_add(struct completer *c, const char *text) {
	if (c == NULL) return;
	critbit0_insert(&(c->cbt), text);
}

char *compl_complete(struct completer *c, const char *prefix) {
	if (c == NULL) return NULL;
	if (c->recalc != NULL) prefix = c->recalc(c, prefix);
	//printf("Completing* <%s>\n", prefix);
	char *r = critbit0_common_suffix_for_prefix(&(c->cbt), prefix);
	utf8_remove_truncated_characters_at_end(r);
	return r;
}

static int compl_wnd_fill_callback(const char *entry, void *p) {
	struct completer *c = (struct completer *)p;
	GtkTreeIter mah;
	gtk_list_store_append(c->list, &mah);
	gtk_list_store_set(c->list, &mah, 0, entry, -1);
	++(c->size);
	return 1;
}

void compl_wnd_show(struct completer *c, const char *prefix, double x, double y, double alty, GtkWidget *parent, bool show_empty, bool show_empty_prefix) {
	if (c == NULL) return;
	if (c->recalc != NULL) prefix = c->recalc(c, prefix);

	//printf("Completing <%s>\n", prefix);

	c->size = 0;
	c->alty = alty;

	if (!show_empty_prefix) {
		if (strcmp(prefix, "") == 0) {
			compl_wnd_hide(c);
			return;
		}
	}

	gtk_list_store_clear(c->list);
	critbit0_allprefixed(&(c->cbt), prefix, compl_wnd_fill_callback, (void *)c);

	if (!show_empty) {
		if (c->size == 0) {
			compl_wnd_hide(c);
			return;
		}
	}

	if (c->common_suffix != NULL) {
		free(c->common_suffix);
		c->common_suffix = NULL;
	}

	c->common_suffix = critbit0_common_suffix_for_prefix(&(c->cbt), prefix);
	if ((c->common_suffix != NULL) && (strcmp(c->common_suffix, "") == 0)) {
		free(c->common_suffix);
		c->common_suffix = NULL;
	}

	gtk_window_set_transient_for(GTK_WINDOW(c->window), GTK_WINDOW(parent));

	x += 2; y += 2;

	gtk_widget_set_uposition(c->window, x, y);

	{
		GtkTreePath *path_to_first = gtk_tree_path_new_first();
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path_to_first, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);
		gtk_tree_path_free(path_to_first);
	}

	gtk_widget_show_all(c->window);
	c->visible = true;
	c->prefix_len = strlen(prefix);

	//printf("autocompletion %p shown\n", c);
}

void compl_wnd_up(struct completer *c) {
	if (c == NULL) return;
	GtkTreePath *path;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	} else {
		if (!gtk_tree_path_prev(path)) {
			gtk_tree_path_free(path);
			path = gtk_tree_path_new_from_indices(c->size-1, -1);
		}
	}

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);
	gtk_tree_path_free(path);
}

void compl_wnd_down(struct completer *c) {
	if (c == NULL) return;
	GtkTreePath *path;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	} else {
		gtk_tree_path_next(path);
	}

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);

	gtk_tree_path_free(path);

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &path, NULL);
	if (path == NULL) {
		path = gtk_tree_path_new_first();
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);
	}

	gtk_tree_path_free(path);
}

char *compl_wnd_get(struct completer *c, bool all) {
	if (c == NULL) return NULL;
	GtkTreePath *focus_path;
	GtkTreeIter iter;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &focus_path, NULL);
	if (focus_path == NULL) {
		gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(c->list), &iter);
		if (!valid) return NULL;
	} else {
		gboolean valid = gtk_tree_model_get_iter(GTK_TREE_MODEL(c->list), &iter, focus_path);
		if (!valid) return NULL;
	}

	GValue value = {0};
	gtk_tree_model_get_value(GTK_TREE_MODEL(c->list), &iter, 0, &value);
	const char *pick = g_value_get_string(&value);
	if (focus_path != NULL) {
		gtk_tree_path_free(focus_path);
	}

	if (c->prefix_len >= strlen(pick)) {
		return NULL;
	}

	char *r = strdup(all ? pick : (pick+c->prefix_len));
	alloc_assert(r);

	g_value_unset(&value);

	return r;
}

void compl_wnd_hide(struct completer *c) {
	if (c == NULL) return;
	if (!(c->visible)) return;
	gtk_widget_hide(c->window);
	c->visible = false;
	if (c->common_suffix != NULL) {
		free(c->common_suffix);
		c->common_suffix = NULL;
	}

	//printf("autocompletion %p hidden\n", c);
}

bool compl_wnd_visible(struct completer *c) {
	if (c == NULL) return false;
	return c->visible;
}

void compl_free(struct completer *c) {
	if (c == NULL) return;
	critbit0_clear(&(c->cbt));
	if (c->common_suffix != NULL) {
		free(c->common_suffix);
		c->common_suffix = NULL;
	}
	if (c->tmpdata != NULL) free(c->tmpdata);
}

int compl_wnd_size(struct completer *c) {
	if (c == NULL) return 0;
	return c->size;
}

const char *list_internal_commands[] = {
	// tcl default commands (and redefined tcl default commands)
	"error", "lappend", "platform",
	"append",	"eval",	"lassign",	"platform::shell",
	"apply",	"exec",	"lindex",	"proc",
	"array",	"exit",	"linsert",	"puts",
	"auto_execok",	"expr",	"list",	"pwd",
	"auto_import",	"fblocked",	"llength",	"re_syntax",	"tcltest",
	"auto_load",	"fconfigure",	"load",	"read",	"tclvars",
	"auto_mkindex",	"fcopy",	"lrange",	"refchan",	"tell",
	"auto_mkindex_old",	"file",	"lrepeat",	"regexp",	"time",
	"auto_qualify",	"fileevent",	"lreplace",	"registry",	"tm",
	"auto_reset",	"filename",	"lreverse",	"regsub",	"trace",
	"bgerror",	"flush",	"lsearch",	"rename",	"unknown",
	"binary",	"for",	"lset",	"return",	"unload",
	"break",	"foreach",	"lsort",	"unset",
	"catch",	"format",	"mathfunc",	"scan",	"update",
	"gets",	"mathop",	"seek",	"uplevel",
	"chan",	"glob",	"memory",	"set",	"upvar",
	"clock",	"global",	"msgcat",	"socket",	"variable",
	"close",	"history",	"namespace",	"source",	"vwait",
	"concat",	"http",	"open",	"split",	"while",
	"continue",	"if",	"package",	"string",
	"dde",	"incr",	"parray",	"subst",
	"dict",	"info",	"pid",	"switch",
	"encoding",	"interp",	"pkg::create",	"Tcl",
	"eof",	"join",	"pkg_mkIndex",

	// special commands
	"setcfg", "bindkey", "new", "pwf", "pwd", "go", "mark", "cb", "save",
	"bufman", "undo", "search", "focuscmd", "move", "gohome", "bg", "<",
	"rgbcolor", "teddyhistory", "interactarg", "s", "c", "cursor", "bindent",
	"teddy-hack-resize", "kill", "refresh", "buffer", "load",

	// lexy
	"lexydef-create", "lexydef-append", "lexassoc", "lexycfg",

	// debug commands
	"wordcompl_dump", "lexy_dump"
};

const char **external_commands = NULL;
int external_commands_cap;
int external_commands_allocated;

void cmdcompl_init(void) {
	char *path, *saveptr, *dir;

	external_commands_allocated = 10;
	external_commands_cap = 0;
	external_commands = malloc(sizeof(const char *) * external_commands_allocated);
	alloc_assert(external_commands);

	/*** Getting all executable names ***/

	asprintf(&path, "%s", getenv("PATH"));

	for (dir = strtok_r(path, ":", &saveptr); dir != NULL; dir = strtok_r(NULL, ":", &saveptr)) {
		DIR *dh = opendir(dir);
		if (dh == NULL) continue;
		struct dirent *den;
		for (den = readdir(dh); den != NULL; den = readdir(dh)) {
			struct stat den_stat;

			if ((den->d_type == DT_REG) || (den->d_type == DT_LNK)) {
				char *den_path;
				asprintf(&den_path, "%s/%s", dir, den->d_name);
				alloc_assert(den_path);

				memset(&den_stat, 0, sizeof(den_stat));
				if (stat(den_path, &den_stat) == 0) {
					if (S_ISREG(den_stat.st_mode) && (den_stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
						if (external_commands_cap >= external_commands_allocated) {
							external_commands_allocated *= 2;
							external_commands = realloc(external_commands, sizeof(const char *) * external_commands_allocated);
							alloc_assert(external_commands);
						}
						char *r = strdup(den->d_name);
						alloc_assert(r);
						external_commands[external_commands_cap++] = r;
					}
				}

				free(den_path);
			}
		}
		closedir(dh);
	}

	free(path);
}

static void load_directory_completions(struct completer *c, DIR *dh, const char *reldir) {
	if (dh == NULL) return;
	struct dirent *den;
	for (den = readdir(dh); den != NULL; den = readdir(dh)) {
		if (den->d_name[0] != '.') {
			char *relname;
			asprintf(&relname, "%s%s%s%s", reldir, (reldir[0] != '\0') ? "/" : "", den->d_name, (den->d_type == DT_DIR) ? "/" : "");
			alloc_assert(relname);
			//printf("\tAdding <%s>\n", relname);
			critbit0_insert(&(c->cbt), relname);
			free(relname);
		}
	}
}

static int refill_word_completer(const char *entry, void *p) {
	compl_add(&the_word_completer, entry);
	return 1;
}

static void load_command_completions(struct completer *c) {
	for (int i = 0; i < sizeof(list_internal_commands) / sizeof(const char *); ++i) {
		compl_add(c, list_internal_commands[i]);
	}

	for (int i = 0; i < external_commands_cap; ++i) {
		compl_add(c, external_commands[i]);
	}
}

static void load_word_completions(struct completer *c) {
	for (int i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;

		critbit0_allprefixed(&(buffers[i]->cbt), "", refill_word_completer, NULL);
	}

	critbit0_allprefixed(&closed_buffers_critbit, "", refill_word_completer, NULL);
	critbit0_allprefixed(&tags_file_critbit, "", refill_word_completer, NULL);
}

static bool cmdcompl_recalc_with(struct completer *c, char *reldir) {
	bool ret = true;
	char *absdir = unrealpath(reldir, true);

	//printf("Recalculating with <%s> -> <%s>\n", reldir, absdir);
	
	DIR *dh = (absdir != NULL) ? opendir(absdir) : NULL;
	if (dh == NULL) {
		if (strcmp(reldir, "") != 0) {
			free(reldir);
			reldir = strdup("");
			alloc_assert(reldir);
		}

		free(absdir);
		absdir = unrealpath(reldir, true);

		if (absdir != NULL) dh = opendir(absdir);
		ret = false;
	}

	critbit0_clear(&(c->cbt));

	load_command_completions(c);
	load_directory_completions(c, dh, reldir);
	load_word_completions(c);

	free(c->tmpdata);
	c->tmpdata = reldir;
	if (dh != NULL) closedir(dh);
	free(absdir);
	return ret;
}

static const char *trim_prefix(const char *prefix) {
	int len = 1;
	for (int i = strlen(prefix)-1; i >= 0; --i) {
		if ((i & 0xc0) != 0x80) {
			int src = 0;
			bool valid = false;
			uint32_t code = utf8_to_utf32(prefix+i, &src, len, &valid);
			if (!valid) return prefix+i+len;
			if (!u_isalnum(code) && (code != '_') && (code != '.')) return prefix+i+len;
			len = 1;
		} else {
			++len;
		}
	}

	return prefix;
}

const char *cmdcompl_recalc(struct completer *c, const char *prefix) {
	char *last_slash = strrchr(prefix, '/');

	// there is no slash, reset directory calculation and trim prefix
	if (last_slash == NULL) {
		if (strcmp(c->tmpdata, "") != 0) {
			cmdcompl_recalc_with(c, strdup(""));
		}
		return trim_prefix(prefix);
	}

	// we already have completions for this directory continue like this
	if ((strlen(c->tmpdata) == strlen(prefix) - strlen(last_slash)) && (strncmp(c->tmpdata, prefix, strlen(prefix) - strlen(last_slash)) == 0)) {
		return prefix;
	}

	*last_slash = '\0';
	char *reldir = strdup(prefix);
	alloc_assert(reldir);
	*last_slash = '/';

	if (cmdcompl_recalc_with(c, reldir)) {
		return prefix;
	} else {
		return trim_prefix(prefix);
	}
}

bool in_external_commands(const char *arg) {
	for (int i = 0; i < external_commands_cap; ++i) {
		if (strcmp(arg, external_commands[i]) == 0) return true;
	}

	if (access(arg, F_OK) == 0) return true;

	char *urp = unrealpath(arg, false);
	if (urp == NULL) return false;
	bool r = (access(urp, F_OK) == 0);
	free(urp);
	return r;
}

void word_completer_full_update(void) {
	char *absdir = unrealpath(the_word_completer.tmpdata, true);

	DIR *dh = (absdir != NULL) ? opendir(absdir) : NULL;
	critbit0_clear(&(the_word_completer.cbt));

	load_command_completions(&the_word_completer);
	load_directory_completions(&the_word_completer, dh, the_word_completer.tmpdata);
	load_word_completions(&the_word_completer);

	if (dh != NULL) closedir(dh);
	free(absdir);
}
