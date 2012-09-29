#ifndef __CMDCOMPL__
#define __CMDCOMPL__

#include "critbit.h"
#include "compl.h"

void cmdcompl_init(void);
void cmdcompl_recalc(struct completer *c, const char *prefix);
bool in_external_commands(const char *arg);

#endif