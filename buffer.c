#include "buffer.h"

static void init_line(line_t *line) {
    line->text = NULL;
    line->allocated_text = 0;
    line->text_cap = 0;
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
    if (!fin) {
        perror("Couldn't open input file");
        exit(EXIT_FAILURE);
    }

    while ((ch = fgetc(fin)) != EOF) {
        if (buffer->lines_cap >= buffer->allocated_lines) {
            grow_lines(buffer);
        }
        if (ch == '\n') {
            ++(buffer->lines_cap);
        } else {
            if (buffer->lines[buffer->lines_cap].text_cap >= buffer->lines[buffer->lines_cap].allocated_text) {
                grow_line(buffer->lines+buffer->lines_cap);
            }
            buffer->lines[buffer->lines_cap].text[buffer->lines[buffer->lines_cap].text_cap] = ch;
            ++(buffer->lines[buffer->lines_cap].text_cap);
        }
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
    */
}
