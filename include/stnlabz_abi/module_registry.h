#ifndef STNLABZ_MODULE_REGISTRY_H
#define STNLABZ_MODULE_REGISTRY_H

/*
 * STN-LABZ
 * Module ABI 1.4
 *
 * module_registry.h
 *
 * Core-controlled module lifecycle registry.
 */

#include <stddef.h>

#include "module.h"




/*
 * ------------------------------------------------
 * MODULE RECORD
 * ------------------------------------------------
 */

typedef struct
{
    stnlabz_module_descriptor_t descriptor;

    stnlabz_module_state_t state;

    stnlabz_module_qualification_result_t qualification;

    int activation_authorized;

} stnlabz_module_record_t;


/*
 * ------------------------------------------------
 * MODULE REGISTRY
 * ------------------------------------------------
 */

typedef struct
{
    stnlabz_module_record_t *modules;

    size_t count;
    size_t capacity;

} stnlabz_module_registry_t;


/*
 * ------------------------------------------------
 * REGISTRY API
 * ------------------------------------------------
 */

void
stnlabz_module_registry_init(
    stnlabz_module_registry_t *registry
);

/* Call on an initialized registry after all modules are stopped/unregistered. */
void stnlabz_module_registry_destroy(stnlabz_module_registry_t *registry);


stnlabz_module_result_t
stnlabz_module_registry_discover(
    stnlabz_module_registry_t *registry,
    const stnlabz_module_descriptor_t *descriptor
);


stnlabz_module_result_t
stnlabz_module_registry_verify(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


stnlabz_module_result_t
stnlabz_module_registry_qualify(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


stnlabz_module_result_t
stnlabz_module_registry_restore_qualification(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_qualification_result_t *qualification
);


stnlabz_module_result_t
stnlabz_module_registry_authorize_activation(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


stnlabz_module_result_t
stnlabz_module_registry_activate(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


/*
 * ACTIVE -> STOPPED
 *
 * Qualification evidence is retained.
 * Activation authority is cleared.
 */
stnlabz_module_result_t
stnlabz_module_registry_stop(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


/*
 * Remove a non-running module from the registry.
 *
 * Core owns any corresponding lifecycle audit record.
 *
 * ACTIVE modules cannot be unregistered.
 */
stnlabz_module_result_t
stnlabz_module_registry_unregister(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


stnlabz_module_result_t
stnlabz_module_registry_fail(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


stnlabz_module_result_t
stnlabz_module_registry_quarantine(
    stnlabz_module_registry_t *registry,
    const char *module_id
);


const stnlabz_module_record_t *
stnlabz_module_registry_find(
    const stnlabz_module_registry_t *registry,
    const char *module_id
);


#endif
