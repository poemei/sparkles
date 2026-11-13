#ifndef SPARKLES_SL_GROUP_H
#define SPARKLES_SL_GROUP_H
/* Typed Core service: available to the synchronous SL private IM dispatch
 * path only. This is an internal module contract, not a Core ABI change. */
#define SPARKLES_SL_GROUP_START_SERVICE "sparkles.sl.group.start"
typedef struct {
    char account[128];   /* Simulator-verified sender: sl:<UUID> */
    char group[64];      /* Operator-configured group alias */
    char opening[512];   /* Opening text to post after session acceptance */
} sparkles_sl_group_start_request_t;
typedef struct {
    int queued;          /* Request queued, NOT sent/delivered. */
} sparkles_sl_group_start_reply_t;
#endif
