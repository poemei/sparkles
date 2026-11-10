#ifndef STNLABZ_CORE_INVENTORY_H
#define STNLABZ_CORE_INVENTORY_H
#include <stddef.h>
#include "stnlabz_abi/module_registry.h"

/* Immutable value-copy of an ABI registry record; no callback or mutable pointer. */
typedef struct {
    char id[STNLABZ_MODULE_ID_MAX];
    char name[STNLABZ_MODULE_NAME_MAX];
    unsigned int version_major, version_minor, version_patch;
    unsigned int required_api_major, required_api_minor;
    stnlabz_module_state_t state;
    stnlabz_module_qualification_result_t qualification;
    int activation_authorized;
    int active;
    /* The registry does not retain failure causes. Do not invent them. */
    const char *failure_reason;
} stnlabz_core_inventory_entry_t;

size_t stnlabz_core_inventory_count(const stnlabz_module_registry_t *registry);
int stnlabz_core_inventory_get(const stnlabz_module_registry_t *registry,
    size_t index, stnlabz_core_inventory_entry_t *out);
int stnlabz_core_inventory_find(const stnlabz_module_registry_t *registry,
    const char *module_id, stnlabz_core_inventory_entry_t *out);
/* Optional audited snapshot, no changes to registry or module lifecycle. */
int stnlabz_core_inventory_log(const stnlabz_module_registry_t *registry);
#endif
