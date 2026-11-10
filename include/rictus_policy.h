#ifndef RICTUS_POLICY_SERVICE_H
#define RICTUS_POLICY_SERVICE_H

#include <stddef.h>

#define STNLABZ_POLICY_EVALUATE_SERVICE "stnlabz.policy.evaluate"
#define RICTUS_POLICY_TEXT_MAX 128
#define RICTUS_POLICY_MODULE_MAX 64

typedef struct {
    char actor[RICTUS_POLICY_TEXT_MAX];
    char mission[RICTUS_POLICY_TEXT_MAX];
    char operation[RICTUS_POLICY_TEXT_MAX];
    char module_id[RICTUS_POLICY_MODULE_MAX];
    char escalation_target[RICTUS_POLICY_TEXT_MAX];
    char verification_source[RICTUS_POLICY_TEXT_MAX];
    int mission_known;
    int within_mission;
    int policy_known;
    int complies_with_policy;
    int instruction_known;
    int instruction_authorized;
    int authority_known;
    int authority_granted;
    int context_verified;
} rictus_policy_evaluate_request_t;

typedef struct {
    int decision;
    int escalation_status;
    char policy_id[RICTUS_POLICY_MODULE_MAX];
    char reason[RICTUS_POLICY_TEXT_MAX];
} rictus_policy_evaluate_response_t;

#endif
