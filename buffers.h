#ifndef __BUFFERS_H__
#define __BUFFERS_H__

#include "buffer.h"
#include "editor.h"
#include "interp.h"

#include <gtk/gtk.h>

void buffers_init(void);
void buffers_add(buffer_t *buffer);
void buffers_free(void);

buffer_t *null_buffer(void);

// returns non-zero if close was successful, zero if the user cancelled the action
bool buffers_close(buffer_t *buffer, bool save_critbit);
bool buffers_close_all(void);

buffer_t *buffers_find_buffer_from_path(const char *path);

buffer_t *buffers_get_buffer_for_process(void);
buffer_t *buffers_create_with_name(char *name);

int teddy_buffer_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

buffer_t *buffer_id_to_buffer(const char *bufferid);

void word_completer_full_update(void);

void buffers_refresh(buffer_t *buffer);
void buffers_register_tags(const char *tags_file);

extern buffer_t **buffers;
extern int buffers_allocated;

#endif