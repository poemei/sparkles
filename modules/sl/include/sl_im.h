#ifndef SPARKLES_SL_IM_H
#define SPARKLES_SL_IM_H
#include <stddef.h>
#include <stdint.h>
#include "sl_wire.h"

/* One-to-one ImprovedInstantMessage only, Low message 254, Dialog 0.
 * Group, conference, object, inventory and control dialogs are excluded. */
#define SPARKLES_SL_IM_MESSAGE_ID 254U
#define SPARKLES_SL_IM_TEXT_MAX 2048U

typedef struct {
    unsigned char sender[16];
    unsigned char conversation[16];
    char name[128];
    char text[SPARKLES_SL_IM_TEXT_MAX];
} sparkles_sl_private_im_t;

typedef enum {
    SPARKLES_SL_IM_VALID=0,
    SPARKLES_SL_IM_TOO_SHORT,
    SPARKLES_SL_IM_WRONG_RECIPIENT,
    SPARKLES_SL_IM_UNSUPPORTED_DIALOG,
    SPARKLES_SL_IM_INVALID_SENDER,
    SPARKLES_SL_IM_INVALID_NAME,
    SPARKLES_SL_IM_INVALID_TEXT,
    SPARKLES_SL_IM_INVALID_BUCKET,
    SPARKLES_SL_IM_TRAILING_DATA
} sparkles_sl_im_validation_t;
const char *sparkles_sl_im_validation_name(sparkles_sl_im_validation_t result);

/* Parse untrusted simulator-originated body; returns 1 for a valid personal IM.
 * A caller must only use it on packets from the authenticated simulator circuit. */
int sparkles_sl_im_decode(const unsigned char *payload, size_t length,
                         const unsigned char own_agent[16],
                         sparkles_sl_private_im_t *im);
/* Same validator with classification for content-free safe diagnostics. */
sparkles_sl_im_validation_t sparkles_sl_im_validate(
    const unsigned char *payload,size_t length,
    const unsigned char own_agent[16], sparkles_sl_private_im_t *im);

/* Build reliable outbound private IM, in bounded unzerocoded UDP format.
 * Includes original session/conversation ID for a direct reply. */
int sparkles_sl_im_encode(const sparkles_sl_session_t *session,
                         const unsigned char recipient[16],
                         const unsigned char conversation[16],
                         uint32_t sequence, const char *sender_name,
                         const char *message,
                         unsigned char *output, size_t capacity,
                         size_t *used);
#endif
