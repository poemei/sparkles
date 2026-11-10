#ifndef STNLABZ_CORE_ACTIVATION_H
#define STNLABZ_CORE_ACTIVATION_H
#include "stnlabz_abi/module_registry.h"
/* Existing ABI-qualified module admission and shutdown. */
stnlabz_module_result_t stnlabz_core_activate_qualified(stnlabz_module_registry_t *registry,const char *module_id,const stnlabz_module_host_t *host);
int stnlabz_core_stop(stnlabz_module_registry_t *registry,const char *module_id);
int stnlabz_core_stop_all(stnlabz_module_registry_t *registry);
#endif
