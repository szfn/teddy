#include "go.h"

#include "interp.h"
#include "baux.h"
#include "ctype.h"
#include "editor.h"

int exec_char_specifier(const char *specifier, int really_exec) {
    long int n1;
    
    if ((strcmp(specifier, "^") == 0) || (strcmp(specifier, ":^") == 0)) {
        if (really_exec) buffer_aux_go_first_nonws(context_editor->buffer);
        return 1;
    }
    
    if ((strcmp(specifier, "$") == 0) || (strcmp(specifier, ":$") == 0)) {
        if (really_exec) buffer_aux_go_end(context_editor->buffer);
        return 1;
    }
    
    if (specifier[0] == ':') {
        n1 = strtol(specifier+1, NULL, 10);
        if (n1 != LONG_MIN) {
            if (really_exec) buffer_aux_go_char(context_editor->buffer, (int)n1);
            return 1;
        }
    }

    return 0;
}

int isnumber(const char *str) {
    const char *c;
    for (c = str; *c != '\0'; ++c) {
        if (!isdigit(*c)) return 0;
    }
    return 1;
}

void exec_go(const char *specifier) {
    long int n1;
    char *pos;

    if (exec_char_specifier(specifier, 1)) return;
    
    if (isnumber(specifier)) {
        n1 = strtol(specifier, NULL, 10);        
        printf("Line\n");
        buffer_aux_go_line(context_editor->buffer, (int)n1);
        return;
    }

    pos = strchr(specifier, ':');
    if ((pos != NULL) && (pos != specifier)) {
        char *c = alloca(sizeof(char) * (pos - specifier + 1));
        strncpy(c, specifier, pos - specifier);
        c[pos - specifier] = '\0';
        if (isnumber(c)) {
            n1 = strtol(c, NULL, 10);
            if (exec_char_specifier(pos, 0)) {
                printf("Line + char\n");
                buffer_aux_go_line(context_editor->buffer, (int)n1);
                exec_char_specifier(pos, 1);
                return;
            }
        }
    }

    //TODO: then it could be a buffer or a filename
}

int acmacs_go_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'go', usage: 'go [<filename>][\":[\"[\"][<line-specifier>][\":\"][<column-specifier][\"]\"]]");
        return TCL_ERROR;
    }

    exec_go(argv[1]);

    editor_center_on_cursor(context_editor);
    gtk_widget_queue_draw(context_editor->drar);
    return TCL_OK;
}


