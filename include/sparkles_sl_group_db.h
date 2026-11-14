#ifndef SPARKLES_SL_GROUP_DB_H
#define SPARKLES_SL_GROUP_DB_H
/* Read-only MariaDB resolver. Independent clubs are authorized by an
 * active sl_groups row and the selected operation's enabled flag, without
 * a business or business_groups row. Business-linked groups additionally
 * require a verified private-IM business owner and eligible active link.
 * No client-supplied UUID or name alone confers native SL permissions.
 * No Core/shared ABI alteration. */
#define SPARKLES_SL_GROUP_RESOLVE_SERVICE "sparkles.mariadb.sl_groups.resolve"
typedef struct {
    char account[128]; /* Simulator-verified sl:<avatar-uuid> */
    char group_name[64];
    unsigned int notice; /* 0=group chat, 1=on-demand notice */
} sparkles_sl_group_resolve_request_t;
typedef struct {
    char group_uuid[37]; /* lower-case canonical UUID, or empty on failure */
    unsigned int matched;
} sparkles_sl_group_resolve_reply_t;
/* Simulator-delivered group membership invitations are checked by exact
 * UUID against active chat-enabled sl_groups, whether club or business.
 * No business link, unverified display name or fee authorizes an invite. */
#define SPARKLES_SL_GROUP_INVITE_AUTH_SERVICE "sparkles.mariadb.sl_groups.invite_allowed"
typedef struct {
    char group_uuid[37];
} sparkles_sl_group_invite_auth_request_t;
typedef struct {
    unsigned int allowed;
} sparkles_sl_group_invite_auth_reply_t;
#endif
