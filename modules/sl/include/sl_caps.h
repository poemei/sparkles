#ifndef SPARKLES_SL_CAPS_H
#define SPARKLES_SL_CAPS_H
#include "sl_grid.h"
#include <stdatomic.h>

typedef struct {
    char seed_url[2048];
    unsigned char agent[16];
    atomic_int *module_stop;
    atomic_int session_stop;
    sparkles_sl_im_ingress_fn ingress;
    void *context;
} sparkles_sl_caps_worker_t;

/* Blocking worker; call on a dedicated thread only, never the UDP loop. */
void *sparkles_sl_caps_run(void *argument);
#endif
