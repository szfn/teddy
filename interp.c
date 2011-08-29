#include "interp.h"

#include <tcl.h>

#include "global.h"
#include "columns.h"
#include "buffers.h"
#include "column.h"
#include "go.h"

#define INITFILE ".teddy"

Tcl_Interp *interp;
editor_t *context_editor = NULL;
enum deferred_action deferred_action_to_return;

static int teddy_exit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'exit' command");
        return TCL_ERROR;
    }

    deferred_action_to_return = CLOSE_EDITOR;
    return TCL_OK;
}

static int teddy_new_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (argc > 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'new', usage: 'new <row|col>'");
        return TCL_ERROR;
    }

    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'new' command");
        return TCL_ERROR;
    }

    if (argc == 1) {
        editor_t *n = heuristic_new_frame(context_editor, null_buffer());
        if (n != NULL) {
            gtk_widget_grab_focus(n->drar);
            deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
        }
        return TCL_OK;
    }

    if (strcmp(argv[1], "row") == 0) {
        editor_t *n = column_new_editor(context_editor->column, null_buffer());
        if (n != NULL) {
            gtk_widget_grab_focus(n->drar);
            deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
        }
        return TCL_OK;
    } else if (strcmp(argv[1], "col") == 0) {
        editor_t *n = columns_new(null_buffer());
        if (n != NULL) {
            gtk_widget_grab_focus(n->drar);
            deferred_action_to_return = FOCUS_ALREADY_SWITCHED;
        }
        return TCL_OK;
    } 
    
    {
        char *msg;
        asprintf(&msg, "Unknown option to 'new': '%s'", argv[1]);
        Tcl_AddErrorInfo(interp, msg);
        free(msg);
        return TCL_ERROR;
    }
}

static int teddy_pwf_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'pwf' command");
        return TCL_ERROR;
    }

    Tcl_SetResult(interp, context_editor->buffer->path, TCL_VOLATILE);
    return TCL_OK;
}

static int teddy_pwd_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'pwf' command");
        return TCL_ERROR;
    }

    Tcl_SetResult(interp, context_editor->buffer->wd, TCL_VOLATILE);
    return TCL_OK;
}

static int teddy_setcfg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    config_item_t *ci = NULL;
    
    if (argc != 3) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to setcfg");
        return TCL_ERROR;
    }

    if (strcmp(argv[1], "main_font") == 0) {
        ci = &cfg_main_font;
    } else if (strcmp(argv[1], "posbox_font") == 0) {
        ci = &cfg_posbox_font;
    }

    if (ci == NULL) {
        Tcl_AddErrorInfo(interp, "Unknown configuration option specified in setcfg");
        return TCL_ERROR;
    }

    setcfg(ci, argv[2]);
    return TCL_OK;
}

static int teddy_bindkey_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    char *key, *value;
    
    if (argc != 3) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to setcfg");
        return TCL_ERROR;
    }

    key = malloc(sizeof(char) * (strlen(argv[1]) + 1));
    strcpy(key, argv[1]);
    value = malloc(sizeof(char) * (strlen(argv[2]) + 1));
    strcpy(value, argv[2]);
    
    g_hash_table_replace(keybindings, key, value);

    return TCL_OK;
}

void interp_init(void) {
    interp = Tcl_CreateInterp();
    if (interp == NULL) {
        printf("Couldn't create TCL interpreter\n");
        exit(EXIT_FAILURE);
    }
    
    Tcl_HideCommand(interp, "after", "hidden_after");
    Tcl_HideCommand(interp, "cd", "hidden_cd");
    
    Tcl_HideCommand(interp, "exit", "hidden_exit");
    Tcl_CreateCommand(interp, "exit", &teddy_exit_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "setcfg", &teddy_setcfg_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "bindkey", &teddy_bindkey_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "new", &teddy_new_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "pwf", &teddy_pwf_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "pwd", &teddy_pwd_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "go", &teddy_go_command, (ClientData)NULL, NULL);
}

void interp_free(void) {
    Tcl_DeleteInterp(interp);
}

enum deferred_action interp_eval(editor_t *editor, const char *command) {
    int code;
    
    context_editor = editor;
    deferred_action_to_return = NOTHING;

    chdir(context_editor->buffer->wd);
    
    code = Tcl_Eval(interp, command);
        
    if (code != TCL_OK) {
        Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);  
        Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
        Tcl_Obj *stackTrace;
        Tcl_IncrRefCount(key);
        Tcl_DictObjGet(NULL, options, key, &stackTrace);
        Tcl_DecrRefCount(key);

        quick_message(context_editor, "TCL Error", Tcl_GetString(stackTrace));
        //TODO: if the error string is very long use a buffer instead
    } else {
        const char *result = Tcl_GetStringResult(interp);
        if (strcmp(result, "") != 0) {
            quick_message(context_editor, "TCL Result", result);
        }
    }

    Tcl_ResetResult(interp);

    return deferred_action_to_return;
}

void read_conf(void) {
    const char *home = getenv("HOME");
    char *name;

    asprintf(&name, "%s/%s", home, INITFILE);
    
    int code = Tcl_EvalFile(interp, name);
    
    if (code != TCL_OK) {
        Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);  
        Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
        Tcl_Obj *stackTrace;
        Tcl_IncrRefCount(key);
        Tcl_DictObjGet(NULL, options, key, &stackTrace);
        Tcl_DecrRefCount(key);

        printf("TCL Error: %s\n", Tcl_GetString(stackTrace));
        exit(EXIT_FAILURE);
    }
    
    free(name);
}
