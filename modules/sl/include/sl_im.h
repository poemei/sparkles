#ifndef SPARKLES_SL_IM_H
#define SPARKLES_SL_IM_H
#include <stddef.h>
#include <stdint.h>
#include "sl_wire.h"

/* ImprovedInstantMessage: dialog 0 private, dialog 17 group/conference
 * session messages. Control, inventory and notices are not chat. */
#define SPARKLES_SL_IM_MESSAGE_ID 254U
#define SPARKLES_SL_SESSION_SEND_DIALOG 17U
#define SPARKLES_SL_GROUP_START_DIALOG 15U
#define SPARKLES_SL_GROUP_NOTICE_DIALOG 32U
#define SPARKLES_SL_CHAT_MESSAGE_ID 139U
#define SPARKLES_SL_IM_TEXT_MAX 2048U

typedef struct {
    unsigned char sender[16];
    unsigned char conversation[16];
    char name[128];
    char text[SPARKLES_SL_IM_TEXT_MAX];
    unsigned int ambient; /* 0=private IM, 1=nearby chat, 2=session chat */
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
/* ChatFromSimulator Low 139. Accept only audible nearby avatar speech,
 * not system messages, objects, typing events, or the assistant itself. */
int sparkles_sl_chat_decode(const unsigned char *payload,size_t length,
                           const unsigned char own_agent[16],
                           sparkles_sl_private_im_t *chat);

int sparkles_sl_im_decode(const unsigned char *payload, size_t length,
                         const unsigned char own_agent[16],
                         sparkles_sl_private_im_t *im);
/* Same validator with classification for content-free safe diagnostics. */
sparkles_sl_im_validation_t sparkles_sl_im_validate(
    const unsigned char *payload,size_t length,
    const unsigned char own_agent[16], sparkles_sl_private_im_t *im);

/* Build reliable outbound private IM, in bounded unzerocoded UDP format.
 * Includes original session/conversation ID for a direct reply. */
/* Dialog 17 replies are addressed to the session, not to the speaker. */
/* An operator-selected group can be joined by sending dialog 15.
 * The simulator checks group membership; this packet grants no rights. */
int sparkles_sl_group_start_encode(const sparkles_sl_session_t *session,
                                  const unsigned char group[16],
                                  uint32_t sequence,const char *sender_name,
                                  unsigned char *output,size_t capacity,
                                  size_t *used);
/* Real group notice: IM dialog 32, recipient=group UUID, null IM ID,
 * message="subject|body", no attachment (one-NUL-byte binary bucket).
 * Enforces the 510-byte Second Life combined notice text limit. */
int sparkles_sl_notice_encode(const sparkles_sl_session_t *session,
                             const unsigned char group[16],
                             uint32_t sequence,const char *sender_name,
                             const char *subject,const char *body,
                             unsigned char *output,size_t capacity,size_t *used);
int sparkles_sl_session_encode(const sparkles_sl_session_t *session,
                             const unsigned char conversation[16],
                             uint32_t sequence,const char *sender_name,
                             const char *message,unsigned char *output,
                             size_t capacity,size_t *used);
int sparkles_sl_im_encode(const sparkles_sl_session_t *session,
                         const unsigned char recipient[16],
                         const unsigned char conversation[16],
                         uint32_t sequence, const char *sender_name,
                         const char *message,
                         unsigned char *output, size_t capacity,
                         size_t *used);
#endif
