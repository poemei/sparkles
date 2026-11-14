#ifndef SPARKLES_SL_HUD_H
#define SPARKLES_SL_HUD_H
#include <stddef.h>
#include "sparkles_sl_group.h"
#define SPARKLES_SL_HUD_REQUEST_MAX 48U
/* Parse object-origin command text. Caller MUST already verify the
 * simulator-origin object UUID against a local operator-pinned allowlist.
 * Parsing alone confers no authorization or group rights. */
typedef struct {
    char request_id[SPARKLES_SL_HUD_REQUEST_MAX];
    char landmark[128]; /* exact inventory name; not yet attached */
    sparkles_sl_notice_send_request_t notice;
} sparkles_sl_hud_request_t;
int sparkles_sl_hud_parse(const char *message,const char *operator_uuid,
                          sparkles_sl_hud_request_t *out);
#endif
