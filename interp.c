#include "interp.h"

#include <tcl.h>

#include "global.h"
#include "columns.h"
#include "buffers.h"
#include "column.h"

Tcl_Interp *interp;
editor_t *context_editor;
enum deferred_action deferred_action_to_return;

static int acmacs_exit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    deferred_action_to_return = CLOSE_EDITOR;
    return TCL_OK;
}

static int acmacs_new_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'new', usage: 'new <row|col>'");
        return TCL_ERROR;
        
    }

    if (strcmp(argv[1], "row") == 0) {
        column_new_editor(context_editor->column, null_buffer());
        return TCL_OK;
    } else if (strcmp(argv[1], "col") == 0) {
        columns_new(null_buffer());
        return TCL_OK;
    } 
    
    /* TODO:
       - implement "new" (without arguments)
     */
    
    {
        char *msg;
        asprintf(&msg, "Unknown option to 'new': '%s'", argv[1]);
        Tcl_AddErrorInfo(interp, msg);
        free(msg);
        return TCL_ERROR;
    }

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
    Tcl_CreateCommand(interp, "exit", &acmacs_exit_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "new", &acmacs_new_command, (ClientData)NULL, NULL);
}

void interp_free(void) {
    Tcl_DeleteInterp(interp);
}

enum deferred_action interp_eval(editor_t *editor, const char *command) {
    int code;
    context_editor = editor;
    deferred_action_to_return = NOTHING;
    
    //TODO: switch to buffer directory before executing command
    
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
    }

    Tcl_ResetResult(interp);

    return deferred_action_to_return;
}
