#ifndef SPARKLES_MODULE_HOST_H
#define SPARKLES_MODULE_HOST_H

#include "stnlabz_abi/module.h"

const stnlabz_module_host_t *sparkles_module_host(void);
void sparkles_module_host_begin_registration(const char *module_id);
void sparkles_module_host_end_registration(void);

#endif
