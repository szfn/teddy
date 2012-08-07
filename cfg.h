#ifndef __CFG_H__
#define __CFG_H__

#include "cfg_auto.h"

typedef struct _config_item_t {
	char *strval;
	int intval;
} config_item_t;

typedef struct _config_t {
	struct _config_t *parent;
	config_item_t *cfg[CONFIG_NUM];
} config_t;

extern config_t global_config;
extern const char *config_names[];

void config_init(config_t *config, config_t *parent);
void config_init_auto_defaults(void);

char *config_strval(config_t *config, int idx);
int config_intval(config_t *config, int idx);
void config_set(config_t *config, int idx, char *val);

#endif
