/* reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes */
#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

FT_Library library;
FT_Face face;
cairo_font_face_t *cairoface;
GtkObject *adjustment;
GtkWidget *drar;
int tab_width = 4;

typedef struct _line_t {
    char *text;
    int allocated_text;
    int text_cap;
} line_t;

line_t *lines;
int allocated_lines;
int lines_cap;

void init_line(line_t *line) {
    line->text = NULL;
    line->allocated_text = 0;
    line->text_cap = 0;
}

void init_lines() {
    int i = 0;
    allocated_lines = 10;
    lines = malloc(allocated_lines * sizeof(line_t));
    lines_cap = 0;
    if (!lines) {
        perror("lines allocation failed");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < allocated_lines; ++i) {
        init_line(lines+i);
    }
}

void grow_lines() {
    int new_allocated_lines = allocated_lines * 2;
    int i;
    lines = realloc(lines, new_allocated_lines * sizeof(line_t));
    if (!lines) {
        perror("lines allocation failed");
        exit(EXIT_FAILURE);
    }
    for (i = allocated_lines; i < new_allocated_lines; ++i) {
        init_line(lines+i);
    }
    allocated_lines = new_allocated_lines;
}

void grow_line(line_t *line) {
    if (line->allocated_text == 0) {
        line->allocated_text = 10;
    } else {
        line->allocated_text *= 2;
    }

    line->text = realloc(line->text, line->allocated_text * sizeof(char));
}

/* TODO:
   - create glyphs at load time
   - horizontal scrollbar
   - cursor positioning
   - editing
   - drawing header
*/

uint8_t first_byte_processing(uint8_t ch) {
    if (ch <= 127) return ch;

    if ((ch & 0xF0) == 0xF0) {
        if ((ch & 0x0E) == 0x0E) return ch & 0x01;
        if ((ch & 0x0C) == 0x0C) return ch & 0x03;
        if ((ch & 0x08) == 0x08) return ch & 0x07;
        return ch & 0x0F;
    }

    if ((ch & 0xE0) == 0xE0) return ch & 0x1F;
    if ((ch & 0xC0) == 0xC0) return ch & 0x3F;
    if ((ch & 0x80) == 0x80) return ch & 0x7F;

    return ch;
}

gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    int i;
    double y ;
    cairo_font_extents_t font_extents;
    cairo_glyph_t *glyphs;
    int allocated_glyphs = 10;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);

    glyphs = malloc(allocated_glyphs*sizeof(cairo_glyph_t));
    if (!glyphs) {
        perror("Glyph space allocation failed");
        exit(EXIT_FAILURE);
    }

    cairo_set_source_rgb(cr, 0, 0, 255);
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_set_font_face(cr, cairoface);
    cairo_set_font_size(cr, 16);

    cairo_scaled_font_extents(cairo_get_scaled_font(cr), &font_extents);

    y = font_extents.height - gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment));

    /*printf("DRAWING!\n");*/

    for (i = 0; i < lines_cap; ++i) {
        FT_Face scaledface = cairo_ft_scaled_font_lock_face(cairo_get_scaled_font(cr));
        int src, dst;
        double x = 5.0; /* left margin */
        FT_Bool use_kerning = FT_HAS_KERNING(scaledface);
        FT_UInt previous = 0;
        char *text = lines[i].text;
        int initial_spaces = 1;
        double em_advance;

        if (y - font_extents.height > allocation.height) break; /* if we passed the visible area the just stop displaying */
        if (y < 0) { /* If the line is before the visible area, just increment y and move to the next line */
            y += font_extents.height;
            continue;
        }
        
        {
            cairo_text_extents_t em_extents;
            cairo_text_extents(cr, "M", &em_extents);
            em_advance = em_extents.width;
        }

        for (src = 0, dst = 0; src < lines[i].text_cap;) {
            uint32_t code;
            FT_UInt glyph_index;
            cairo_text_extents_t extents;

            /* get next unicode codepoint in code, advance src */
            if ((uint8_t)text[src] > 127) {
                code = first_byte_processing(text[src]);
                ++src;

                for (; ((uint8_t)text[src] > 127) && (src < lines[i].text_cap); ++src) {
                    code <<= 6;
                    code += (text[src] & 0x3F);
                }
            } else {
                code = text[src];
                ++src;
            }

            if (code != 0x09) {
                glyph_index = FT_Get_Char_Index(scaledface, code);
            } else {
                glyph_index = FT_Get_Char_Index(scaledface, 0x20);
            }

            /* Kerning correction for x */
            if (use_kerning && previous && glyph_index) {
                FT_Vector delta;

                FT_Get_Kerning(scaledface, previous, glyph_index, FT_KERNING_DEFAULT, &delta);

                x += delta.x >> 6;
            }

            if (dst >= allocated_glyphs) {
                allocated_glyphs *= 2;
                glyphs = realloc(glyphs, allocated_glyphs*sizeof(cairo_glyph_t));
            }
  
            previous = glyphs[dst].index = glyph_index;
            glyphs[dst].x = x;
            glyphs[dst].y = y;
            cairo_glyph_extents(cr, glyphs+dst, 1, &extents);

            /* Fix x_advance accounting for special treatment of indentation and special treatment of tabs */
            if (initial_spaces) {
                if (code == 0x20) {
                    extents.x_advance = em_advance;
                } else if (code == 0x09) {
                    extents.x_advance = em_advance * tab_width;
                } else {
                    initial_spaces = 0;
                }
            } else {
                if (code == 0x09) {
                    extents.x_advance *= tab_width;
                }
            }

            if (x + extents.x_advance >= 0) { /* only show visible glyphs */
                if (glyphs[dst].x > allocation.width) {
                    /*printf("Bailing out of drawing %g > %d (%d)\n", glyphs[dst].x, allocation.width, dst);*/
                    ++dst;
                    break;
                }
                ++dst;
            } else {
                /*printf("Hiding invisible glyph: %g %g (%d) (idx: %d) (code: %u) (%g,%g)\n", x, extents.x_advance, dst, glyph_index, code, glyphs[dst].x, glyphs[dst].y);*/
            }

            x += extents.x_advance;
        }

        cairo_show_glyphs(cr, glyphs, dst);
        y += font_extents.height;
    }

    gtk_adjustment_set_upper(GTK_ADJUSTMENT(adjustment), (lines_cap+1) * font_extents.height);
    gtk_adjustment_set_page_size(GTK_ADJUSTMENT(adjustment), allocation.height);
    gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(adjustment), allocation.height/2);

    cairo_ft_scaled_font_unlock_face(cairo_get_scaled_font(cr));  
    cairo_destroy(cr);
    free(glyphs);
  
    return TRUE;
}

gboolean scrolled_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    printf("Scrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)));
    gtk_widget_queue_draw(drar);
    return TRUE;
}

void load_text_file(const char *filename) {
    FILE *fin = fopen(filename, "r");
    char ch;
    if (!fin) {
        perror("Couldn't open input file");
        exit(EXIT_FAILURE);
    }

    while ((ch = fgetc(fin)) != EOF) {
        if (lines_cap >= allocated_lines) {
            grow_lines();
        }
        if (ch == '\n') {
            ++lines_cap;
        } else {
            if (lines[lines_cap].text_cap >= lines[lines_cap].allocated_text) {
                grow_line(lines+lines_cap);
            }
            lines[lines_cap].text[lines[lines_cap].text_cap] = ch;
            ++(lines[lines_cap].text_cap);
        }
    }

    fclose(fin);
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    int error;

    gtk_init(&argc, &argv);

    if (argc <= 1) {
        printf("Nothing to show\n");
        exit(EXIT_SUCCESS);
    }

    printf("Will show: %s\n", argv[1]);

    init_lines();
    load_text_file(argv[1]);

    error = FT_Init_FreeType(&library);

    if (error) {
        printf("Freetype initialization error\n");
        exit(EXIT_FAILURE);
    }

    error = FT_New_Face(library, "/usr/share/fonts/truetype/msttcorefonts/arial.ttf", 0, &face);

    if (error) {
        printf("Error loading freetype font\n");
        exit(EXIT_FAILURE);
    }

    cairoface = cairo_ft_font_face_create_for_ft_face(face, 0);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "Acmacs");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 600);

    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    drar = gtk_drawing_area_new();

    g_signal_connect(G_OBJECT(drar), "expose_event",
                     G_CALLBACK(expose_event_callback), NULL);

    {
        GtkWidget *drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        GtkWidget *hbox = gtk_hbox_new(FALSE, 1);

        gtk_container_add(GTK_CONTAINER(hbox), drar);
        gtk_container_add(GTK_CONTAINER(hbox), drarscroll);

        gtk_box_set_child_packing(GTK_BOX(hbox), drar, TRUE, TRUE, 0, GTK_PACK_START);
        gtk_box_set_child_packing(GTK_BOX(hbox), drarscroll, FALSE, FALSE, 0, GTK_PACK_END);

        gtk_container_add(GTK_CONTAINER(window), hbox);

        g_signal_connect_swapped(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), NULL);
    }

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}
