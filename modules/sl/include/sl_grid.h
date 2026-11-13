#ifndef SPARKLES_SL_GRID_H
#define SPARKLES_SL_GRID_H
#include <stdatomic.h>
#include "sl_im.h"

typedef struct {
    unsigned char recipient[16];
    unsigned char conversation[16];
    char text[512];
    unsigned int session_chat; /* deliver through dialog 17, not direct IM */
    unsigned int start_group; /* initiate group and wait for session confirmation */
} sparkles_sl_im_reply_t;

/* Called by the networking worker; must be nonblocking and bounded. */
typedef int (*sparkles_sl_im_ingress_fn)(
    const sparkles_sl_private_im_t *im, void *context);
/* Called by networking worker to take a response from a separate Core
 * dispatch thread; returns 0 when no reply is ready. */
typedef int (*sparkles_sl_im_dequeue_fn)(
    sparkles_sl_im_reply_t *reply, void *context);
typedef void (*sparkles_sl_im_reset_fn)(void *context);
typedef struct {
    unsigned char operator_uuid[16];
    unsigned char group[2][16];
    char name[2][64];
} sparkles_sl_group_config_t;
typedef void (*sparkles_sl_group_config_fn)(
    const sparkles_sl_group_config_t *cfg,void *context);

/* Native-C grid loop. Socket belongs ONLY to this network thread.
 * Core inference is invoked by the owning module's separate dispatcher. */
void sparkles_sl_grid_run(atomic_int *stop,
                          sparkles_sl_im_ingress_fn ingress,
                          sparkles_sl_im_dequeue_fn dequeue,
                          sparkles_sl_im_reset_fn reset,
                          sparkles_sl_group_config_fn configure,
                          void *context);
#endif
