#ifndef SPARKLES_ROUTE_H
#define SPARKLES_ROUTE_H

/* Transport-neutral Core ingress. Existing ABI 1.5 host layout stays unchanged. */
#define SPARKLES_ROUTER_DISPATCH_SERVICE "digit.router.dispatch"
#define SPARKLES_ROUTE_TEXT_MAX 2048
#define SPARKLES_ROUTE_REPLY_MAX 512

typedef struct {
    char sender[128];
    char account[128];
    char text[SPARKLES_ROUTE_TEXT_MAX];
} sparkles_route_request_t;

typedef struct {
    char text[SPARKLES_ROUTE_REPLY_MAX];
    int replied;
} sparkles_route_reply_t;

#endif
