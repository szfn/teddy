#include "baux.h"

#include <unicode/uchar.h>

void buffer_aux_go_first_nonws_or_0(buffer_t *buffer) {
    int old_cursor_glyph = buffer->cursor_glyph;
    buffer_aux_go_first_nonws(buffer);
    if (old_cursor_glyph == buffer->cursor_glyph) {
        buffer->cursor_glyph = 0;
    }
}

void buffer_aux_go_first_nonws(buffer_t *buffer) {
    int i;
    for (i = 0; i < buffer->cursor_line->cap; ++i) {
        uint32_t code = buffer->cursor_line->glyph_info[i].code;
        if ((code != 0x20) && (code != 0x09)) break;
    }
    buffer->cursor_glyph = i;
}

void buffer_aux_go_end(buffer_t *buffer) {
    buffer->cursor_glyph = buffer->cursor_line->cap;
}

void buffer_aux_go_char(buffer_t *buffer, int n) {
    buffer->cursor_glyph = n;
    if (buffer->cursor_glyph > buffer->cursor_line->cap) buffer->cursor_glyph = buffer->cursor_line->cap;
    if (buffer->cursor_glyph < 0) buffer->cursor_glyph = 0;
}

void buffer_aux_go_line(buffer_t *buffer, int n) {
    real_line_t *cur, *prev;
    for (cur = buffer->real_line; cur != NULL; cur = cur->next) {
        if (cur->lineno == n) {
            buffer->cursor_line = cur;
            buffer->cursor_glyph = 0;
            return;
        }
        prev = cur;
    }
    if (cur == NULL) {
        buffer->cursor_line = prev;
        buffer->cursor_glyph = 0;
    }
}

void buffer_aux_wnwa_next(buffer_t *buffer) {
    UBool searching_alnum;
    if (buffer->cursor_glyph >= buffer->cursor_line->cap) return;

    searching_alnum = !u_isalnum(buffer->cursor_line->glyph_info[buffer->cursor_glyph].code);

    for ( ; buffer->cursor_glyph < buffer->cursor_line->cap; ++(buffer->cursor_glyph)) {
        if (u_isalnum(buffer->cursor_line->glyph_info[buffer->cursor_glyph].code) == searching_alnum) break;
    }
}

void buffer_aux_wnwa_prev(buffer_t *buffer) {
    UBool searching_alnum;
    if (buffer->cursor_glyph <= 0) return;

    --(buffer->cursor_glyph);

    searching_alnum = !u_isalnum(buffer->cursor_line->glyph_info[buffer->cursor_glyph].code);

    for ( ; buffer->cursor_glyph >= 0; --(buffer->cursor_glyph)) {
        if (u_isalnum(buffer->cursor_line->glyph_info[buffer->cursor_glyph].code) == searching_alnum) break;
    }

    ++(buffer->cursor_glyph);
}

void buffer_indent_newline(buffer_t *buffer, char *r) {
    int i = 0;
    r[0] = '\n';
    for ( ; i < buffer->cursor_line->cap; ++i) {
        uint32_t code = buffer->cursor_line->glyph_info[i].code;
        if (code == 0x20) {
            r[i+1] = ' ';
        } else if (code == 0x09) {
            r[i+1] = '\t';
        } else {
            r[i+1] = '\0';
            break;
        }
    }
}
