#include "baux.h"

#include <unicode/uchar.h>

#include "global.h"

void buffer_aux_go_first_nonws_or_0(buffer_t *buffer) {
    int old_cursor_glyph = buffer->cursor.glyph;
    buffer_aux_go_first_nonws(buffer);
    if (old_cursor_glyph == buffer->cursor.glyph) {
        buffer->cursor.glyph = 0;
    }
}

void buffer_aux_go_first_nonws(buffer_t *buffer) {
    int i;
    for (i = 0; i < buffer->cursor.line->cap; ++i) {
        uint32_t code = buffer->cursor.line->glyph_info[i].code;
        if ((code != 0x20) && (code != 0x09)) break;
    }
    buffer->cursor.glyph = i;
}

void buffer_aux_go_end(buffer_t *buffer) {
    if (cfg_default_spaceman.intval) {
        buffer_line_clean_trailing_spaces(buffer, buffer->cursor.line);
    }
    buffer->cursor.glyph = buffer->cursor.line->cap;
}

void buffer_aux_go_char(buffer_t *buffer, int n) {
    buffer->cursor.glyph = n;
    if (buffer->cursor.glyph > buffer->cursor.line->cap) buffer->cursor.glyph = buffer->cursor.line->cap;
    if (buffer->cursor.glyph < 0) buffer->cursor.glyph = 0;
}

void buffer_aux_go_line(buffer_t *buffer, int n) {
    real_line_t *cur, *prev;
    for (cur = buffer->real_line; cur != NULL; cur = cur->next) {
        if (cur->lineno+1 == n) {
            buffer->cursor.line = cur;
            buffer->cursor.glyph = 0;
            return;
        }
        prev = cur;
    }
    if (cur == NULL) {
        buffer->cursor.line = prev;
        buffer->cursor.glyph = 0;
    }
}

static UBool u_isalnum_or_underscore(uint32_t code) {
    return u_isalnum(code) || (code == 0x5f);
}

void buffer_aux_wnwa_next_ex(lpoint_t *point) {
    UBool searching_alnum;
    if (point->glyph >= point->line->cap) return;

    searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

    for ( ; point->glyph < point->line->cap; ++(point->glyph)) {
        if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) break;
    }
}

void buffer_aux_wnwa_next(buffer_t *buffer) {
    buffer_aux_wnwa_next_ex(&(buffer->cursor));
}

void buffer_aux_wnwa_prev_ex(lpoint_t *point) {
    UBool searching_alnum;
    if (point->glyph <= 0) return;

    --(point->glyph);

    searching_alnum = !u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code);

    for ( ; point->glyph >= 0; --(point->glyph)) {
        if (u_isalnum_or_underscore(point->line->glyph_info[point->glyph].code) == searching_alnum) break;
    }

    ++(point->glyph);
}

void buffer_aux_wnwa_prev(buffer_t *buffer) {
    buffer_aux_wnwa_prev_ex(&(buffer->cursor));
}

void buffer_indent_newline(buffer_t *buffer, char *r) {
    int i = 0;
    r[0] = '\n';
    for ( ; i < buffer->cursor.line->cap; ++i) {
        uint32_t code = buffer->cursor.line->glyph_info[i].code;
        if (code == 0x20) {
            r[i+1] = ' ';
        } else if (code == 0x09) {
            r[i+1] = '\t';
        } else {
            r[i+1] = '\0';
            break;
        }
    }
    r[i+1] = '\0';
}

void buffer_append(buffer_t *buffer, const char *msg, int length, int on_new_line) {
    char *text;

    buffer_unset_mark(buffer);
    
    for (; buffer->cursor.line->next != NULL; buffer->cursor.line = buffer->cursor.line->next);
    buffer->cursor.glyph = buffer->cursor.line->cap;
    //printf("buffer_append %d %d\n", buffer->cursor.glyph, buffer->cursor.line->cap);

    if (on_new_line) {
        if (buffer->cursor.glyph != 0) {
            buffer_replace_selection(buffer, "\n");
        }
    }

    text = malloc(sizeof(char) * (length + 1));
    strncpy(text, msg, length);
    text[length] = '\0';

    buffer_replace_selection(buffer, text);
    
    free(text);
}
