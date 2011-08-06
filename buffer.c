#include "buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

static real_line_t *new_real_line() {
    real_line_t *line = malloc(sizeof(real_line_t));
    line->allocated = 10;
    line->glyphs = malloc(sizeof(cairo_glyph_t) * line->allocated);
    if (!(line->glyphs)) {
        perror("Couldn't allocate glyphs space");
        exit(EXIT_FAILURE);
    }
    line->glyph_info = malloc(sizeof(my_glyph_info_t) * line->allocated);
    if (!(line->glyphs)) {
        perror("Couldn't allocate glyphs space");
        exit(EXIT_FAILURE);
    }
    line->cap = 0;
    line->next = NULL;
    return line;
}

static display_line_t *new_display_line(real_line_t *real_line, int offset, int size) {
    display_line_t *display_line = malloc(sizeof(display_line_t));
    display_line->real_line = real_line;
    display_line->offset = offset;
    display_line->size = size;
    display_line->next = NULL;
    display_line->hard_end = 1;
    return display_line;
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

static void grow_line(real_line_t *line, int insertion_point, int size) { 
    /*printf("cap: %d allocated: %d\n", line->glyphs_cap, line->allocated_glyphs);*/
   
    while (line->cap + size >= line->allocated) {
        line->allocated *= 2;
        if (line->allocated == 0) line->allocated = 10;
        /*printf("new size: %d\n", line->allocated_glyphs);*/
        line->glyphs = realloc(line->glyphs, sizeof(cairo_glyph_t) * line->allocated);
        if (!(line->glyphs)) {
            perror("Couldn't allocate glyphs space");
            exit(EXIT_FAILURE);
        }
        line->glyph_info = realloc(line->glyph_info, sizeof(my_glyph_info_t) * line->allocated);
        if (!(line->glyph_info)) {
            perror("Couldn't allocate glyphs space");
            exit(EXIT_FAILURE);
        }
    }

    if (insertion_point < line->cap) {
        /*printf("memmove %x <- %x %d\n", line->glyphs+dst+1, line->glyphs+dst, line->glyphs_cap - dst); */
        memmove(line->glyphs+insertion_point+size, line->glyphs+insertion_point, sizeof(cairo_glyph_t)*(line->cap - insertion_point));
        memmove(line->glyph_info+insertion_point+size, line->glyph_info+insertion_point, sizeof(my_glyph_info_t)*(line->cap - insertion_point));
        line->cap += size;
    } 
}

void buffer_line_insert_utf8_text(buffer_t *buffer, real_line_t *line, char *text, int len, int insertion_point, int move_cursor) {
    FT_Face scaledface = cairo_ft_scaled_font_lock_face(buffer->main_font.cairofont);
    FT_Bool use_kerning = FT_HAS_KERNING(scaledface);
    FT_UInt previous = 0;
    int initial_spaces = 1;
    int src, dst;
    double width = 0.0;
    
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
        if (dst == line->cap) {
            ++(line->cap);
        }
        ++dst;

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

double buffer_line_adjust_glyphs(buffer_t *buffer, display_line_t *display_line, double x, double y) {
    real_line_t *line = display_line->real_line;
    cairo_glyph_t *glyphs = line->glyphs + display_line->offset;
    my_glyph_info_t *glyph_info = line->glyph_info + display_line->offset;
    int i;

    for (i = 0; i < display_line->size; ++i) {
        if (i > 0) x += glyph_info[i].kerning_correction;
        glyphs[i].x = x;
        glyphs[i].y = y;
        x += glyph_info[i].x_advance;
    }

    return x;
}

static void buffer_create_display(buffer_t *buffer) {
    real_line_t *line;
    display_line_t **display_line_pp = &(buffer->display_line);

    for (line = buffer->real_line; line != NULL; line = line->next) {
        *display_line_pp = new_display_line(line, 0, line->cap);
        display_line_pp = &((*display_line_pp)->next);
    }
}

void load_text_file(buffer_t *buffer, const char *filename) {
    FILE *fin = fopen(filename, "r");
    char ch;
    int i = 0;
    int text_allocation = 10;
    char *text = malloc(sizeof(char) * text_allocation);
    real_line_t **real_line_pp = &(buffer->real_line);

    buffer->rendered_height = 0;

    if (!fin) {
        perror("Couldn't open input file");
        exit(EXIT_FAILURE);
    }

    if (text == NULL) {
        perror("Couldn't allocate memory");
        exit(EXIT_FAILURE);
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
            text[i] = '\0';
            if (*real_line_pp == NULL) *real_line_pp = new_real_line();
            buffer_line_insert_utf8_text(buffer, *real_line_pp, text, strlen(text), (*real_line_pp)->cap, 0);
            real_line_pp = &((*real_line_pp)->next);
            buffer->rendered_height += buffer->line_height;
            i = 0;
        } else {
            text[i++] = ch;
        }
    }

    free(text);

    buffer_create_display(buffer);
    
    fclose(fin);
}

void buffer_cursor_position(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y) {
    *x = 0; *y = 0;
    /* TODO: rewrite
    line_t *line;
    
    *y = origin_y + (buffer->line_height * buffer->cursor_line);

    *x = origin_x;
    if (buffer->cursor_line >= buffer->lines_cap) return;

    line = buffer->lines + buffer->cursor_line;

    if (buffer->cursor_glyph < line->glyphs_cap) {
        *x = line->glyphs[buffer->cursor_glyph].x;
    } else if (line->glyphs_cap > 0) {
        *x = line->glyphs[line->glyphs_cap-1].x + line->glyph_info[line->glyphs_cap-1].x_advance;
        }*/
}

void buffer_move_cursor_to_position(buffer_t *buffer, double origin_x, double origin_y, double x, double y) {
    buffer->cursor_line = 0; buffer->cursor_glyph = 0;
    /* TODO reimplement
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
        }*/
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

    buffer->real_line = NULL;
    buffer->display_line = NULL;
    buffer->display_lines_count = 0;

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
    {
        real_line_t *cursor;
        
        cursor = buffer->real_line;
        
        while (cursor != NULL) {
            real_line_t *next = cursor->next;
            free(cursor->glyphs);
            free(cursor->glyph_info);
            free(cursor);
            cursor = next;
        }
    }

    {
        display_line_t *cursor;

        cursor = buffer->display_line;

        while (cursor != NULL) {
            display_line_t *next = cursor->next;
            free(cursor);
            cursor = next;
        }
    }

    acmacs_font_free(&(buffer->main_font));
    acmacs_font_free(&(buffer->posbox_font));
}

static void buffer_line_reflow_softwrap(buffer_t *buffer, display_line_t *display_line, double softwrap_width) {
    double width = 0.0;
    real_line_t *real_line = display_line->real_line;
    int j;
    
    /* Scan current display line until either:
       - we ran out of space
       - the line ends */

    for (j = 0; j < display_line->size; ++j) {
        width += real_line->glyph_info[display_line->offset+j].kerning_correction;
        width += real_line->glyph_info[display_line->offset+j].x_advance;
        if (width > softwrap_width) {
            break;
        }
    }
    
    /* the first character we can ever move to the next line is always the first one */
    if (j == 0) j = 1;

    if (j < display_line->size) {
        /* Create a new display line and push characters to it */
        display_line_t *saved_next = display_line->next;
        display_line->next = new_display_line(display_line->real_line, display_line->offset + j, display_line->size - j);
        display_line->next->hard_end = display_line->hard_end;
        display_line->next->next = saved_next;
        
        display_line->hard_end = 0;
        display_line->size = j;
    } else {
        display_line_t *next_display_line = display_line->next;
        if (display_line->hard_end) return;

        /* Join the next line with this one */

        display_line->hard_end = next_display_line->hard_end;
        display_line->size += next_display_line->size;

        display_line->next = next_display_line->next;

        free(next_display_line);

        /* Recur, this is inefficient and potentially leads to stack overflow but must do for now */

        buffer_line_reflow_softwrap(buffer, display_line, softwrap_width);
    }
}

void debug_print_lines_state(buffer_t *buffer) {
    display_line_t *display_line;
    int i, cnt = 0;

    for (display_line = buffer->display_line; display_line != NULL; display_line = display_line->next) {
        printf("%d offset:%d size:%d hard_end:%d [", cnt, display_line->offset, display_line->size, display_line->hard_end);
        for (i = 0; i < display_line->size; ++i) {
            printf("%c", (char)(display_line->real_line->glyph_info[display_line->offset + i].code));
        }
        printf("]\n");
        ++cnt;
    }

    printf("----------------\n\n");
}

void buffer_reflow_softwrap(buffer_t *buffer, double softwrap_width) {
    display_line_t *display_line;
    /* int i; */

    if (fabs(buffer->rendered_width - softwrap_width) < 0.001) return;
    buffer->rendered_width = softwrap_width;
    
    softwrap_width -= buffer->right_margin + buffer->left_margin;

    //debug_print_lines_state(buffer);

    /* printf("Reflow called\n"); */

    /* Get the real cursor position */
    /*TODO*/

    for (display_line = buffer->display_line; display_line != NULL; display_line = display_line->next) {
        buffer_line_reflow_softwrap(buffer, display_line, softwrap_width);
        //debug_print_lines_state(buffer);
    }


    /* Restore cursor position */
    /*TODO*/
}
