#ifndef SPARKLES_MEMORY_H
#define SPARKLES_MEMORY_H

/* In-process conversational memory for transport-verified identities.
 * Account is a trusted SL UUID or verified IRC account, never a nickname.
 * Stored observations are not authoritative facts about other people. */
#define SPARKLES_MEMORY_EXCHANGE_SERVICE "sparkles.mariadb.context.exchange"
#define SPARKLES_MEMORY_TEXT_MAX 2047
#define SPARKLES_MEMORY_RECALL_MAX 1200

typedef struct {
    char account[128];
    unsigned int record;
    unsigned int ambient; /* SL public nearby speech; never private IM. */
    char utterance[SPARKLES_MEMORY_TEXT_MAX + 1];
} sparkles_memory_exchange_request_t;

typedef struct {
    unsigned int saved;
    unsigned int recalled;
    char previous[SPARKLES_MEMORY_RECALL_MAX];
} sparkles_memory_exchange_reply_t;
#endif
