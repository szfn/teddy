#ifndef __TEDDY_IPC__
#define __TEDDY_IPC__

#include "buffer.h"

void ipc_init(void);
void ipc_link_to(const char *session_name);
void ipc_event(buffer_t *buffer, const char *description, const char *arg1);
void ipc_finalize(void);

#endif
