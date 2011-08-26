#ifndef __RESHANDLE_H__
#define __RESHANDLE_H__

#include <gtk/gtk.h>

typedef struct _reshandle_t {
    int modified;
    GtkWidget *resdr;
} reshandle_t;

reshandle_t *reshandle_new(void);
void reshandle_free(reshandle_t *reshandle);

#endif
