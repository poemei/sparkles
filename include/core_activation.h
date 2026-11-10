#ifndef STNLABZ_CORE_ACTIVATION_H
#define STNLABZ_CORE_ACTIVATION_H

#include "core_policy.h"
#include "stnlabz_abi/module_registry.h"

stnlabz_module_result_t stnlabz_core_authorize(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_policy_request_t *request);

stnlabz_module_result_t stnlabz_core_activate(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_host_t *host,
    const stnlabz_policy_request_t *request);

/*
 * Sparkles qualified module admission.
 * A module reaches this boundary only after Core verification and qualification.
 * No external account or conversational identity participates in this decision.
 */
stnlabz_module_result_t stnlabz_core_activate_qualified(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_host_t *host);

int stnlabz_core_stop(stnlabz_module_registry_t *registry, const char *module_id);
int stnlabz_core_stop_all(stnlabz_module_registry_t *registry);

#endif
