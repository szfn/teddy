#include "buffer.h"

#include <stdint.h>

static void init_line(line_t *line) {
    line->text = NULL;
    line->allocated_text = 0;
    line->text_cap = 0;

    line->allocated_glyphs = 10;
    line->glyphs = malloc(sizeof(cairo_glyph_t) * line->allocated_glyphs);
    line->glyph_info = malloc(sizeof(my_glyph_info_t) * line->allocated_glyphs);
    /* TODO: check for successful allocation */
    line->glyphs_cap = 0;
}

static uint8_t utf8_first_byte_processing(uint8_t ch) {
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

static void line_recalculate_glyphs(buffer_t *buffer, int line_idx) {
    FT_Face scaledface = cairo_ft_scaled_font_lock_face(buffer->cairofont);
    FT_Bool use_kerning = FT_HAS_KERNING(scaledface);
    char *text = buffer->lines[line_idx].text;
    FT_UInt previous = 0;
    int initial_spaces = 1;
    int src, dst;

    for (src = 0, dst = 0; src < buffer->lines[line_idx].text_cap; ) {
        uint32_t code;
        FT_UInt glyph_index;
        cairo_text_extents_t extents;

        /* get next unicode codepoint in code, advance src */
        if ((uint8_t)text[src] > 127) {
            code = utf8_first_byte_processing(text[src]);
            ++src;
            
            for (; ((uint8_t)text[src] > 127) && (src < buffer->lines[line_idx].text_cap); ++src) {
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

        if (dst >= buffer->lines[line_idx].allocated_glyphs) {
            buffer->lines[line_idx].allocated_glyphs *= 2;
            buffer->lines[line_idx].glyphs = realloc(buffer->lines[line_idx].glyphs, sizeof(cairo_glyph_t) * buffer->lines[line_idx].allocated_glyphs);
            buffer->lines[line_idx].glyph_info = realloc(buffer->lines[line_idx].glyph_info, sizeof(my_glyph_info_t) * buffer->lines[line_idx].allocated_glyphs);
            /* TODO: check that allocation is successful */
        }

        /* Kerning correction for x */
        if (use_kerning && previous && glyph_index) {
            FT_Vector delta;
            
            FT_Get_Kerning(scaledface, previous, glyph_index, FT_KERNING_DEFAULT, &delta);

            buffer->lines[line_idx].glyph_info[dst].kerning_correction = delta.x >> 6;
        } else {
            buffer->lines[line_idx].glyph_info[dst].kerning_correction = 0;
        }
        
        previous = buffer->lines[line_idx].glyphs[dst].index = glyph_index;
        buffer->lines[line_idx].glyphs[dst].x = 0.0;
        buffer->lines[line_idx].glyphs[dst].y = 0.0;

        cairo_scaled_font_glyph_extents(buffer->cairofont, buffer->lines[line_idx].glyphs + dst, 1, &extents);
        
        /* Fix x_advance accounting for special treatment of indentation and special treatment of tabs */
        if (initial_spaces) {
            if (code == 0x20) {
                extents.x_advance = buffer->em_advance;
            } else if (code == 0x09) {
                extents.x_advance = buffer->em_advance * buffer->tab_width;
            } else {
                initial_spaces = 0;
            }
        } else {
            if (code == 0x09) {
                extents.x_advance *= buffer->tab_width;
            }
        }
        
        buffer->lines[line_idx].glyph_info[dst].x_advance = extents.x_advance;
        ++dst;
    }

    buffer->lines[line_idx].glyphs_cap = dst;

    cairo_ft_scaled_font_unlock_face(buffer->cairofont);
}

void buffer_line_adjust_glyphs(buffer_t *buffer, int line_idx, double x, double y) {
    cairo_glyph_t *glyphs = buffer->lines[line_idx].glyphs;
    my_glyph_info_t *glyph_info = buffer->lines[line_idx].glyph_info;
    int glyphs_cap = buffer->lines[line_idx].glyphs_cap;
    int i;

    for (i = 0; i < glyphs_cap; ++i) {
        x += glyph_info[i].kerning_correction;
        glyphs[i].x = x;
        glyphs[i].y = y;
        x += glyph_info[i].x_advance;
    }

    return;
}

static void init_lines(buffer_t *buffer) {
    int i = 0;
    buffer->allocated_lines = 10;
    buffer->lines = malloc(buffer->allocated_lines * sizeof(line_t));
    buffer->lines_cap = 0;
    if (!(buffer->lines)) {
        perror("lines allocation failed");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < buffer->allocated_lines; ++i) {
        init_line(buffer->lines+i);
    }
}

static void grow_lines(buffer_t *buffer) {
    int new_allocated_lines = buffer->allocated_lines * 2;
    int i;
    buffer->lines = realloc(buffer->lines, new_allocated_lines * sizeof(line_t));
    if (!(buffer->lines)) {
        perror("lines allocation failed");
        exit(EXIT_FAILURE);
    }
    for (i = buffer->allocated_lines; i < new_allocated_lines; ++i) {
        init_line(buffer->lines+i);
    }
    buffer->allocated_lines = new_allocated_lines;
}

static void grow_line(line_t *line) {
    if (line->allocated_text == 0) {
        line->allocated_text = 10;
    } else {
        line->allocated_text *= 2;
    }

    line->text = realloc(line->text, line->allocated_text * sizeof(char));
}

void load_text_file(buffer_t *buffer, const char *filename) {
    FILE *fin = fopen(filename, "r");
    char ch;
    int last_was_newline = 0;
    
    if (!fin) {
        perror("Couldn't open input file");
        exit(EXIT_FAILURE);
    }

    if (buffer->lines_cap >= buffer->allocated_lines) {
        grow_lines(buffer);
    }

    while ((ch = fgetc(fin)) != EOF) {
        if (ch == '\n') {
            last_was_newline = 1;
            line_recalculate_glyphs(buffer, buffer->lines_cap);
            ++(buffer->lines_cap);
            if (buffer->lines_cap >= buffer->allocated_lines) {
                grow_lines(buffer);
            }
        } else {
            last_was_newline = 0;
            if (buffer->lines[buffer->lines_cap].text_cap >= buffer->lines[buffer->lines_cap].allocated_text) {
                grow_line(buffer->lines+buffer->lines_cap);
            }
            buffer->lines[buffer->lines_cap].text[buffer->lines[buffer->lines_cap].text_cap] = ch;
            ++(buffer->lines[buffer->lines_cap].text_cap);
        }
    }

    if (!last_was_newline) {
        ++(buffer->lines_cap);
    }

    fclose(fin);
}

buffer_t *buffer_create(FT_Library *library) {
    buffer_t *buffer = malloc(sizeof(buffer_t));
    int error;

    error = FT_New_Face(*library, "/usr/share/fonts/truetype/msttcorefonts/arial.ttf", 0, &(buffer->face));
    if (error) {
        printf("Error loading freetype font\n");
        exit(EXIT_FAILURE);
    }

    buffer->cairoface = cairo_ft_font_face_create_for_ft_face(buffer->face, 0);

    cairo_matrix_init(&(buffer->font_size_matrix), 16, 0, 0, 16, 0, 0);
    cairo_matrix_init(&(buffer->font_ctm), 1, 0, 0, 1, 0, 0);
    buffer->font_options = cairo_font_options_create();

    buffer->cairofont = cairo_scaled_font_create(buffer->cairoface, &(buffer->font_size_matrix), &(buffer->font_ctm), buffer->font_options);
    
    {
        cairo_text_extents_t em_extents;
        cairo_font_extents_t font_extents;
        
        cairo_scaled_font_text_extents(buffer->cairofont, "M", &em_extents);
        buffer->em_advance = em_extents.width;

        cairo_scaled_font_extents(buffer->cairofont, &font_extents);
        buffer->line_height = font_extents.height;
    }

    init_lines(buffer);

    buffer->tab_width = 4;

    return buffer;
}

void buffer_free(buffer_t *buffer) {
    /* TODO: deallocare tutto:
     - face
     - cairoface
     - font_options
     - cairo_scaled_font
     - lines
    */
}
