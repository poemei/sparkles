#ifndef RICTUS_POLICY_INDEX_INTERNAL_H
#define RICTUS_POLICY_INDEX_INTERNAL_H

#include "rictus_policy_evidence.h"

/* [AI:GPT-6 | 2026-11-04 05:39:50 UTC] */
int rictus_policy_index_scan(const char *path, const char *operation,
    const char *target, rictus_policy_evidence_response_t *result);
int rictus_policy_index_selftest(unsigned *executed, unsigned *passed,
    int *negative_executed, int *negative_passed);
/* [End AI:GPT-6] */

#endif
