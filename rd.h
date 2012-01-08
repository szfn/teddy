#ifndef __RD__
#define __RD__

#include <sys/types.h>
#include <dirent.h>

#include "buffer.h"

void rd(DIR *dir, buffer_t *buffer);
void rd_init(void);

#endif