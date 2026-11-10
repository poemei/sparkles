#ifndef RICTUS_CONTROL_SERVICE_H
#define RICTUS_CONTROL_SERVICE_H

#define RICTUS_CONTROL_REQUEST_SERVICE "rictus.control.request"
#define RICTUS_CONTROL_TEXT_MAX 128
#define RICTUS_CONTROL_TARGET_MAX 64

typedef struct {
    char actor[RICTUS_CONTROL_TEXT_MAX];
    char operation[RICTUS_CONTROL_TEXT_MAX];
    char target[RICTUS_CONTROL_TARGET_MAX];
    int context_verified;
} rictus_control_request_t;

typedef struct {
    int decision;
    int executor_reached;
    int execution_suppressed;
    int assigned_mission_verified;
    int mission_known;
    int policy_known;
    int instruction_known;
    int authority_known;
    char policy_id[64];
    char reason[RICTUS_CONTROL_TEXT_MAX];
} rictus_control_response_t;

#endif
