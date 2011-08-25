#include "interp.h"

#include <tcl.h>

#include "global.h"
#include "column.h"

Tcl_Interp *interp;
editor_t *context_editor;
enum deferred_action deferred_action_to_return;

static int acmacs_exit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    deferred_action_to_return = CLOSE_EDITOR;
    return TCL_OK;
}

static int acmacs_new_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    column_new_editor(column, context_editor->buffer);
    /* TODO:
       - reassign current implementation to "new row"
       - implement "new col"
       - implement "new" (without arguments)
     */
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
