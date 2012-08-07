#include "cfg.h"

#include <stdlib.h>
#include <string.h>

#include "global.h"

config_t global_config;

void config_init(config_t *config, config_t *parent) {
	config->parent = parent;
	for (int i = 0; i < CONFIG_NUM; ++i) config->cfg[i] = NULL;
}

static config_item_t *config_get(config_t *config, int idx) {
	if (config->cfg[idx] != NULL) {
		return config->cfg[idx];
	} else {
		if (config->parent != NULL) {
			return config_get(config->parent, idx);
		} else {
			return NULL;
		}
	}
}

char *config_strval(config_t *config, int idx) {
	config_item_t *cit = config_get(config, idx);
	return (cit != NULL) ? cit->strval : NULL;
}

int config_intval(config_t *config, int idx) {
	config_item_t *cit = config_get(config, idx);
	return (cit != NULL) ? cit->intval : 0;
}

void config_set(config_t *config, int idx, char *val) {
	if (config->cfg[idx] == NULL) {
		config->cfg[idx] = malloc(sizeof(config_item_t));
		alloc_assert(config->cfg[idx]);
	}

	config->cfg[idx]->strval = strdup(val);
	alloc_assert(config->cfg[idx]->strval);
	config->cfg[idx]->intval = atoi(val);
}
