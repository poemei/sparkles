#ifndef SPARKLES_REMINDER_H
#define SPARKLES_REMINDER_H
/* Private application service contract. Separate from the shared Core ABI.
 * An accepted, verified Second Life calendar participant can receive one
 * private appointment reminder; no group notices are generated. */
#define SPARKLES_REMINDER_CLAIM_SERVICE "sparkles.mariadb.reminders.claim"
#define SPARKLES_REMINDER_ATTEMPT_SERVICE "sparkles.mariadb.reminders.attempt"
typedef struct {
    unsigned int available;
    unsigned long long attempt_id;
    char recipient_uuid[37];
    char message[448];
} sparkles_reminder_claim_reply_t;
typedef struct {
    unsigned long long attempt_id;
    unsigned int queued;
} sparkles_reminder_attempt_request_t;
#endif
