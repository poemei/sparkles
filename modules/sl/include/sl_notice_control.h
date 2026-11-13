#ifndef SPARKLES_SL_NOTICE_CONTROL_H
#define SPARKLES_SL_NOTICE_CONTROL_H
#include <stdatomic.h>
#include "stnlabz_abi/module.h"
#include "sparkles_sl_group.h"
/* Listener binds loopback ONLY. TLS and public access are the operator's
 * reverse-proxy responsibility. All requests require the 0600 notice.conf. */
typedef int (*sparkles_notice_publish_fn)(
    const sparkles_sl_notice_send_request_t *notice,void *ctx);
typedef struct {
    atomic_int *stop;
    const stnlabz_module_host_t *host;
    sparkles_notice_publish_fn publish;
    void *publish_ctx;
} sparkles_notice_control_t;
int sparkles_notice_control_run(sparkles_notice_control_t *control);
#endif
