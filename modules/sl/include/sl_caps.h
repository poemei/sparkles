#ifndef SPARKLES_SL_CAPS_H
#define SPARKLES_SL_CAPS_H
#include "sl_grid.h"
#include <stdatomic.h>
#include <pthread.h>

#define SPARKLES_SL_MEMBER_QUEUE_MAX 16U

typedef struct {
    char seed_url[2048];
    unsigned char agent[16];
    atomic_int *module_stop;
    atomic_int session_stop;
    sparkles_sl_im_ingress_fn ingress;
    pthread_mutex_t *group_lock;
    unsigned char *group_confirm_uuid;
    int *group_confirm_status;
    unsigned char (*shared_joined)[16];
    unsigned int *shared_joined_count;
    pthread_mutex_t *membership_lock;
    sparkles_sl_membership_request_t *membership_queue;
    unsigned int *membership_head;
    unsigned int *membership_tail;
    unsigned int *membership_count;
    /* result: 1=member, 0=confirmed nonmember, -1=unknown */
    void (*membership_result)(const sparkles_sl_membership_request_t *,
                              int result,void *context);
    void *context;
} sparkles_sl_caps_worker_t;

/* Blocking worker; call on a dedicated thread only, never the UDP loop. */
void *sparkles_sl_caps_run(void *argument);
#endif
