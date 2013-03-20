#ifndef __OLDSCROLL_H__
#define __OLDSCROLL_H__

#include <gtk/gtk.h>

#include "editor.h"

#define GTK_TYPE_OLDSCROLL (gtk_oldscroll_get_type())
#define GTK_OLDSCROLL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_OLDSCROLL, oldscroll_t))
#define GTK_OLDSCROLL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_OLDSCROLL, oldscroll_class))
#define GTK_IS_OLDSCROLL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_OLDSCROLL))
#define GTK_OLDSCROLL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_OLDSCROLL, oldscroll_class))

struct _oldscroll_t;
typedef struct _oldscroll_t oldscroll_t;

GType gtk_oldscroll_get_type(void) G_GNUC_CONST;
GtkWidget *oldscroll_new(GtkAdjustment *adjustment, editor_t *editor);

#endif
