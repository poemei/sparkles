#include <stdio.h>
#include <string.h>
#include "core_inventory.h"
#include "logging.h"

size_t stnlabz_core_inventory_count(const stnlabz_module_registry_t *registry)
{
    return registry ? registry->count : 0;
}

int stnlabz_core_inventory_get(const stnlabz_module_registry_t *registry,
    size_t index, stnlabz_core_inventory_entry_t *out)
{
    const stnlabz_module_record_t *record;
    if (!registry || !out || index >= registry->count || !registry->modules)
        return 0;
    record = &registry->modules[index];
    memset(out, 0, sizeof(*out));
    memcpy(out->id, record->descriptor.id, sizeof(out->id));
    memcpy(out->name, record->descriptor.name, sizeof(out->name));
    out->version_major = record->descriptor.version_major;
    out->version_minor = record->descriptor.version_minor;
    out->version_patch = record->descriptor.version_patch;
    out->required_api_major = record->descriptor.required_core_api_major;
    out->required_api_minor = record->descriptor.required_core_api_minor;
    out->state = record->state;
    out->qualification = record->qualification;
    out->activation_authorized = record->activation_authorized;
    out->active = record->state == STNLABZ_MODULE_STATE_ACTIVE;
    out->failure_reason = "not_recorded_in_registry";
    return 1;
}

int stnlabz_core_inventory_find(const stnlabz_module_registry_t *registry,
    const char *module_id, stnlabz_core_inventory_entry_t *out)
{
    size_t i;
    if (!registry || !module_id || !out) return 0;
    for (i = 0; i < registry->count; ++i) {
        if (strcmp(registry->modules[i].descriptor.id, module_id) == 0)
            return stnlabz_core_inventory_get(registry, i, out);
    }
    return 0;
}

int stnlabz_core_inventory_log(const stnlabz_module_registry_t *registry)
{
    stnlabz_core_inventory_entry_t entry;
    char detail[512];
    size_t i;
    if (!registry || !stnlabz_log_is_open()) return 0;
    (void)snprintf(detail, sizeof detail, "count=%zu", registry->count);
    if (!stnlabz_log_event("INVENTORY", "SNAPSHOT", detail) || !stnlabz_log_sync())
        return 0;
    for (i = 0; i < registry->count; ++i) {
        if (!stnlabz_core_inventory_get(registry, i, &entry)) return 0;
        (void)snprintf(detail, sizeof detail,
            "id=%.63s name=%.127s version=%u.%u.%u required_api=%u.%u state=%s tests=%u passed=%u failed=%u negative_executed=%d negative_passed=%d authorized=%d active=%d failure_reason=%s",
            entry.id, entry.name,
            entry.version_major, entry.version_minor, entry.version_patch,
            entry.required_api_major, entry.required_api_minor,
            stnlabz_module_state_string(entry.state),
            entry.qualification.tests_executed, entry.qualification.tests_passed,
            entry.qualification.tests_failed,
            entry.qualification.negative_test_executed,
            entry.qualification.negative_test_passed,
            entry.activation_authorized, entry.active, entry.failure_reason);
        if (!stnlabz_log_event("INVENTORY", "ENTRY", detail) || !stnlabz_log_sync())
            return 0;
    }
    return 1;
}
