#ifndef SPARKLES_SL_LOGIN_H
#define SPARKLES_SL_LOGIN_H
#include <stddef.h>
#include <stdint.h>
#include "sl_wire.h"

/* Native-C login transport. Login is a credential exchange, NOT proof
 * that the simulator circuit is active or the avatar is present. */
typedef struct {
    char first[64];
    char last[64];
    char password[256];
    char start[128];
    char group_chat[37]; /* Optional operator-selected group UUID. */
} sparkles_sl_login_config_t;

typedef struct {
    sparkles_sl_session_t circuit;
    char avatar_name[128];
    char sim_ip[64];
    unsigned short sim_port;
    char seed_capability[2048];
    unsigned char group_chat[16]; /* Zero when no group session selected. */
} sparkles_sl_login_reply_t;

/* Returns 1 only on a syntactically valid successful grid login response.
 * Caller MUST establish the simulator circuit separately.
 * No credentials or raw login responses are logged. */
int sparkles_sl_login(const sparkles_sl_login_config_t *config,
                     sparkles_sl_login_reply_t *reply);
/* Load operator-owned /opt/sparkles/auth/sl/sl.conf, attempt grid login,
 * and securely clear credentials before returning. */
int sparkles_sl_login_from_file(sparkles_sl_login_reply_t *reply);
#endif
