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

/* Parse untrusted simulator-originated body; returns 1 for a valid personal IM.
 * A caller must only use it on packets from the authenticated simulator circuit. */
int sparkles_sl_im_decode(const unsigned char *payload, size_t length,
                         const unsigned char own_agent[16],
                         sparkles_sl_private_im_t *im);

/* Build reliable outbound private IM, in bounded unzerocoded UDP format.
 * Includes original session/conversation ID for a direct reply. */
int sparkles_sl_im_encode(const sparkles_sl_session_t *session,
                         const unsigned char recipient[16],
                         const unsigned char conversation[16],
                         uint32_t sequence, const char *message,
                         unsigned char *output, size_t capacity,
                         size_t *used);
#endif
