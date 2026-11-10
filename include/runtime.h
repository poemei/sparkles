#ifndef STNLABZ_RUNTIME_H
#define STNLABZ_RUNTIME_H

typedef enum {
    STNLABZ_OPERATIONAL_NORMAL = 0,
    STNLABZ_OPERATIONAL_SAFE_MODE = 1,
    STNLABZ_OPERATIONAL_COMPANY_PRESERVATION = 2
} stnlabz_operational_state_t;

typedef enum {
    STNLABZ_FAULT_AUTHORITY = 0,
    STNLABZ_FAULT_INTEGRITY = 1,
    STNLABZ_FAULT_SYSTEM = 2,
    STNLABZ_FAULT_MISSION = 3,
    STNLABZ_FAULT_TRUTHFULNESS = 4,
    STNLABZ_FAULT_UNAUTHORIZED_CONDITION = 5
} stnlabz_fault_t;

typedef struct {
    int running;
    stnlabz_operational_state_t operational_state;
    stnlabz_fault_t preservation_cause;
    int preservation_cause_set;
    void (*module_poll)(void *context);
    void *module_poll_context;
} stnlabz_runtime_t;

/* Core runtime functions do not require any installed modules. */
void stnlabz_runtime_init(stnlabz_runtime_t *runtime);
int stnlabz_runtime_run(stnlabz_runtime_t *runtime);
void stnlabz_runtime_request_stop(void);
int stnlabz_runtime_report_fault(stnlabz_runtime_t *runtime, stnlabz_fault_t fault);
int stnlabz_runtime_human_release(stnlabz_runtime_t *runtime, int authorized_human_release);
stnlabz_operational_state_t stnlabz_runtime_operational_state(const stnlabz_runtime_t *runtime);
int stnlabz_runtime_normal_operations_allowed(const stnlabz_runtime_t *runtime);

#endif
