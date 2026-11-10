#ifndef RICTUS_COMPLIANCE_INTERNAL_H
#define RICTUS_COMPLIANCE_INTERNAL_H

#include <stddef.h>

typedef struct {
    char operation[128];
    char module_id[64];
    char escalation_target[128];
    int within_mission;
    int complies_with_policy;
    int instruction_authorized;
    int authority_granted;
} rictus_compliance_scenario_t;

int rictus_compliance_parse(const char *text, rictus_compliance_scenario_t *out);
int rictus_compliance_selftest(unsigned *executed, unsigned *passed,
    int *negative_executed, int *negative_passed);

#endif
