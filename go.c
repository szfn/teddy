#include "go.h"

#include "interp.h"
#include "baux.h"
#include "ctype.h"
#include "editor.h"
#include "buffers.h"
#include "global.h"
#include "columns.h"

static int exec_char_specifier(const char *specifier, int really_exec, editor_t *context_editor) {
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

static int isnumber(const char *str) {
    const char *c = str;
    
    if ((*c == '+') || (*c == '-')) ++c;
    
    for (; *c != '\0'; ++c) {
        if (!isdigit(*c)) return 0;
    }
    return 1;
}

static int exec_go_position(const char *specifier, editor_t *context_editor) {
    long int n1;
    char *pos;

    if (exec_char_specifier(specifier, 1, context_editor)) return 1;
    
    if (isnumber(specifier)) {
        n1 = strtol(specifier, NULL, 10);        
        printf("Line\n");
        buffer_aux_go_line(context_editor->buffer, (int)n1);
        return 1;
    }

    pos = strchr(specifier, ':');
    if ((pos != NULL) && (pos != specifier)) {
        char *c = alloca(sizeof(char) * (pos - specifier + 1));
        strncpy(c, specifier, pos - specifier);
        c[pos - specifier] = '\0';
        if (isnumber(c)) {
            n1 = strtol(c, NULL, 10);
            if (exec_char_specifier(pos, 0, context_editor)) {
                printf("Line + char\n");
                buffer_aux_go_line(context_editor->buffer, (int)n1);
                exec_char_specifier(pos, 1, context_editor);
                return 1;
            } else {
                return 0;
            }
        }
    }

    return 0;
}

editor_t *go_to_buffer(editor_t *editor, buffer_t *buffer) {
    editor_t *target = columns_get_buffer(buffer);
    if (target != NULL) {
        gtk_widget_grab_focus(target->drar);
        deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
        return target;
    }
    
    //TODO: here we should ask what to do and potentially open a new editor
    editor_switch_buffer(editor, buffer);
    chdir(editor->buffer->wd);
    return editor;
}

int exec_go(const char *specifier) {
     char *sc = NULL;
     char *saveptr, *tok;
     char *urp;
     buffer_t *buffer = NULL;
     editor_t *editor = NULL;
     int retval;

     if (exec_go_position(specifier, context_editor)) {
         retval = 1;
         goto exec_go_cleanup;
     }

     sc = malloc(sizeof(char) * (strlen(specifier) + 1));
     strcpy(sc, specifier);

     tok = strtok_r(sc, ":", &saveptr);
     if (tok == NULL) { retval = 0; goto exec_go_cleanup; }

     urp = unrealpath(context_editor->buffer->path, tok);

     buffer = buffers_find_buffer_from_path(urp);
     if (buffer == NULL) {
         buffer = buffer_create(&library);
         if (load_text_file(buffer, urp) != 0) {
             buffer_free(buffer);
             retval = 0;
             goto exec_go_cleanup;
         } 
         buffers_add(buffer);
    }

    editor = go_to_buffer(context_editor, buffer);

    tok = strtok_r(sc, ":", &saveptr);
    if (tok == NULL) { retval = 1; goto exec_go_cleanup; }
    if (strlen(tok) == 0) { retval = 1; goto exec_go_cleanup; }

    if (tok[0] == '[') ++tok;
    if (tok[strlen(tok)-1] == ']') tok[strlen(tok)-1] = '\0';

    exec_go_position(tok, context_editor);

    retval = 1;

 exec_go_cleanup:
    if (sc != NULL) free(sc);
    return retval;
}

int acmacs_go_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'go', usage: 'go [<filename>][\":[\"[\"][<line-specifier>][\":\"][<column-specifier][\"]\"]]");
        return TCL_ERROR;
    }

    if (!exec_go(argv[1])) {
        //TODO:
        // - ask user if one wants to create a new file
        Tcl_AddErrorInfo(interp, "Command 'go' couldn't understand its argument, usage: 'go [<filename>][\":[\"[\"][<line-specifier>][\":\"][<column-specifier][\"]\"]]");
        return TCL_ERROR;
    } else {
        editor_center_on_cursor(context_editor);
        gtk_widget_queue_draw(context_editor->drar);
        return TCL_OK;
    }
}


