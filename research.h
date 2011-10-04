#ifndef __RESEARCH__
#define __RESEARCH__

#include <gtk/gtk.h>
#include <tcl.h>

extern void research_init(GtkWidget *window);
extern int teddy_research_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif
