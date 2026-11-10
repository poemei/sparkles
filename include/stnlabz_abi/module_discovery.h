#ifndef STNLABZ_MODULE_DISCOVERY_H
#define STNLABZ_MODULE_DISCOVERY_H

#include <stddef.h>

#include "module.h"
#include "module_loader.h"
#include "module_registry.h"

#define STNLABZ_MODULE_DISCOVERY_STAGE_MAX 32
#define STNLABZ_MODULE_DISCOVERY_REASON_MAX 128

typedef struct
{
    size_t directories_examined;
    size_t modules_loaded;
    size_t modules_discovered;
    size_t modules_rejected;
    char rejected_directory[STNLABZ_MODULE_ID_MAX];
    char rejected_module[STNLABZ_MODULE_ID_MAX];
    char rejection_stage[STNLABZ_MODULE_DISCOVERY_STAGE_MAX];
    char rejection_reason[STNLABZ_MODULE_DISCOVERY_REASON_MAX];
} stnlabz_module_discovery_report_t;

/* Resolve the modules directory relative to the host executable. */
stnlabz_module_result_t
stnlabz_module_discovery_get_path(
    char *modules_path,
    size_t modules_path_size
);

/*
 * Scan the modules directory, load platform shared libraries,
 * obtain descriptors through the approved ABI, and submit valid
 * descriptors to the Core registry.  Rejection detail for the most recent
 * rejected module is returned in the discovery report so the host can place
 * the reason in its authoritative audit trail.
 */
stnlabz_module_result_t
stnlabz_module_discovery_scan(
    stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader,
    const char *modules_path,
    stnlabz_module_discovery_report_t *report
);

#endif
