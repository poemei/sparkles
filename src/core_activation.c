#include <stdio.h>
#include <string.h>
#include "core_activation.h"
#include "abi.h" /* Shared STN-LABZ ABI lifecycle declarations. */
#include "logging.h"
#include "module_host.h"

static int audit(const char *event, const char *id,
                 stnlabz_module_result_t result,
                 const stnlabz_module_record_t *record)
{
    char detail[320];
    (void)snprintf(detail, sizeof detail,
        "module=%.63s version=%u.%u.%u result=%s state=%s authorized=%d",
        id ? id : "unknown",
        record ? record->descriptor.version_major : 0,
        record ? record->descriptor.version_minor : 0,
        record ? record->descriptor.version_patch : 0,
        stnlabz_module_result_string(result),
        record ? stnlabz_module_state_string(record->state) : "NOT_FOUND",
        record ? record->activation_authorized : 0);
    return stnlabz_log_event("MODULE", event, detail) && stnlabz_log_sync();
}

stnlabz_module_result_t stnlabz_core_activate_qualified(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_host_t *host)
{
    const stnlabz_module_record_t *record;
    stnlabz_module_result_t rc;

    if (!registry || !module_id || !stnlabz_log_is_open())
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    record = stnlabz_module_registry_find(registry, module_id);
    if (!record) return STNLABZ_MODULE_ERR_NOT_FOUND;
    if (record->state != STNLABZ_MODULE_STATE_QUALIFIED &&
        record->state != STNLABZ_MODULE_STATE_STOPPED)
        return STNLABZ_MODULE_ERR_NOT_QUALIFIED;

    sparkles_module_host_begin_registration(module_id);
    rc = stnlabz_module_abi_authorize_and_activate(registry, module_id, host);
    sparkles_module_host_end_registration();
    if (!audit(rc == STNLABZ_MODULE_OK ? "AUTO_ACTIVE" : "AUTO_ACTIVATION_FAILED",
               module_id, rc, stnlabz_module_registry_find(registry, module_id)))
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    return rc;
}

int stnlabz_core_stop(stnlabz_module_registry_t *registry, const char *module_id)
{
    const stnlabz_module_record_t *record;
    stnlabz_module_result_t rc;
    if (!registry || !module_id || !stnlabz_log_is_open()) return 0;
    record = stnlabz_module_registry_find(registry, module_id);
    if (!record || record->state != STNLABZ_MODULE_STATE_ACTIVE) return 0;
    if (!audit("STOP_REQUEST", module_id, STNLABZ_MODULE_OK, record))
        return 0;
    rc = stnlabz_module_abi_stop(registry, module_id);
    if (!audit(rc == STNLABZ_MODULE_OK ? "STOP_PASS" : "STOP_FAILED",
               module_id, rc, stnlabz_module_registry_find(registry, module_id)))
        return 0;
    return rc == STNLABZ_MODULE_OK;
}

int stnlabz_core_stop_all(stnlabz_module_registry_t *registry)
{
    size_t i;
    int good = 1;
    if (!registry || !stnlabz_log_is_open()) return 0;
    for (i = registry->count; i > 0; --i) {
        stnlabz_module_record_t *record = &registry->modules[i - 1];
        if (record->state != STNLABZ_MODULE_STATE_ACTIVE) continue;
        if (!stnlabz_core_stop(registry, record->descriptor.id)) good = 0;
    }
    return good;
}
