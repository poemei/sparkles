#ifndef SPARKLES_SHOW_H
#define SPARKLES_SHOW_H
/* Trusted module-only Core ABI service. Claims one due show action from
 * MariaDB and reserves an at-most-once delivery attempt. */
#define SPARKLES_SHOW_CLAIM_SERVICE "sparkles.mariadb.shows.claim"
#define SPARKLES_SHOW_RESPONSE_SERVICE "sparkles.mariadb.shows.respond"
#define SPARKLES_SHOW_ATTEMPT_SERVICE "sparkles.mariadb.shows.attempt"
#define SPARKLES_SHOW_CONFIRMATION 1U
#define SPARKLES_SHOW_NOTICE 2U
typedef struct {
    unsigned int kind;
    unsigned long long attempt_id;
    char recipient_uuid[37]; /* verified show owner, or notice group UUID */
    char subject[65];        /* notice only */
    char message[448];
} sparkles_show_action_t;
typedef struct {
    unsigned int available;
    sparkles_show_action_t action;
} sparkles_show_claim_reply_t;
typedef struct {
    char account[128]; /* simulator-verified private IM account */
    unsigned int answer; /* 1: show yes; 2: show cancel */
} sparkles_show_response_request_t;
typedef struct {
    unsigned int updated;
} sparkles_show_response_reply_t;
typedef struct {
    unsigned long long attempt_id;
    unsigned int queued; /* queue accepted, not actual notice delivery */
} sparkles_show_attempt_request_t;
#endif
