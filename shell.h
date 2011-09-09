#ifndef __SHELL_H__
#define __SHELL_H__

#include <tcl.h>

int teddy_fdopen_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_fdclose_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_fddup2_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_fdpipe_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_posixfork_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_posixexec_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_posixwaitpid_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_posixexit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif
