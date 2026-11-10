#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>
#include "runtime.h"
#include "logging.h"
#include <stdio.h>

/* Core lifecycle; no module manager or hotload dependency. */
static volatile sig_atomic_t stnlabz_stop_requested = 0;

static void stnlabz_runtime_signal_handler(int signal_number)
{
    (void)signal_number;
    stnlabz_stop_requested = 1;
}
void stnlabz_runtime_request_stop(void) { stnlabz_stop_requested = 1; }

void stnlabz_runtime_init(stnlabz_runtime_t *runtime)
{
    if (!runtime) return;
    runtime->running = 0;
    runtime->operational_state = STNLABZ_OPERATIONAL_NORMAL;
    runtime->preservation_cause = STNLABZ_FAULT_SYSTEM;
    runtime->preservation_cause_set = 0;
    runtime->module_poll = NULL;
    runtime->module_poll_context = NULL;
    if (stnlabz_log_is_open()) (void)stnlabz_log_event("RUNTIME", "INITIALIZED", "state=normal");
}
int stnlabz_runtime_report_fault(stnlabz_runtime_t *runtime, stnlabz_fault_t fault)
{
    if (!runtime) return 0;
    /* Qualifying Core faults enter preservation; module failures do not imply one. */
    if (fault < STNLABZ_FAULT_AUTHORITY || fault > STNLABZ_FAULT_UNAUTHORIZED_CONDITION) return 0;
    runtime->operational_state = STNLABZ_OPERATIONAL_SAFE_MODE;
    runtime->preservation_cause = fault;
    runtime->preservation_cause_set = 1;
    runtime->operational_state = STNLABZ_OPERATIONAL_COMPANY_PRESERVATION;
    if (stnlabz_log_is_open()) {
        char detail[64];
        (void)snprintf(detail, sizeof detail, "fault=%d state=company_preservation", (int)fault);
        if (!stnlabz_log_event("RUNTIME", "CORE_FAULT", detail) || !stnlabz_log_sync()) return 0;
    }
    return 1;
}
int stnlabz_runtime_human_release(stnlabz_runtime_t *runtime, int authorized_human_release)
{
    if (!runtime || !authorized_human_release ||
        runtime->operational_state != STNLABZ_OPERATIONAL_COMPANY_PRESERVATION)
        return 0;
    runtime->operational_state = STNLABZ_OPERATIONAL_NORMAL;
    runtime->preservation_cause_set = 0;
    if (stnlabz_log_is_open() &&
        (!stnlabz_log_event("RUNTIME", "HUMAN_RELEASE", "state=normal") || !stnlabz_log_sync())) return 0;
    return 1;
}
stnlabz_operational_state_t stnlabz_runtime_operational_state(const stnlabz_runtime_t *runtime)
{
    return runtime ? runtime->operational_state : STNLABZ_OPERATIONAL_COMPANY_PRESERVATION;
}
int stnlabz_runtime_normal_operations_allowed(const stnlabz_runtime_t *runtime)
{
    return runtime && runtime->operational_state == STNLABZ_OPERATIONAL_NORMAL;
}
int stnlabz_runtime_run(stnlabz_runtime_t *runtime)
{
    struct sigaction action;
    struct sigaction previous_int, previous_term;
    struct timespec delay = { 1, 0 };
    if (!runtime || runtime->running) return 0;
    action.sa_handler = stnlabz_runtime_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, &previous_int) != 0) return 0;
    if (sigaction(SIGTERM, &action, &previous_term) != 0) {
        (void)sigaction(SIGINT, &previous_int, NULL);
        return 0;
    }
    stnlabz_stop_requested = 0;
    runtime->running = 1;
    if (stnlabz_log_is_open() &&
        (!stnlabz_log_event("RUNTIME", "START", "status=running") || !stnlabz_log_sync())) {
        runtime->running = 0;
        (void)sigaction(SIGINT, &previous_int, NULL);
        (void)sigaction(SIGTERM, &previous_term, NULL);
        return 0;
    }
    while (!stnlabz_stop_requested) {
        struct timespec remaining = delay;
        while (nanosleep(&remaining, &remaining) != 0) {
            if (errno == EINTR) {
                if (stnlabz_stop_requested) break;
                continue;
            }
            (void)stnlabz_runtime_report_fault(runtime, STNLABZ_FAULT_SYSTEM);
            runtime->running = 0;
            (void)sigaction(SIGINT, &previous_int, NULL);
            (void)sigaction(SIGTERM, &previous_term, NULL);
            return 0;
        }
        if (stnlabz_runtime_normal_operations_allowed(runtime) && runtime->module_poll)
            runtime->module_poll(runtime->module_poll_context);
    }
    runtime->running = 0;
    (void)sigaction(SIGINT, &previous_int, NULL);
    (void)sigaction(SIGTERM, &previous_term, NULL);
    if (stnlabz_log_is_open() &&
        (!stnlabz_log_event("RUNTIME", "STOP", "reason=stop_requested") || !stnlabz_log_sync()))
        return 0;
    return 1;
}
