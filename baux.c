#include "baux.h"

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

