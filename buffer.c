#include "buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "global.h"

void buffer_set_mark_at_cursor(buffer_t *buffer) {
    copy_lpoint(&(buffer->mark), &(buffer->cursor));
    //printf("Mark set @ %d,%d\n", buffer->mark_line->lineno, buffer->mark_glyph);
}

void buffer_unset_mark(buffer_t *buffer) {
    if (buffer->mark.line != NULL) {
        buffer->mark.line = NULL;
        buffer->mark.glyph = -1;
        //printf("Mark unset\n");
    }
}

static void buffer_set_to_real(buffer_t *buffer, lpoint_t *real_point) {
    copy_lpoint(&(buffer->cursor), real_point);

    if (buffer->cursor.glyph < 0) buffer->cursor.glyph = 0;
    if (buffer->cursor.glyph > buffer->cursor.line->cap) buffer->cursor.glyph = buffer->cursor.line->cap;
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

static void buffer_join_lines(buffer_t *buffer, real_line_t *line1, real_line_t *line2) {
    real_line_t *line_cur;
    int lineno;

    if (line1 == NULL) return;
    if (line2 == NULL) return;
    
    grow_line(line1, line1->cap, line2->cap);
    memcpy(line1->glyphs+line1->cap, line2->glyphs, sizeof(cairo_glyph_t)*line2->cap);
    memcpy(line1->glyph_info+line1->cap, line2->glyph_info, sizeof(my_glyph_info_t)*line2->cap);
    line1->cap += line2->cap;

    /* remove line2 from real_lines list */

    line1->next = line2->next;
    if (line2->next != NULL) line2->next->prev = line1;
    
    free(line2->glyphs);
    free(line2->glyph_info);
    free(line2);

    lineno = line1->lineno + 1;

    for (line_cur = line1->next; line_cur != NULL; line_cur = line_cur->next) {
        line_cur->lineno = lineno;
        ++lineno;
    }
}

static void buffer_line_delete_from(buffer_t *buffer, real_line_t *real_line, int start, int size) {
    memmove(real_line->glyphs+start, real_line->glyphs+start+size, sizeof(cairo_glyph_t)*(real_line->cap-start-size));
    memmove(real_line->glyph_info+start, real_line->glyph_info+start+size, sizeof(my_glyph_info_t)*(real_line->cap-start-size));
    real_line->cap -= size;
}

static void buffer_remove_selection(buffer_t *buffer, lpoint_t *start, lpoint_t *end) {
    real_line_t *real_line;
    int lineno;
 
    if (start->line == NULL) return;
    if (end->line == NULL) return;

    buffer->modified = 1;

    //printf("Deleting %d from %d (size: %d)\n", start_line->lineno, start_glyph, start_line->cap-start_glyph);

    /* Special case when we are deleting a section of the same line */
    if (start->line == end->line) {
        buffer_line_delete_from(buffer, start->line, start->glyph, end->glyph-start->glyph);
        buffer_set_to_real(buffer, start);
        return;
    }
    
    /* Remove text from first and last real lines */
    buffer_line_delete_from(buffer, start->line, start->glyph, start->line->cap-start->glyph);
    buffer_line_delete_from(buffer, end->line, 0, end->glyph);

    /* Remove real_lines between start and end */
    for (real_line = start->line->next; (real_line != NULL) && (real_line != end->line); ) {
        real_line_t *next = real_line->next;
        free(real_line->glyphs);
        free(real_line->glyph_info);
        free(real_line);
        real_line = next;
    }

    start->line->next = end->line;
    end->line->prev = start->line;

    /* Renumber real_lines */

    lineno = start->line->lineno+1;
    for (real_line = start->line->next; real_line != NULL; real_line = real_line->next) {
        real_line->lineno = lineno;
        ++lineno;
    }

    /*
    printf("AFTER FIXING REAL LINES, BEFORE FIXING DISPLAY LINES:\n");
    debug_print_real_lines_state(buffer);
    debug_print_lines_state(buffer);
    */

    buffer_set_to_real(buffer, start);

    buffer_join_lines(buffer, start->line, end->line);
}

static real_line_t *new_real_line(int lineno) {
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
    line->prev = NULL;
    line->next = NULL;
    line->lineno = lineno;
    line->y_increment = 0.0;
    return line;
}

static real_line_t *buffer_copy_line(buffer_t *buffer, real_line_t *real_line, int start, int size) {
    real_line_t *r = new_real_line(-1);

    //printf("buffer_copy_line start: %d, cap: %d, size to copy: %d\n", start, real_line->cap, size);

    grow_line(r, 0, size);

    memcpy(r->glyphs, real_line->glyphs+start, size * sizeof(cairo_glyph_t));
    memcpy(r->glyph_info, real_line->glyph_info+start, size * sizeof(my_glyph_info_t));

    r->cap = size;

    return r;
}

static void buffer_real_line_insert(buffer_t *buffer, real_line_t *insertion_line, real_line_t* real_line) {
    real_line_t *cur;
    int lineno;

    //debug_print_real_lines_state(buffer);
    
    real_line->next = insertion_line->next;
    if (real_line->next != NULL) real_line->next->prev = real_line;
    real_line->prev = insertion_line;
    insertion_line->next = real_line;

    lineno = insertion_line->lineno;

    //printf("Renumbering from %d\n", lineno);

    for (cur = insertion_line; cur != NULL; cur = cur->next) {
        cur->lineno = lineno;
        lineno++;
        //debug_print_real_lines_state(buffer);
    }
}

static void buffer_split_line(buffer_t *buffer, lpoint_t *point) {
    real_line_t *copied_segment = buffer_copy_line(buffer, point->line, point->glyph, point->line->cap - point->glyph);
    buffer_line_delete_from(buffer, point->line, point->glyph, point->line->cap - point->glyph);
    buffer_real_line_insert(buffer, point->line, copied_segment);
}

static double buffer_line_fix_spaces(buffer_t *buffer, real_line_t *line) {
    int i = 0;
    int initial_spaces = 1;
    double width_correction = 0.0;

    //printf("fixing spaces\n");
    
    for (i = 0; i < line->cap; ++i) {
        if (line->glyph_info[i].code == 0x20) {
            double new_width;
            if (initial_spaces) {
                new_width = buffer->em_advance;
            } else {
                new_width = buffer->space_advance;
            }

            //printf("is: %d width: %g\n", initial_spaces, new_width);
            
            width_correction += new_width - line->glyph_info[i].x_advance;
            line->glyph_info[i].x_advance = new_width;
            
        } else if (line->glyph_info[i].code == 0x09) {
            double new_width;
            if (initial_spaces) {
                new_width = buffer->em_advance * buffer->tab_width;
            } else {
                new_width = buffer->space_advance * buffer->tab_width;
            }

            width_correction += new_width - line->glyph_info[i].x_advance;
            line->glyph_info[i].x_advance = new_width;

        } else {
            initial_spaces = 0;
        }
    }

    return width_correction;
}

static int buffer_line_insert_utf8_text(buffer_t *buffer, real_line_t *line, const char *text, int len, int insertion_point) {
    FT_Face scaledface = cairo_ft_scaled_font_lock_face(buffer->main_font.cairofont);
    FT_Bool use_kerning = FT_HAS_KERNING(scaledface);
    FT_UInt previous = 0;
    int src, dst;
    int inserted_glyphs = 0;

    if (insertion_point > 0) {
        previous = line->glyphs[insertion_point-1].index;
    }

    for (src = 0, dst = insertion_point; src < len; ) {
        uint32_t code = utf8_to_utf32(text, &src, len);
        FT_UInt glyph_index;
        cairo_text_extents_t extents;
        /*printf("First char: %02x\n", (uint8_t)text[src]);*/

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

        previous = line->glyphs[dst].index = glyph_index;
        line->glyphs[dst].x = 0.0;
        line->glyphs[dst].y = 0.0;

        cairo_scaled_font_glyph_extents(buffer->main_font.cairofont, line->glyphs + dst, 1, &extents);
        
        if (code == 0x09) {
            extents.x_advance *= buffer->tab_width;
        }
        
        line->glyph_info[dst].x_advance = extents.x_advance;
        if (dst == line->cap) {
            ++(line->cap);
        }
        ++dst;
        ++inserted_glyphs;
    }

    if (dst < line->cap) {
        if (use_kerning) {
            FT_Vector delta;
            
            FT_Get_Kerning(scaledface, previous, line->glyphs[dst].index, FT_KERNING_DEFAULT, &delta);

            line->glyph_info[dst].kerning_correction = delta.x >> 6;
        } else {
            line->glyph_info[dst].kerning_correction = 0;
        }
        
    }

    //buffer_line_fix_spaces(buffer, line);

    cairo_ft_scaled_font_unlock_face(buffer->main_font.cairofont);

    return inserted_glyphs;
}

static void buffer_insert_multiline_text(buffer_t *buffer, lpoint_t *start_point, const char *text) {
    lpoint_t point;
    int start = 0;
    int end = 0;
    
    copy_lpoint(&point, start_point);

    //printf("Inserting multiline text [[%s]]\n\n", text);

    while (end < strlen(text)) {
        if ((text[end] == '\n') || (text[end] == '\r')) {
            //printf("line cap: %d glyph %d\n", line->cap, glyph);
            point.glyph += buffer_line_insert_utf8_text(buffer, point.line, text+start, end-start, point.glyph);
            //printf("    line cap: %d glyph: %d\n", line->cap, glyph);
            buffer_split_line(buffer, &point);
            
            assert(point.line->next != NULL);
            point.line = point.line->next;
            point.glyph = 0;
            
            ++end;
            start = end;
        } else {
            if (buffer->job != NULL) {
                if (text[end] == 0x08) {
                    if (end == 0) {
                        // if this the very first character delete one character from the buffer
                        if (buffer->cursor.line->cap > 0) {
                            --(buffer->cursor.line->cap);
                            --(point.glyph);
                        }
                    } else {
                        //printf("(bs) line cap: %d glyph: %d\n", line->cap, glyph);
                        point.glyph += buffer_line_insert_utf8_text(buffer, point.line, text+start, end-start-1, point.glyph);
                        //printf("    line cap: %d glyph: %d\n", line->cap, glyph);
                    }
                    ++end;
                    start = end;
                } else {
                    ++end;
                }
            } else {
                ++end;
            }
        }
    }

    if (start < end) {
        point.glyph += buffer_line_insert_utf8_text(buffer, point.line, text+start, end-start, point.glyph);
        //printf("(end) line cap: %d glyph: %d\n", line->cap, glyph);
    }

    copy_lpoint(&(buffer->cursor), &point);
}

static void freeze_selection(buffer_t *buffer, selection_t *selection, lpoint_t *start, lpoint_t *end) {
    if ((start->line == NULL) || (end->line == NULL)) {
        freeze_point(&(selection->start), &(buffer->cursor));
        freeze_point(&(selection->end), &(buffer->cursor));
        selection->text = malloc(sizeof(char));
        
        if (selection->text == NULL) {
            perror("Out of memory");
            exit(EXIT_FAILURE);
        }

        selection->text[0] = '\0';
    } else {
        freeze_point(&(selection->start), start);
        freeze_point(&(selection->end), end);
        
        selection->text = buffer_lines_to_text(buffer, start, end);
    }
}

static void buffer_line_adjust_glyphs(buffer_t *buffer, real_line_t *line, double y) {
    int i;
    double y_increment = buffer->line_height;
    double x = buffer->left_margin;
    
    line->start_y = y;
    line->end_y = y;

    buffer_line_fix_spaces(buffer, line);    

    //printf("setting type\n");
    for (i = 0; i < line->cap; ++i) {
        x += line->glyph_info[i].kerning_correction;
        if (x+line->glyph_info[i].x_advance > buffer->rendered_width - buffer->right_margin) {
            y += buffer->line_height;
            line->end_y = y;
            y_increment += buffer->line_height;
            x = buffer->left_margin;
        }
        line->glyphs[i].x = x;
        line->glyphs[i].y = y;
        x += line->glyph_info[i].x_advance;
        //printf("x: %g (%g)\n", x, glyph_info[i].x_advance);
    }

    line->y_increment = y_increment;
}

static void buffer_typeset_from(buffer_t *buffer, real_line_t *start_line) {
    real_line_t *line;
    double y = start_line->start_y;
    
    for (line = start_line; line != NULL; line = line->next) {
        buffer_line_adjust_glyphs(buffer, line, y);
        y += line->y_increment;
    }
}

void buffer_replace_selection(buffer_t *buffer, const char *new_text) {
    lpoint_t start_point, end_point;
    undo_node_t *undo_node;
    
    //printf("buffer_replace_selection (call): %d %d\n", buffer->cursor.line->cap, buffer->cursor.glyph);

    if (!(buffer->editable)) return;

    buffer->modified = 1;

    if (buffer->job == NULL)
        undo_node = malloc(sizeof(undo_node_t));

    buffer_get_selection(buffer, &start_point, &end_point);

    if (buffer->job == NULL)
       freeze_selection(buffer, &(undo_node->before_selection), &start_point, &end_point);
    
    buffer_remove_selection(buffer, &start_point, &end_point);

    copy_lpoint(&start_point, &(buffer->cursor));

    //printf("buffer_replace_selection: %d %d\n", buffer->cursor_line->cap, buffer->cursor_glyph);    
    buffer_insert_multiline_text(buffer, &(buffer->cursor), new_text);

    copy_lpoint(&end_point, &(buffer->cursor));

    if (buffer->job == NULL)
        freeze_selection(buffer, &(undo_node->after_selection), &start_point, &end_point);

    if (buffer->job == NULL)
        undo_push(&(buffer->undo), undo_node);

    buffer_typeset_from(buffer, start_point.line);
    
    buffer_unset_mark(buffer);
}

static real_line_t *buffer_search_line(buffer_t *buffer, int lineno) {
    real_line_t *real_line;
    for (real_line = buffer->real_line; real_line != NULL; real_line = real_line->next) {
        if (real_line->lineno == lineno) return real_line;
    }
    return real_line;
}

static void buffer_thaw_selection(buffer_t *buffer, selection_t *selection, lpoint_t *start, lpoint_t *end) {
    start->line = buffer_search_line(buffer, selection->start.lineno);
    start->glyph = selection->start.glyph;

    end->line = buffer_search_line(buffer, selection->end.lineno);
    end->glyph = selection->end.glyph;
}

void buffer_undo(buffer_t *buffer) {
    lpoint_t start_point, end_point;
    real_line_t *typeset_start_line;
    undo_node_t *undo_node; 

    if (!(buffer->editable)) return;
    if (buffer->job != NULL) return;

    undo_node = undo_pop(&(buffer->undo));

    buffer->modified = 1;

    buffer_unset_mark(buffer);

    buffer_thaw_selection(buffer, &(undo_node->after_selection), &start_point, &end_point);

    buffer_remove_selection(buffer, &start_point, &end_point);

    typeset_start_line = buffer->cursor.line;

    buffer_insert_multiline_text(buffer, &(buffer->cursor), undo_node->before_selection.text);

    buffer_typeset_from(buffer, typeset_start_line);

    undo_node_free(undo_node);
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

uint32_t utf8_to_utf32(const char *text, int *src, int len) {
    uint32_t code;
    
    /* get next unicode codepoint in code, advance src */
    if ((uint8_t)text[*src] > 127) {
        code = utf8_first_byte_processing(text[*src]);
        ++(*src);

        /*printf("   Next char: %02x (%02x)\n", (uint8_t)text[src], (uint8_t)text[src] & 0xC0);*/
            
        for (; (((uint8_t)text[*src] & 0xC0) == 0x80) && (*src < len); ++(*src)) {
            code <<= 6;
            code += (text[*src] & 0x3F);
        }
    } else {
        code = text[*src];
        ++(*src);
    }

    return code;
}

void load_empty(buffer_t *buffer) {
    if (buffer->has_filename) {
        return;
    }

    buffer->has_filename = 0;
    buffer->path = NULL;

    buffer->cursor.line = buffer->real_line = new_real_line(0);
    buffer->cursor.glyph = 0;
}

void buffer_cd(buffer_t *buffer, const char *wd) {
    if (buffer->has_filename) return;

    if (buffer->path != NULL) free(buffer->path);
    asprintf(&(buffer->path), "%s", wd);

    if (buffer->wd != NULL) free(buffer->wd);
    asprintf(&(buffer->wd), "%s", wd);
}

int load_text_file(buffer_t *buffer, const char *filename) {
    FILE *fin = fopen(filename, "r");
    char ch;
    int i = 0;
    int text_allocation = 10;
    char *text = malloc(sizeof(char) * text_allocation);
    real_line_t *prev_line = NULL;
    real_line_t **real_line_pp = &(buffer->real_line);
    int lineno = 0;

    if (!fin) {
        return -1;
    }

    if (buffer->has_filename) {
        return -1;
    }

    buffer->has_filename = 1;
    free(buffer->name);
    buffer->path = realpath(filename, NULL);
    {
        char *name = strrchr(buffer->path, '/');
        if (name == NULL) {
            asprintf(&(buffer->name), "%s", buffer->path);
        } else {
            asprintf(&(buffer->name), "%s", name+1);
            
            free(buffer->wd);
            buffer->wd = malloc(sizeof(char) * (name - buffer->path + 2));
            strncpy(buffer->wd, buffer->path, (name - buffer->path + 1));
            buffer->wd[name - buffer->path + 1] = '\0';
            //printf("Working directory: [%s]\n", buffer->wd);
        }
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
            if (*real_line_pp == NULL) *real_line_pp = new_real_line(lineno);
            buffer_line_insert_utf8_text(buffer, *real_line_pp, text, strlen(text), (*real_line_pp)->cap);
            (*real_line_pp)->prev = prev_line;
            prev_line = *real_line_pp;
            real_line_pp = &((*real_line_pp)->next);
            i = 0;
            ++lineno;
        } else {
            text[i++] = ch;
        }
    }

    text[i] = '\0';
    if (*real_line_pp == NULL) *real_line_pp = new_real_line(lineno);
    buffer_line_insert_utf8_text(buffer, *real_line_pp, text, strlen(text), (*real_line_pp)->cap);
    (*real_line_pp)->prev = prev_line;

    buffer->cursor.line = buffer->real_line;
    buffer->cursor.glyph = 0;

    free(text);

    printf("Loaded lines: %d (name: %s) (path: %s)\n", lineno, buffer->name, buffer->path);
    
    fclose(fin);

    return 0;
}

char *buffer_line_to_text(buffer_t *buffer, real_line_t *line) {
    lpoint_t start, end;
    start.line = end.line = line; 
    start.glyph = 0;
    end.glyph = line->cap;
    return buffer_lines_to_text(buffer, &start, &end);
}

char *buffer_lines_to_text(buffer_t *buffer, lpoint_t *startp, lpoint_t *endp) {
    real_line_t *line;
    int allocated = 0;
    int cap = 0;
    char *r = NULL;

    allocated = 10;
    r = malloc(sizeof(char) * allocated);
    
    for (line = startp->line; line != NULL; line = line->next) {
        int start, end, i;
        if (line == startp->line) {
            start = startp->glyph;
        } else {
            start = 0;
        }
        
        if (line == endp->line) {
            end = endp->glyph;
        } else {
            end = line->cap;
        }
    
        for (i = start; i < end; ++i) {
            uint32_t code = line->glyph_info[i].code;
            int i, inc, first_byte_mask, first_byte_pad;
        
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
        
            if (cap+inc >= allocated) {
                allocated *= 2;
                r = realloc(r, sizeof(char)* allocated);
            }
        
            for (i = inc; i > 0; --i) {
                r[cap+i] = ((uint8_t)code & 0x2f) + 0x80;
                code >>= 6;
            }
        
            r[cap] = ((uint8_t)code & first_byte_mask) + first_byte_pad;
        
            cap += inc + 1;
        }
    
    
        if (line == endp->line) break;
        else {
            if (cap >= allocated) {
                allocated *= 2;
                r = realloc(r, sizeof(char)*allocated);
            }
            r[cap++] = '\n';
        }
    }

    if (cap >= allocated) {
        allocated *= 2;
        r = realloc(r, sizeof(char)*allocated);
    }
    r[cap++] = '\0';

    return r;
}

void save_to_text_file(buffer_t *buffer) {
    char *cmd;
    FILE *file;
    char *r;
    size_t towrite, write_start, written;
    
    asprintf(&cmd, "mv -f %s %s~", buffer->path, buffer->path);
    system(cmd);
    free(cmd);

    file = fopen(buffer->path, "w");

    if (!file) {
        perror("Couldn't write to file");
        return;
    }

    {
        lpoint_t startp = { buffer->real_line, 0 };
        lpoint_t endp = { NULL, -1 };
        r = buffer_lines_to_text(buffer, &startp, &endp);
    }

    if (r[strlen(r)-1] == '\n') r[strlen(r)-1] = '\0'; // removing spurious final newline added by loading function

    towrite = strlen(r);
    write_start = 0;

    while (towrite > 0) {
        written = fwrite(r+write_start, sizeof(char), towrite, file);
        if (written == 0) {
            perror("Error writing to file");
            break;
        }
        towrite -= written;
        write_start += written;
    }

    fclose(file);

    free(r);

    asprintf(&cmd, "diff %s %s~", buffer->path, buffer->path);
    system(cmd);
    free(cmd);

    buffer->modified = 0;
}

void line_get_glyph_coordinates(buffer_t *buffer, lpoint_t *point, double *x, double *y) {
    if (point->line == NULL) {
        *y = 0.0;
        *x = 0.0;
        return;
    }

    if (point->line->cap == 0) {
        *x = buffer->left_margin;
        *y = point->line->start_y;
        return;
    }
    
    if (point->glyph >= point->line->cap) {
        *y = point->line->glyphs[point->line->cap-1].y;
        *x = point->line->glyphs[point->line->cap-1].x + point->line->glyph_info[point->line->cap-1].x_advance;
    } else {
        *y = point->line->glyphs[point->glyph].y;
        *x = point->line->glyphs[point->glyph].x;
    }
}

void buffer_cursor_position(buffer_t *buffer, double *x, double *y) {
    line_get_glyph_coordinates(buffer, &(buffer->cursor), x, y);
}

void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y) {
    real_line_t *line, *prev = NULL;
    int i;

    for (line = buffer->real_line; line->next != NULL; line = line->next) {
        //printf("Cur y: %g (searching %g)\n", line->start_y, y);
        if (line->end_y > y) break;
    }

    //printf("New position lineno: %d\n", line->lineno);
    buffer->cursor.line = line;

    if (line == NULL) line = prev;

    assert(line != NULL);

    for (i = 0; i < line->cap; ++i) {
        if ((y >= line->glyphs[i].y - buffer->line_height) && (y <= line->glyphs[i].y)) {
            double glyph_start = line->glyphs[i].x;
            double glyph_end = glyph_start + line->glyph_info[i].x_advance;

            if (x < glyph_start) {
                buffer->cursor.glyph = i;
                break;
            }

            if ((x >= glyph_start) && (x <= glyph_end)) {
                double dist_start = x - glyph_start;
                double dist_end = glyph_end - x;
                if (dist_start < dist_end) {
                    buffer->cursor.glyph = i;
                } else {
                    buffer->cursor.glyph = i+1;
                }
                break;
            }
        }
    }

    if (i >= line->cap) {
        buffer->cursor.glyph = line->cap;
    }
}

buffer_t *buffer_create(FT_Library *library) {
    buffer_t *buffer = malloc(sizeof(buffer_t));

    buffer->library = library;
    buffer->modified = 0;
    buffer->editable = 1;
    buffer->job = NULL;

    asprintf(&(buffer->name), "+unnamed");
    buffer->path = NULL;
    asprintf(&(buffer->wd), "%s", getcwd(NULL, 0));
    buffer->has_filename = 0;

    undo_init(&(buffer->undo));

    teddy_font_init(&(buffer->main_font), library, cfg_main_font.strval);
    teddy_font_init(&(buffer->posbox_font), library, cfg_posbox_font.strval);
    
    {
        cairo_text_extents_t extents;
        cairo_font_extents_t font_extents;
        
        cairo_scaled_font_text_extents(buffer->main_font.cairofont, "M", &extents);
        buffer->em_advance = extents.width;

        cairo_scaled_font_text_extents(buffer->main_font.cairofont, "x", &extents);
        buffer->ex_height = extents.height;

        cairo_scaled_font_text_extents(buffer->main_font.cairofont, " ", &extents);
        buffer->space_advance = extents.x_advance;

        cairo_scaled_font_extents(buffer->main_font.cairofont, &font_extents);
        buffer->line_height = font_extents.height;
        buffer->ascent = font_extents.ascent;
        buffer->descent = font_extents.descent;
    }

    buffer->real_line = NULL;

    buffer->rendered_height = 0.0;
    buffer->rendered_width = 0.0;

    buffer->cursor.line = NULL;
    buffer->cursor.glyph = 0;

    buffer->mark.line = NULL;
    buffer->mark.glyph = -1;

    buffer->tab_width = 4;
    buffer->left_margin = 4.0;
    buffer->right_margin = 4.0;

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

    teddy_font_free(&(buffer->main_font));
    teddy_font_free(&(buffer->posbox_font));

    undo_free(&(buffer->undo));

    free(buffer->name);
    free(buffer->path);
    free(buffer->wd);
    free(buffer);
}

void debug_print_real_lines_state(buffer_t *buffer) __attribute__ ((unused));
void debug_print_real_lines_state(buffer_t *buffer) {
    real_line_t *real_line;
    int i;

    for (real_line = buffer->real_line; real_line != NULL; real_line = real_line->next) {
        printf("%d [", real_line->lineno);
        for (i = 0; i < real_line->cap; ++i) {
            printf("%c", (char)(real_line->glyph_info[i].code));
        }
        printf("]\n");
    }

    printf("------------------------\n");
    
}

void buffer_get_selection(buffer_t *buffer, lpoint_t *start, lpoint_t *end) {
    if (buffer->mark.line == NULL) {
        start->line = NULL;
        start->glyph = -1;
        end->line = NULL;
        end->glyph = -1;
        return;
    }

    if (buffer->mark.line == buffer->cursor.line) {
        start->line = end->line = buffer->cursor.line;
        
        if (buffer->mark.glyph == buffer->cursor.glyph) {
            start->glyph = end->glyph = buffer->mark.glyph;
            return;
        } else if (buffer->mark.glyph < buffer->cursor.glyph) {
            start->glyph = buffer->mark.glyph;
            end->glyph = buffer->cursor.glyph;
        } else {
            end->glyph = buffer->mark.glyph;
            start->glyph = buffer->cursor.glyph;
        }
        
    } else if (buffer->mark.line->lineno < buffer->cursor.line->lineno) {
        copy_lpoint(start, &(buffer->mark));
        copy_lpoint(end, &(buffer->cursor));
    } else {
        copy_lpoint(start, &(buffer->cursor));
        copy_lpoint(end, &(buffer->mark));
    }

    return;
}

int buffer_real_line_count(buffer_t *buffer) {
    real_line_t *cur;
    int count = 0;
    for (cur = buffer->real_line; cur != NULL; cur = cur->next) {
        ++count;
    }
    return count;
}

void buffer_move_cursor(buffer_t *buffer, int direction) {
    buffer->cursor.glyph += direction;
    if (buffer->cursor.glyph < 0) {
        if (buffer->cursor.line->prev != NULL) {
            buffer->cursor.line = buffer->cursor.line->prev;
            buffer->cursor.glyph = buffer->cursor.line->cap;
        } else {
            buffer->cursor.glyph = 0;
        }
    }
    if (buffer->cursor.glyph > buffer->cursor.line->cap) {
        if (buffer->cursor.line->next != NULL) {
            buffer->cursor.glyph = 0;
            buffer->cursor.line = buffer->cursor.line->next;
        } else {
            buffer->cursor.glyph = buffer->cursor.line->cap;
        }
    }
}

void buffer_typeset_maybe(buffer_t *buffer, double width) {
    real_line_t *line;
    double y = buffer->line_height + (buffer->ex_height / 2);
    
    if (fabs(width - buffer->rendered_width) < 0.001) {
        return;
    }

    buffer->rendered_width = width;

    for (line = buffer->real_line; line != NULL; line = line->next) {
        buffer_line_adjust_glyphs(buffer, line, y);
        y += line->y_increment;
    }
}
