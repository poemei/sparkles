#ifndef RICTUS_GOVERNANCE_INTERNAL_H
#define RICTUS_GOVERNANCE_INTERNAL_H

#include <stddef.h>

#define RICTUS_GOVERNANCE_PATH "/opt/rictus/modules/compliance/governance.conf"

typedef struct {
    int mission_known;
    int within_mission;
    int policy_known;
    int complies_with_policy;
    int instruction_known;
    int instruction_authorized;
    int authority_known;
    int authority_granted;
    char escalation_target[128];
} rictus_governance_result_t;

int rictus_control_parse(const char *text,
    char *operation, size_t operation_size,
    char *target, size_t target_size);

int rictus_governance_resolve_text(const char *text,
    const char *actor, const char *operation, const char *target,
    rictus_governance_result_t *out);

int rictus_governance_resolve_file(const char *path,
    const char *actor, const char *operation, const char *target,
    rictus_governance_result_t *out);

int rictus_governance_selftest(unsigned *executed, unsigned *passed,
    int *negative_executed, int *negative_passed);

#endif
