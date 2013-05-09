#ifndef __TEDDY_IPC__
#define __TEDDY_IPC__

#include "buffer.h"

void ipc_init(void);
void ipc_finalize(void);
void ipc_link_to(const char *session_name);

void ipc_event(struct multiqueue *mq, buffer_t *buffer, const char *type, const char *detail);

#endif
