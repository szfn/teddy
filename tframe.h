#ifndef __FRAME_H__
#define __FRAME_H__

#include <stdbool.h>

#include <gtk/gtk.h>

#define GTK_TYPE_TFRAME (gtk_tframe_get_type())
#define GTK_TFRAME(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_TFRAME, tframe_t))
#define GTK_TFRAME_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_TFRAME, tframe_class))
#define GTK_IS_TFRAME(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_TFRAME))
#define GTK_TFRAME_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_TFRAME, tframe_class))

struct _tframe_t;
struct _columns_t;
typedef struct _tframe_t tframe_t;

GType gtk_tframe_get_type(void) G_GNUC_CONST;
tframe_t *tframe_new(const char *title, GtkWidget *content, struct _columns_t *columns);

void tframe_set_title(tframe_t *tframe, const char *title);
void tframe_set_modified(tframe_t *tframe, bool modified);

double tframe_fraction(tframe_t *tframe);
void tframe_fraction_set(tframe_t *tframe, double fraction);

GtkWidget *tframe_content(tframe_t *tframe);

#endif
