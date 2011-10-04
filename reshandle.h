#ifndef __RESHANDLE_H__
#define __RESHANDLE_H__

#include <gtk/gtk.h>

typedef struct _reshandle_t {
	int modified;
	GtkWidget *resdr;

	/* associated editor/column */
	struct _column_t *column;
	struct _editor_t *editor;

	/* resize informations */
	double origin_x, origin_y;
} reshandle_t;

reshandle_t *reshandle_new(struct _column_t *column, struct _editor_t *editor);
void reshandle_free(reshandle_t *reshandle);

#endif
