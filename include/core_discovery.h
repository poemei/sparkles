#ifndef STNLABZ_CORE_DISCOVERY_H
#define STNLABZ_CORE_DISCOVERY_H
#include "stnlabz_abi/module_discovery.h"
/* Core discovery, verification, qualification; never authorizes or activates. */
int stnlabz_core_discover(stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader, const char *path,
    stnlabz_module_discovery_report_t *report);
/* Verify each DISCOVERED descriptor using unmodified ABI 1.5 lifecycle.
 * Failed candidates are isolated; audit failures fail the Core operation. */
int stnlabz_core_verify(stnlabz_module_registry_t *registry);
/* Run the established ABI qualification gate for UNVERIFIED modules. */
int stnlabz_core_qualify(stnlabz_module_registry_t *registry);
void stnlabz_core_discovery_cleanup(stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader);
#endif
