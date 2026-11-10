#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "agent_root.h"
#include "sparkles.h"
#include "logging.h"
#include "core_activation.h"
#include "core_inventory.h"
#include "core_discovery.h"
#include "core_hotload.h"
#include "module_host.h"
#include "stnlabz_abi/module_registry.h"
#include "stnlabz_abi/module_loader.h"
#include "runtime.h"
#include "sparkles_route.h"

#define SPARKLES_MODULE_DIRECTORY STNLABZ_INSTALL_PATH "/modules"

/* Terminal input is a Core ingress, not a capability or module.
 * It invokes the same existing transport-neutral router used by IRC/web. */
static int sparkles_terminal_dispatch(void *context, const char *message,
                                   char *output, size_t capacity)
{
    const stnlabz_module_host_t *host = context;
    sparkles_route_request_t request = {0};
    sparkles_route_reply_t reply = {0};
    size_t length, used = 0;
    stnlabz_module_result_t result;
    if (!host || !host->invoke_service || !message || !output || !capacity)
        return 0;
    length = strlen(message);
    if (!length || length >= sizeof request.text)
        return 0;
    memcpy(request.sender, "terminal", sizeof "terminal");
    memcpy(request.text, message, length + 1U);
    result = host->invoke_service(SPARKLES_ROUTER_DISPATCH_SERVICE,
                                  &request, sizeof request,
                                  &reply, sizeof reply, &used);
    if (result != STNLABZ_MODULE_OK || used != sizeof reply ||
        !reply.replied || !memchr(reply.text, 0, sizeof reply.text) ||
        !reply.text[0])
        return 0;
    length = strlen(reply.text);
    if (length >= capacity) return 0;
    memcpy(output, reply.text, length + 1U);
    return 1;
}

/* Core identity and operations never depend on any one capability module. */
int stnlabz_initialize(void)
{
    stnlabz_runtime_t runtime;
    stnlabz_module_registry_t modules;
    stnlabz_module_loader_t loader;
    stnlabz_hotload_t *watch = NULL;
    const stnlabz_agent_root_t *root;
    int runtime_ok;
    const stnlabz_module_host_t *module_host = sparkles_module_host();

    stnlabz_module_registry_init(&modules);
    stnlabz_module_loader_init(&loader);

    if (!stnlabz_log_open()) {
        fprintf(stderr, "[CORE] Cannot open mandatory log: %s\n", STNLABZ_LOG_PATH);
        return 1;
    }
    if (!stnlabz_log_event("CORE", "START", "status=initiated")) goto log_failure;

    root = stnlabz_agent_root_identity();
    if (!stnlabz_agent_root_validate(root)) {
        (void)stnlabz_log_event("CORE", "ROOT_REJECTED", "startup_denied=true");
        fputs("[CORE] Agent Root validation failed. Startup denied.\n", stderr);
        stnlabz_log_close();
        return 1;
    }
    if (!stnlabz_log_event("CORE", "ROOT_VERIFIED", "agent=sparkles revision=1"))
        goto log_failure;

    stnlabz_runtime_init(&runtime);

    watch = stnlabz_hotload_create(SPARKLES_MODULE_DIRECTORY, &modules, &loader,
                                   module_host);
    if (!watch) {
        (void)stnlabz_log_event("MODULE", "HOTLOAD_FAILED",
                                "reason=watch_initialization_failed");
        goto log_failure;
    }

    /*
     * Evaluate modules already present before entering the runtime loop.
     * New arrivals are evaluated by the same poll path while Sparkles runs.
     */
    stnlabz_hotload_poll(watch);

    if (!stnlabz_core_inventory_log(&modules)) goto log_failure;
    if (!stnlabz_log_event("CORE", "MODULE_MANAGER_READY",
                           "status=watch_active")) goto log_failure;

    runtime.module_poll = (void (*)(void *))stnlabz_hotload_poll;
    runtime.module_poll_context = watch;
    runtime.terminal_dispatch = sparkles_terminal_dispatch;
    runtime.terminal_context = (void *)module_host;


    if (!stnlabz_log_event("CORE", "RUNNING",
        "verification=complete module_watch=active automatic_admission=qualified_only") ||
        !stnlabz_log_sync()) goto log_failure;

    if (isatty(STDIN_FILENO)) {
        puts("Ms. Sparkles is standing by.");
        puts("CTRL + C to quit.");
        fflush(stdout);
    }

    runtime_ok = stnlabz_runtime_run(&runtime);
    if (!runtime_ok)
        (void)stnlabz_log_event("CORE", "RUNTIME_FAILED", NULL);

    if (!stnlabz_core_stop_all(&modules)) goto stop_failure;
    if (!stnlabz_core_inventory_log(&modules)) goto log_failure;
    stnlabz_hotload_destroy(watch);
    watch = NULL;
    stnlabz_core_discovery_cleanup(&modules, &loader);
    (void)stnlabz_log_event("MODULE", "MANAGER_SHUTDOWN", "id=unknown result=0");

    if (!runtime_ok) {
        stnlabz_log_close();
        fputs("[CORE] Runtime failed.\n", stderr);
        return 1;
    }

    if (!stnlabz_log_event("CORE", "STOPPED", "reason=signal") ||
        !stnlabz_log_sync()) goto log_failure;
    stnlabz_log_close();
    printf("[CORE] Stopped. Shutdown recorded in %s\n", STNLABZ_LOG_PATH);
    return 0;

stop_failure:
    if (watch) stnlabz_hotload_destroy(watch);
    stnlabz_log_close();
    fputs("[CORE] Module stop failed; shared library handles retained.\n", stderr);
    return 1;

log_failure:
    if (watch) {
        (void)stnlabz_core_stop_all(&modules);
        stnlabz_hotload_destroy(watch);
    }
    stnlabz_core_discovery_cleanup(&modules, &loader);
    (void)stnlabz_log_event("MODULE", "MANAGER_SHUTDOWN", "id=unknown result=0");
    stnlabz_log_close();
    fputs("[CORE] Mandatory logging or module-watch initialization failed.\n", stderr);
    return 1;
}
