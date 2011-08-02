#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <stdint.h>

FT_Library library;
FT_Face face;
cairo_font_face_t *cairoface;

char *blap[] = { "T. L. WÀfWAfanculò", "ProoooooooOOOOOOOOooooooOOOOOOOooooooottttt", "blap" };

/* TODO:
   - caricare da un file esterno
   - non scrivere oltre (o prima) l'area disegnabile    
   - scrollbars
   - trucchetto degli spazi iniziali
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

gboolean expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data) {
  cairo_t *cr = gdk_cairo_create(widget->window);
  int i;
  double y ;
  cairo_font_extents_t font_extents;
  cairo_glyph_t *glyphs;
  int allocated_glyphs = 10;

  glyphs = malloc(allocated_glyphs*sizeof(cairo_glyph_t));
  if (!glyphs) {
    perror("Glyph space allocation failed");
    exit(EXIT_FAILURE);
  }

  cairo_set_source_rgb(cr, 255, 255, 255);
  cairo_set_font_face(cr, cairoface);
  cairo_set_font_size(cr, 40);

  cairo_scaled_font_extents(cairo_get_scaled_font(cr), &font_extents);

  y = font_extents.height;

  for (i = 0; i < 3; ++i) {
    FT_Face scaledface = cairo_ft_scaled_font_lock_face(cairo_get_scaled_font(cr));
    int src, dst;
    double x = 5.0; /* left margin */
    FT_Bool use_kerning = FT_HAS_KERNING(scaledface);
    FT_UInt previous = 0;

    for (src = 0, dst = 0; src < strlen(blap[i]); ++dst) {
      cairo_text_extents_t extents;
      uint32_t code;
      FT_UInt glyph_index;

      if ((uint8_t)blap[i][src] > 127) {
	printf("first byte\n");
	code = first_byte_processing(blap[i][src]);
	++src;

	for (; ((uint8_t)blap[i][src] > 127) && (src < strlen(blap[i])); ++src) {
	  code <<= 6;
	  code += (blap[i][src] & 0x3F);
	}
      } else {
	code = blap[i][src];
	++src;
      }

      glyph_index = FT_Get_Char_Index(scaledface, code);

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
      cairo_glyph_extents(cr, glyphs+dst, 1, &extents);
      glyphs[dst].x = x;
      x += extents.x_advance;
      glyphs[dst].y = y;
    }
    
    cairo_show_glyphs(cr, glyphs, strlen(blap[i]));
    y += font_extents.height;
  }

  cairo_ft_scaled_font_unlock_face(cairo_get_scaled_font(cr));  
  cairo_destroy(cr);
  free(glyphs);
  
  return TRUE;
}

int main(int argc, char *argv[]) {
  GtkWidget *window, *drar;
  int error;

  gtk_init(&argc, &argv);

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
  gtk_widget_set_size_request(drar, 100, 100);
  gtk_container_add(GTK_CONTAINER(window), drar);

  g_signal_connect(G_OBJECT(drar), "expose_event",
		   G_CALLBACK(expose_event_callback), NULL); 

  gtk_widget_show_all(window);

  gtk_main();

  return 0;
}
