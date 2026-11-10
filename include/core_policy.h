#ifndef STNLABZ_CORE_POLICY_H
#define STNLABZ_CORE_POLICY_H
#include "agent_root.h"
typedef enum {
    STNLABZ_POLICY_ALLOW = 0,
    STNLABZ_POLICY_DENY,
    STNLABZ_POLICY_ESCALATE
} stnlabz_policy_decision_t;
typedef struct {
    const char *actor;           /* Already verified by a trusted Core caller. */
    const char *mission;         /* Assigned mission identifier. */
    const char *operation;       /* Operation being requested. */
    const char *module_id;       /* Target module, if applicable. */
    int within_mission;
    int complies_with_policy;
    int instruction_authorized;
    int authority_granted;
    const char *escalation_target; /* Valid next-level contact supplied by operator policy. */
    const char *verification_source; /* Trusted Core caller provenance label. */
    int context_verified; /* Asserted by trusted caller, NOT independent authentication. */
} stnlabz_policy_request_t;
typedef enum {
    STNLABZ_ESCALATION_NONE = 0,
    STNLABZ_ESCALATION_PENDING_DELIVERY,
    STNLABZ_ESCALATION_UNAVAILABLE
} stnlabz_escalation_status_t;
typedef struct {
    stnlabz_policy_decision_t decision;
    const char *policy_id;
    const char *reason;
    stnlabz_escalation_status_t escalation_status; /* No external delivery is implied. */
} stnlabz_policy_result_t;
/* Fail-closed evaluation; writes a synchronized policy decision to the Core log. */
int stnlabz_core_policy_evaluate(const stnlabz_policy_request_t *request,
    stnlabz_policy_result_t *result);
#endif
