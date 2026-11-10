#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include "core_discovery.h"
#include "logging.h"

static int audit(const char *event, const char *detail)
{
    return stnlabz_log_event("MODULE", event, detail) && stnlabz_log_sync();
}

int stnlabz_core_discover(stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader, const char *path,
    stnlabz_module_discovery_report_t *report)
{
    stnlabz_module_result_t rc;
    char detail[512];
    if (!registry || !loader || !path || !report || !stnlabz_log_is_open())
        return 0;
    {
        struct stat st;
        if (stat(path, &st) != 0) {
            int error = errno;
            if (error != ENOENT) {
                (void)snprintf(detail, sizeof detail,
                    "path=%.360s errno=%d reason=%s", path, error, strerror(error));
                (void)audit("DIRECTORY_FAILED", detail);
                return 0;
            }
            if (mkdir(path, 0755) != 0) {
                error = errno;
                /* A concurrent creator is fine only if the result is a directory. */
                if (error != EEXIST || stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    (void)snprintf(detail, sizeof detail,
                        "path=%.360s errno=%d reason=%s", path, error, strerror(error));
                    (void)audit("DIRECTORY_FAILED", detail);
                    return 0;
                }
            } else {
                (void)snprintf(detail, sizeof detail, "path=%.400s status=created", path);
                if (!audit("DIRECTORY_CREATED", detail)) return 0;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            (void)snprintf(detail, sizeof detail,
                "path=%.400s reason=not_a_directory", path);
            (void)audit("DIRECTORY_FAILED", detail);
            return 0;
        }
    }
    if (!audit("SCAN_START", "status=initiated")) return 0;
    rc = stnlabz_module_discovery_scan(registry, loader, path, report);
    if (rc == STNLABZ_MODULE_ERR_NOT_FOUND) {
        (void)audit("SCAN_FAILED", "reason=directory_unavailable");
        return 0;
    }
    if (rc != STNLABZ_MODULE_OK) {
        (void)snprintf(detail, sizeof detail, "reason=%s",
                       stnlabz_module_result_string(rc));
        (void)audit("SCAN_FAILED", detail);
        return 0;
    }
    (void)snprintf(detail, sizeof detail,
        "examined=%zu loaded=%zu discovered=%zu rejected=%zu",
        report->directories_examined, report->modules_loaded,
        report->modules_discovered, report->modules_rejected);
    if (!audit("SCAN_COMPLETE", detail)) return 0;
    if (report->modules_rejected) {
        (void)snprintf(detail, sizeof detail,
            "module=%.63s stage=%.31s reason=%.127s rejected=%zu",
            report->rejected_module, report->rejection_stage,
            report->rejection_reason, report->modules_rejected);
        if (!audit("REJECTED_LAST", detail)) return 0;
    }
    /* Discovery never confers qualification or activation authority. */
    return 1;
}

int stnlabz_core_verify(stnlabz_module_registry_t *registry)
{
    size_t i;
    char detail[320];
    if (!registry || !stnlabz_log_is_open()) return 0;
    for (i = 0; i < registry->count; ++i) {
        stnlabz_module_record_t *r = &registry->modules[i];
        stnlabz_module_result_t result;
        if (r->state != STNLABZ_MODULE_STATE_DISCOVERED) continue;
        (void)snprintf(detail, sizeof detail,
            "id=%.63s version=%u.%u.%u abi=%u.%u",
            r->descriptor.id, r->descriptor.version_major,
            r->descriptor.version_minor, r->descriptor.version_patch,
            r->descriptor.required_core_api_major,
            r->descriptor.required_core_api_minor);
        if (!audit("VERIFY_ATTEMPT", detail)) return 0;
        result = stnlabz_module_registry_verify(registry, r->descriptor.id);
        (void)snprintf(detail, sizeof detail,
            "id=%.63s result=%s state=%s",
            r->descriptor.id, stnlabz_module_result_string(result),
            stnlabz_module_state_string(r->state));
        if (!audit(result == STNLABZ_MODULE_OK ? "VERIFY_PASS" : "VERIFY_REJECT",
                   detail)) return 0;
    }
    return 1;
}

int stnlabz_core_qualify(stnlabz_module_registry_t *registry)
{
    size_t i;
    char detail[384];
    if (!registry || !stnlabz_log_is_open()) return 0;
    for (i = 0; i < registry->count; ++i) {
        stnlabz_module_record_t *r = &registry->modules[i];
        stnlabz_module_result_t result;
        if (r->state != STNLABZ_MODULE_STATE_UNVERIFIED) continue;
        (void)snprintf(detail, sizeof detail, "id=%.63s state=UNVERIFIED min_tests=%u",
            r->descriptor.id, (unsigned)STNLABZ_MODULE_MIN_TESTS);
        if (!audit("QUALIFY_ATTEMPT", detail)) return 0;
        result = stnlabz_module_registry_qualify(registry, r->descriptor.id);
        (void)snprintf(detail, sizeof detail,
            "module=%.63s version=%u.%u.%u result=%s state=%s executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d authorized=%d",
            r->descriptor.id, r->descriptor.version_major,
            r->descriptor.version_minor, r->descriptor.version_patch,
            stnlabz_module_result_string(result),
            stnlabz_module_state_string(r->state),
            r->qualification.tests_executed, r->qualification.tests_passed,
            r->qualification.tests_failed,
            r->qualification.negative_test_executed,
            r->qualification.negative_test_passed, r->activation_authorized);
        if (!audit(result == STNLABZ_MODULE_OK ? "QUALIFICATION_GREEN" : "QUALIFICATION_FAILED",
                   detail)) return 0;
    }
    return 1;
}

void stnlabz_core_discovery_cleanup(stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader)
{
    if (!registry || !loader) return;
    /* Unload only modules known non-running or successfully stopped. */
    while (registry->count) {
        stnlabz_module_record_t *r = &registry->modules[registry->count - 1];
        if (r->state != STNLABZ_MODULE_STATE_DISCOVERED &&
            r->state != STNLABZ_MODULE_STATE_UNVERIFIED &&
            r->state != STNLABZ_MODULE_STATE_QUALIFIED &&
            r->state != STNLABZ_MODULE_STATE_STOPPED &&
            r->state != STNLABZ_MODULE_STATE_FAILED &&
            r->state != STNLABZ_MODULE_STATE_QUARANTINED) break;
        (void)stnlabz_module_registry_fail(registry, r->descriptor.id);
        (void)stnlabz_module_registry_unregister(registry, r->descriptor.id);
    }
    if (registry->count == 0) {
        stnlabz_module_loader_unload_all(loader);
        stnlabz_module_registry_destroy(registry);
    }
}
