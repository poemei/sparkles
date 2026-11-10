#ifndef STNLABZ_CORE_HOTLOAD_H
#define STNLABZ_CORE_HOTLOAD_H

#include "stnlabz_abi/module_loader.h"
#include "stnlabz_abi/module_registry.h"

typedef struct stnlabz_hotload stnlabz_hotload_t;

stnlabz_hotload_t *stnlabz_hotload_create(
    const char *directory,
    stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader,
    const stnlabz_module_host_t *host);

void stnlabz_hotload_poll(stnlabz_hotload_t *watch);
void stnlabz_hotload_destroy(stnlabz_hotload_t *watch);

#endif
