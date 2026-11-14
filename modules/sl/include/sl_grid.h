#ifndef SPARKLES_SL_GRID_H
#define SPARKLES_SL_GRID_H
#include <stdatomic.h>
#include "sl_im.h"

typedef struct {
    unsigned char recipient[16];
    unsigned char conversation[16];
    char text[512];
    unsigned int session_chat; /* deliver through dialog 17, not direct IM */
    unsigned int nearby; /* deliver in public channel-zero ChatFromViewer */
    unsigned int start_group; /* initiate group and wait for session confirmation */
    unsigned int notice; /* official group notice, dialog 32, not session chat */
    unsigned int membership_check; /* GroupMemberData capability query */
    unsigned int invite_group; /* confirmed nonmember: InviteGroupRequest */
    unsigned char visitor[16]; /* target for check/invitation */
    char subject[65];
} sparkles_sl_im_reply_t;

/* Called by the networking worker; must be nonblocking and bounded. */
typedef int (*sparkles_sl_im_ingress_fn)(
    const sparkles_sl_private_im_t *im, void *context);
/* Called by networking worker to take a response from a separate Core
 * dispatch thread; returns 0 when no reply is ready. */
typedef int (*sparkles_sl_im_dequeue_fn)(
    sparkles_sl_im_reply_t *reply, void *context);
typedef void (*sparkles_sl_im_reset_fn)(void *context);
typedef void (*sparkles_sl_presence_fn)(int online,void *context);
/* Only exact simulator-delivered group UUIDs may be authorized by the
 * existing registry. The socket worker never derives policy from names. */
typedef int (*sparkles_sl_group_invite_authorize_fn)(
    const unsigned char group[16],void *context);

typedef struct sparkles_sl_membership_request_tag {
    unsigned char group[16];
    unsigned char visitor[16];
} sparkles_sl_membership_request_t;
typedef void (*sparkles_sl_membership_result_fn)(
    const sparkles_sl_membership_request_t *request,
    int result,void *context);

/* Native-C grid loop. Socket belongs ONLY to this network thread.
 * Core inference is invoked by the owning module's separate dispatcher. */
void sparkles_sl_grid_run(atomic_int *stop,
                          sparkles_sl_im_ingress_fn ingress,
                          sparkles_sl_im_dequeue_fn dequeue,
                          sparkles_sl_im_reset_fn reset,
                          sparkles_sl_presence_fn presence,
                          sparkles_sl_group_invite_authorize_fn authorize_invite,
                          sparkles_sl_membership_result_fn membership_result,
                          void *context);
#endif
