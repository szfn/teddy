#include "interp.h"

#include <tcl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <pty.h>

#include "global.h"
#include "columns.h"
#include "buffers.h"
#include "column.h"
#include "go.h"
#include "baux.h"
#include "jobs.h"
#include "shell.h"
#include "colors.h"

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
    } else if (strcmp(argv[1], "focus_follows_mouse") == 0) {
        ci = &cfg_focus_follows_mouse;
    } else if (strcmp(argv[1], "default_autoindent") == 0) {
        ci = &cfg_default_autoindent;
    } else if (strcmp(argv[1], "editor_bg_color") == 0) {
        ci = &cfg_editor_bg_color;
    } else if (strcmp(argv[1], "editor_fg_color") == 0) {
        ci = &cfg_editor_fg_color;
    } else if (strcmp(argv[1], "posbox_border_color") == 0) {
        ci = &cfg_posbox_border_color;
    } else if (strcmp(argv[1], "posbox_bg_color") == 0) {
        ci = &cfg_posbox_bg_color;
    } else if (strcmp(argv[1], "posbox_fg_color") == 0) {
        ci = &cfg_posbox_fg_color;
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

static int teddy_mark_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'mark' command");
        return TCL_ERROR;
    }

    if (argc != 1) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'mark' command");
        return TCL_ERROR;
    }

    editor_mark_action(context_editor);

    return TCL_OK;
}

static int teddy_cb_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'cb' command");
        return TCL_ERROR;
    }

    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'cb' command");
        return TCL_ERROR;
    }

    if (strcmp(argv[1], "copy") == 0) {
        editor_copy_action(context_editor);
    } else if (strcmp(argv[1], "cut") == 0) {
        editor_cut_action(context_editor);
    } else if (strcmp(argv[1], "paste") == 0) {
        editor_insert_paste(context_editor, default_clipboard);
    } else if (strcmp(argv[1], "ppaste") == 0) {
        editor_insert_paste(context_editor, selection_clipboard);
    } else {
        Tcl_AddErrorInfo(interp, "Wrong argument to 'cb' command");
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int teddy_save_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'save' command");
        return TCL_ERROR;
    }

    if (argc != 1) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'save' command");
        return TCL_ERROR;
    }

    editor_save_action(context_editor);

    return TCL_OK;
}

static int teddy_bufman_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'bufman' command");
        return TCL_ERROR;
    }

    if (argc != 1) {\
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bufman' command");
        return TCL_ERROR;
    }

    buffers_show_window(context_editor);

    return TCL_OK;
}

static int teddy_undo_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'undo' command");
        return TCL_ERROR;
    }

    if (argc != 1) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'undo' command");
        return TCL_ERROR;
    }

    editor_undo_action(context_editor);

    return TCL_OK;
}

static int teddy_search_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'search' command");
        return TCL_ERROR;
    }

    if (argc != 1) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'search' command");
        return TCL_ERROR;
    }

    editor_start_search(context_editor);

    return TCL_OK;
}

static int teddy_focuscmd_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'focuscmd' command");
        return TCL_ERROR;
    }

    if (argc != 1) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'focuscmd' command");
        return TCL_ERROR;
    }

    gtk_widget_grab_focus(context_editor->entry);

    return TCL_OK;
}

enum teddy_move_command_operation_t {
    TMCO_NONE = 0,
    TMCO_DEL,
    TMCO_CUT
};

static int teddy_move_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    int next;
    enum teddy_move_command_operation_t operation = TMCO_NONE;
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'move' command");
        return TCL_ERROR;
    }

    if ((argc < 3) || (argc > 4)) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'move' command, usage: move <prev|next> <char|wnwa|softline> [del|cut]");
        return TCL_ERROR;
    }

    next = (strcmp(argv[1], "next") == 0);

    if (argc == 4) {
        if (strcmp(argv[3], "del") == 0) {
            operation = TMCO_DEL;
        } else if (strcmp(argv[3], "cut") == 0) {
            operation = TMCO_CUT;
        } else {
            Tcl_AddErrorInfo(interp, "Unknown action argument to 'move' command");
            return TCL_ERROR;
        }
    }

    if (operation != TMCO_NONE) {
        editor_mark_action(context_editor);
    }
    
    if (strcmp(argv[2], "char") == 0) {
        editor_move_cursor(context_editor, 0, next ? 1 : -1, MOVE_NORMAL, TRUE);
    } else if (strcmp(argv[2], "softline") == 0) {
        editor_move_cursor(context_editor, next ? 1 : -1, 0, MOVE_NORMAL, TRUE);
    } else if (strcmp(argv[2], "line") == 0) {
        real_line_t *n = NULL;
        n = next ? context_editor->buffer->cursor_line->next : context_editor->buffer->cursor_line->prev;
        if (n != NULL) {
            context_editor->buffer->cursor_line = n;
            editor_complete_move(context_editor, TRUE);
        }
    } else if (strcmp(argv[2], "wnwa") == 0) {
        if (next)
            buffer_aux_wnwa_next(context_editor->buffer);
        else
            buffer_aux_wnwa_prev(context_editor->buffer);
        editor_complete_move(context_editor, TRUE);
    } else {
        if (operation != TMCO_NONE) 
        Tcl_AddErrorInfo(interp, "Unknown argument to 'move' command");
        return TCL_ERROR;
    }

    switch(operation) {
    case TMCO_DEL:
        editor_replace_selection(context_editor, "");
        break;
    case TMCO_CUT:
        editor_cut_action(context_editor);
        break;
    default: // TMCO_NONE -- nothing is done
        break;
    }

    return TCL_OK;
}

static int teddy_gohome_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'gohome' command");
        return TCL_ERROR;
    }

    if (argc != 1) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'gohome' command");
        return TCL_ERROR;
    }

    buffer_aux_go_first_nonws_or_0(context_editor->buffer);
    editor_complete_move(context_editor, TRUE);

    return TCL_OK;
}

static void waitall(void) {
    for (;;) {
        int status;
        pid_t done = wait(&status);
        if (done == -1) {
            if (errno == ECHILD) break; // no more child processes
        } else {
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                fprintf(stderr, "pid %d failed\n", done);
                break;
            }
        }
    }
}

static int teddy_backgrounded_bg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    pid_t child;
    
    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bg' command");
        return TCL_ERROR;
    }

    child = fork();
    if (child == -1) {
        Tcl_AddErrorInfo(interp, "Fork failed");
        return TCL_ERROR;
    } else if (child != 0) {
        /* parent code */

        return TCL_OK;
    }

    {
        int code = Tcl_Eval(interp, argv[1]);
        if (code != TCL_OK) {
            Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);  
            Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
            Tcl_Obj *stackTrace;
            Tcl_IncrRefCount(key);
            Tcl_DictObjGet(NULL, options, key, &stackTrace);
            Tcl_DecrRefCount(key);
            
            fprintf(stderr, "TCL Exception: %s\n", Tcl_GetString(stackTrace));
            waitall();
            exit(EXIT_FAILURE);
        } else {
            waitall();
            exit(atoi(Tcl_GetStringResult(interp)));
        }
    }

    exit(EXIT_SUCCESS); // the child's life end's here (if we didn't exec something before)
}

static int teddy_setenv_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    if (argc != 3) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'setenv' command");
        return TCL_ERROR;
    }

    if (setenv(argv[1], argv[2], 1) == -1) {
        Tcl_AddErrorInfo(interp, "Error executing 'setenv' command");
        return TCL_ERROR;
    } else {
        return TCL_OK;
    }
    
}

static int teddy_bg_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    pid_t child;
    int masterfd;
    buffer_t *buffer;
    struct termios term;
    
    if (context_editor == NULL) {
        Tcl_AddErrorInfo(interp, "No editor open, can not execute 'bg' command");
        return TCL_ERROR;
    }

    if (argc != 2) {
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'bg' command");
        return TCL_ERROR;
    }

    buffer = buffers_get_buffer_for_process();
    if (context_editor->buffer != buffer)
        buffer_cd(buffer, context_editor->buffer->wd);
    
    go_to_buffer(context_editor, buffer);

    bzero(&term, sizeof(struct termios));

    term.c_iflag = IGNCR | IUTF8;
    term.c_oflag = ONLRET;

    child = forkpty(&masterfd, NULL, &term, NULL);
    if (child == -1) {
        Tcl_AddErrorInfo(interp, "Fork failed");
        return TCL_ERROR;
    } else if (child != 0) {
        /* parent code */

        char *msg;
        asprintf(&msg, "~ Executing {%s} on PID %d\n\n", argv[1], child);
        buffer_append(buffer, msg, strlen(msg), TRUE);
        free(msg);

        if (!jobs_register(child, masterfd, buffer)) {
            Tcl_AddErrorInfo(interp, "Registering job failed, probably exceeded the maximum number of jobs available");
            return TCL_ERROR;
        }

        return TCL_OK;
    }

    /* child code here */

    setenv("TERM", "ansi", 1);
    setenv("PAGER", "", 1);
    setenv("SHELL", "teddy", 1);

    Tcl_CreateCommand(interp, "fdopen", &teddy_fdopen_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "fdclose", &teddy_fdclose_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "fddup2", &teddy_fddup2_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "fdpipe", &teddy_fdpipe_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "posixfork", &teddy_posixfork_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "posixexec", &teddy_posixexec_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "posixwaitpid", &teddy_posixwaitpid_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "posixexit", &teddy_posixexit_command, (ClientData)NULL, NULL);
    Tcl_SetVar(interp, "backgrounded", "1", TCL_GLOBAL_ONLY);
    Tcl_CreateCommand(interp, "setenv", &teddy_setenv_command, (ClientData)NULL, NULL);

    Tcl_HideCommand(interp, "unknown", "_non_backgrounded_unknown");
    Tcl_Eval(interp, "rename backgrounded_unknown unknown");
    Tcl_HideCommand(interp, "bg", "_non_backgrounded_bg");
    Tcl_CreateCommand(interp, "bg", &teddy_backgrounded_bg_command, (ClientData)NULL, NULL);

    // Without this tcl screws up the newline character on its own output, it tries to output cr + lf and the terminal converts lf again, resulting in an output of cr + cr + lf, other c programs seem to behave correctly
    Tcl_Eval(interp, "fconfigure stdin -translation binary; fconfigure stdout -translation binary; fconfigure stderr -translation binary");

    {
        int code = Tcl_Eval(interp, argv[1]);
        if (code != TCL_OK) {
            Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);  
            Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
            Tcl_Obj *stackTrace;
            Tcl_IncrRefCount(key);
            Tcl_DictObjGet(NULL, options, key, &stackTrace);
            Tcl_DecrRefCount(key);
            
            fprintf(stderr, "TCL Exception: %s\n", Tcl_GetString(stackTrace));
            waitall();
            exit(EXIT_FAILURE);
        } else {
            waitall();
            exit(atoi(Tcl_GetStringResult(interp)));
        }
    }

    exit(EXIT_SUCCESS); // the child's life end's here (if we didn't exec something before)
}

static int teddy_rgbcolor_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
    switch(argc) {
    case 2: {
        int ret = (long int)g_hash_table_lookup(x11colors, argv[1]);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(ret));
        return TCL_OK;
    }
    case 4: {
        int ret = atoi(argv[3]) + (atoi(argv[2]) << 8) + (atoi(argv[1]) << 16);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(ret));
        return TCL_OK;
    }
    default:
        Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'rgbcolor' command");
        return TCL_ERROR;
    }
}

void interp_init(void) {
    interp = Tcl_CreateInterp();
    if (interp == NULL) {
        printf("Couldn't create TCL interpreter\n");
        exit(EXIT_FAILURE);
    }

    Tcl_SetVar(interp, "backgrounded", "0", 0);
    Tcl_HideCommand(interp, "after", "hidden_after");
    Tcl_HideCommand(interp, "cd", "hidden_cd");
    Tcl_HideCommand(interp, "tcl_endOfWord", "hidden_tcl_endOfWord");
    Tcl_HideCommand(interp, "tcl_findLibrary", "hidden_tcl_findLibrary");
    Tcl_HideCommand(interp, "tcl_startOfPreviousWord", "hidden_tcl_startOfPreviousWord");
    Tcl_HideCommand(interp, "tcl_wordBreakAfter", "hidden_tcl_wordBreakAfter");
    Tcl_HideCommand(interp, "tcl_wordBreakBefore", "hidden_tcl_wordBreakBefore");
    
    Tcl_HideCommand(interp, "exit", "hidden_exit");
    Tcl_CreateCommand(interp, "exit", &teddy_exit_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "setcfg", &teddy_setcfg_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "bindkey", &teddy_bindkey_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "new", &teddy_new_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "pwf", &teddy_pwf_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "pwd", &teddy_pwd_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "go", &teddy_go_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "mark", &teddy_mark_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "cb", &teddy_cb_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "save", &teddy_save_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "bufman", &teddy_bufman_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "undo", &teddy_undo_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "search", &teddy_search_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "focuscmd", &teddy_focuscmd_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "move", &teddy_move_command, (ClientData)NULL, NULL);
    Tcl_CreateCommand(interp, "gohome", &teddy_gohome_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "bg", &teddy_bg_command, (ClientData)NULL, NULL);

    Tcl_CreateCommand(interp, "rgbcolor", &teddy_rgbcolor_command, (ClientData)NULL, NULL);
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
