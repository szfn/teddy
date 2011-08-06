#include "buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

static void init_line(line_t *line) {
    line->allocated_glyphs = 10;
    line->glyphs = malloc(sizeof(cairo_glyph_t) * line->allocated_glyphs);
    if (!(line->glyphs)) {
        perror("Couldn't allocate glyphs space");
        exit(EXIT_FAILURE);
    }
    line->glyph_info = malloc(sizeof(my_glyph_info_t) * line->allocated_glyphs);
    if (!(line->glyphs)) {
        perror("Couldn't allocate glyphs space");
        exit(EXIT_FAILURE);
    }
    line->glyphs_cap = 0;
    line->hard_start = 1;
    line->hard_end = 1;
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

static void grow_line(line_t *line, int insertion_point, int size) { 
    /*printf("cap: %d allocated: %d\n", line->glyphs_cap, line->allocated_glyphs);*/
   
    while (line->glyphs_cap + size >= line->allocated_glyphs) {
        line->allocated_glyphs *= 2;
        /*printf("new size: %d\n", line->allocated_glyphs);*/
        line->glyphs = realloc(line->glyphs, sizeof(cairo_glyph_t) * line->allocated_glyphs);
        if (!(line->glyphs)) {
            perror("Couldn't allocate glyphs space");
            exit(EXIT_FAILURE);
        }
        line->glyph_info = realloc(line->glyph_info, sizeof(my_glyph_info_t) * line->allocated_glyphs);
        if (!(line->glyph_info)) {
            perror("Couldn't allocate glyphs space");
            exit(EXIT_FAILURE);
        }
    }

    if (insertion_point < line->glyphs_cap) {
        /*printf("memmove %x <- %x %d\n", line->glyphs+dst+1, line->glyphs+dst, line->glyphs_cap - dst); */
        memmove(line->glyphs+insertion_point+size, line->glyphs+insertion_point, sizeof(cairo_glyph_t)*(line->glyphs_cap - insertion_point));
        memmove(line->glyph_info+insertion_point+size, line->glyph_info+insertion_point, sizeof(my_glyph_info_t)*(line->glyphs_cap - insertion_point));
        line->glyphs_cap += size;
    } 
}

void buffer_line_insert_utf8_text(buffer_t *buffer, int line_idx, char *text, int len, int insertion_point, int move_cursor) {
    FT_Face scaledface = cairo_ft_scaled_font_lock_face(buffer->main_font.cairofont);
    FT_Bool use_kerning = FT_HAS_KERNING(scaledface);
    FT_UInt previous = 0;
    int initial_spaces = 1;
    int src, dst;
    double width = 0.0;
    line_t *line = buffer->lines + line_idx;
    
    for (src = 0, dst = insertion_point; src < len; ) {
        uint32_t code;
        FT_UInt glyph_index;
        cairo_text_extents_t extents;
        /*printf("First char: %02x\n", (uint8_t)text[src]);*/

        /* get next unicode codepoint in code, advance src */
        if ((uint8_t)text[src] > 127) {
            code = utf8_first_byte_processing(text[src]);
            ++src;

            /*printf("   Next char: %02x (%02x)\n", (uint8_t)text[src], (uint8_t)text[src] & 0xC0);*/
            
            for (; (((uint8_t)text[src] & 0xC0) == 0x80) && (src < len); ++src) {
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

        grow_line(line, dst, 1); 

        line->glyph_info[dst].code = code;

        /* Kerning correction for x */
        if (use_kerning && previous && glyph_index) {
            FT_Vector delta;
            
            FT_Get_Kerning(scaledface, previous, glyph_index, FT_KERNING_DEFAULT, &delta);

            line->glyph_info[dst].kerning_correction = delta.x >> 6;
        } else {
            line->glyph_info[dst].kerning_correction = 0;
        }

        width += line->glyph_info[dst].kerning_correction;
        
        previous = line->glyphs[dst].index = glyph_index;
        line->glyphs[dst].x = 0.0;
        line->glyphs[dst].y = 0.0;

        cairo_scaled_font_glyph_extents(buffer->main_font.cairofont, line->glyphs + dst, 1, &extents);
        
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
        
        line->glyph_info[dst].x_advance = extents.x_advance;
        width += extents.x_advance;
        ++dst;
        ++(line->glyphs_cap);

        if (move_cursor) {
            buffer->cursor_glyph++;
        }
    }
    
    width += buffer->em_advance;

    if (width > buffer->rendered_width) {
        buffer->rendered_width = width;
    }

    cairo_ft_scaled_font_unlock_face(buffer->main_font.cairofont);
}

double buffer_line_adjust_glyphs(buffer_t *buffer, int line_idx, double x, double y) {
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

    return x;
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

void load_text_file(buffer_t *buffer, const char *filename) {
    FILE *fin = fopen(filename, "r");
    char ch;
    int last_was_newline = 0;
    int i;
    int text_allocation = 10;
    char *text = malloc(sizeof(char) * text_allocation);

    if (!fin) {
        perror("Couldn't open input file");
        exit(EXIT_FAILURE);
    }

    if (text == NULL) {
        perror("Couldn't allocate memory");
        exit(EXIT_FAILURE);
    }
    
    if (buffer->lines_cap >= buffer->allocated_lines) {
        grow_lines(buffer);
    }

    while ((ch = fgetc(fin)) != EOF) {
        if (i >= text_allocation) {
            text_allocation *= 2;
            text = realloc(text, sizeof(char) * text_allocation);
            if (text == NULL) {
                perror("Couldn't allocate memory");
                exit(EXIT_FAILURE);
            }
        }
        if (ch == '\n') {
            last_was_newline = 1;
            text[i] = '\0';
            buffer_line_insert_utf8_text(buffer, buffer->lines_cap, text, strlen(text), buffer->lines[buffer->lines_cap].glyphs_cap, 0);
            ++(buffer->lines_cap);
            if (buffer->lines_cap >= buffer->allocated_lines) {
                grow_lines(buffer);
            }

            i = 0;
        } else {
            last_was_newline = 0;
            text[i++] = ch;
        }
    }

    if (!last_was_newline) {
        ++(buffer->lines_cap);
    }

    buffer->rendered_height = buffer->line_height * (1+buffer->lines_cap);

    fclose(fin);
}

void buffer_cursor_position(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y) {
    /*int i;*/
    line_t *line;
    
    *y = origin_y + (buffer->line_height * buffer->cursor_line);

    *x = origin_x;
    if (buffer->cursor_line >= buffer->lines_cap) return;

    line = buffer->lines + buffer->cursor_line;

    if (buffer->cursor_glyph < line->glyphs_cap) {
        *x = line->glyphs[buffer->cursor_glyph].x;
    } else if (line->glyphs_cap > 0) {
        *x = line->glyphs[line->glyphs_cap-1].x + line->glyph_info[line->glyphs_cap-1].x_advance;
    }
}

void buffer_move_cursor_to_position(buffer_t *buffer, double origin_x, double origin_y, double x, double y) {
    int i;
    line_t *line;

    buffer->cursor_line = ceil((y - origin_y) / buffer->line_height);

    if (buffer->cursor_line >= buffer->lines_cap) buffer->cursor_line = buffer->lines_cap - 1;
    if (buffer->cursor_line < 0) buffer->cursor_line = 0;

    if (buffer->lines_cap <= 0) {
        buffer->cursor_glyph = 0;
        return;
    }

    line = buffer->lines + buffer->cursor_line;

    for (i = 0; i < line->glyphs_cap; ++i) {
        double glyph_start = line->glyphs[i].x;
        double glyph_end = glyph_start + line->glyph_info[i].x_advance;

        if ((x >= glyph_start) && (x <= glyph_end)) {
            double dist_start = x - glyph_start;
            double dist_end = glyph_end - x;
            if (dist_start < dist_end) {
                buffer->cursor_glyph = i;
            } else {
                buffer->cursor_glyph = i+1;
            }
            break;
        }
    }

    if (i >= line->glyphs_cap) {
        buffer->cursor_glyph = line->glyphs_cap;
    }
}

buffer_t *buffer_create(FT_Library *library) {
    buffer_t *buffer = malloc(sizeof(buffer_t));

    buffer->library = library;
    
    acmacs_font_init(&(buffer->main_font), library, "/usr/share/fonts/truetype/msttcorefonts/arial.ttf", 16);
    acmacs_font_init(&(buffer->posbox_font), library, "/usr/share/fonts/truetype/msttcorefonts/arial.ttf", 12);
    
    {
        cairo_text_extents_t extents;
        cairo_font_extents_t font_extents;
        
        cairo_scaled_font_text_extents(buffer->main_font.cairofont, "M", &extents);
        buffer->em_advance = extents.width;

        cairo_scaled_font_text_extents(buffer->main_font.cairofont, "x", &extents);
        buffer->ex_height = extents.height;

        cairo_scaled_font_extents(buffer->main_font.cairofont, &font_extents);
        buffer->line_height = font_extents.height;
        buffer->ascent = font_extents.ascent;
        buffer->descent = font_extents.descent;
    }

    init_lines(buffer);

    buffer->rendered_height = 0.0;
    buffer->rendered_width = 0.0;

    buffer->cursor_line = 0;
    buffer->cursor_glyph = 0;

    buffer->tab_width = 4;
    buffer->left_margin = 8.0;
    buffer->right_margin = 8.0;

    return buffer;
}

void buffer_free(buffer_t *buffer) {
    int i;
    
    for (i = 0; i < buffer->allocated_lines; ++i) {
        line_t *curline = buffer->lines+i;
        if (curline->glyphs != NULL) free(curline->glyphs);
        if (curline->glyph_info != NULL) free(curline->glyph_info);
    }

    free(buffer->lines);

    acmacs_font_free(&(buffer->main_font));
    acmacs_font_free(&(buffer->posbox_font));
}

static void buffer_line_reflow_softwrap(buffer_t *buffer, int line_idx, double softwrap_width) {
    double width = 0.0;
    int j;
    line_t *line = buffer->lines + line_idx;
    
    /* Scan current line until either:
       - we ran out of space
       - the line ends */
    
    for (j = 0; j < line->glyphs_cap; ++j) {
        width += line->glyph_info[j].kerning_correction;
        width += line->glyph_info[j].x_advance;
        if (width > softwrap_width) {
            break;
        }
    }
    
    /* the first character we can ever move to the next line is always the first one */
    if (j == 0) j = 1;
    
    if (j < line->glyphs_cap) {
        /* If this line used to have a hard end here then create a new line to push extra characters to */
        
        line_t *next_line;
        
        if (line->hard_end) {
            line->hard_end = 0;

            /* We create a new line here */

            if (buffer->lines_cap >= buffer->allocated_lines) {
                grow_lines(buffer);
            }
            memmove(buffer->lines + line_idx + 2, buffer->lines + line_idx + 1, sizeof(line_t) * (buffer->lines_cap - line_idx - 1));

            next_line = buffer->lines + line_idx + 1;

            init_line(next_line);

            next_line->hard_start = 0;
            next_line->hard_end = 1;
        } else {
            next_line = buffer->lines + line_idx + 1;
        }

        /* Push characters on the next line*/

        grow_line(next_line, 0, line->glyphs_cap-j);

        memmove(next_line->glyphs, line->glyphs+j, sizeof(cairo_glyph_t) * (line->glyphs_cap-j));
        memmove(next_line->glyph_info, line->glyph_info+j, sizeof(my_glyph_info_t) * (line->glyphs_cap-j));

        if (next_line->glyphs_cap == 0) {
            next_line->glyphs_cap = line->glyphs_cap-j;
        }

        line->glyphs_cap = j;
    } else {
        if (line->hard_end) return;
        
        /* If the line ended and (line->hard_end == false) copy glyphs from next line until the space is filled or we run into a line that is the hard_end */
    }
}

void buffer_reflow_softwrap(buffer_t *buffer, double softwrap_width) {
    int i;
    softwrap_width -= buffer->right_margin + buffer->left_margin;
    
    if (buffer->rendered_width <= softwrap_width) return;

    /* Get the real cursor position */

    for (i = 0; i < buffer->lines_cap; ++i) {
        buffer_line_reflow_softwrap(buffer, i, softwrap_width);
    }

    buffer->rendered_width = softwrap_width + buffer->right_margin + buffer->left_margin;

    /* Restore cursor position */
}
