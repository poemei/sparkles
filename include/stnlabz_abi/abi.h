#ifndef STNLABZ_MODULE_ABI_H
#define STNLABZ_MODULE_ABI_H

/*
 * STN-LABZ
 * Module ABI 1.5
 *
 * abi.h
 *
 * Reusable module lifecycle orchestration.
 */

#include "module.h"
#include "module_registry.h"

stnlabz_module_result_t
stnlabz_module_abi_prepare(
    stnlabz_module_registry_t *registry,
    const stnlabz_module_descriptor_t *descriptor
);

stnlabz_module_result_t
stnlabz_module_abi_authorize_and_activate(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_host_t *host
);

stnlabz_module_result_t
stnlabz_module_abi_stop(
    stnlabz_module_registry_t *registry,
    const char *module_id
);

stnlabz_module_result_t
stnlabz_module_abi_unregister(
    stnlabz_module_registry_t *registry,
    const char *module_id
);

stnlabz_module_result_t
stnlabz_module_abi_prepare_replacement(
    stnlabz_module_registry_t *registry,
    const char *module_id
);

int
stnlabz_module_abi_self_test(void);

#endif
