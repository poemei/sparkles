#ifndef SPARKLES_SL_GROUP_DB_H
#define SPARKLES_SL_GROUP_DB_H
/* Read-only MariaDB resolver. The verified private-IM principal must own
 * a linked, active business whose active group permits chat. No client-
 * supplied UUID is accepted. Database schema: businesses, business_groups,
 * sl_groups. No Core/shared ABI alteration. */
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
 * group UUID against the operator-maintained active group/business registry.
 * This is not a grant based on an unverified invite display name. */
#define SPARKLES_SL_GROUP_INVITE_AUTH_SERVICE "sparkles.mariadb.sl_groups.invite_allowed"
typedef struct {
    char group_uuid[37];
} sparkles_sl_group_invite_auth_request_t;
typedef struct {
    unsigned int allowed;
} sparkles_sl_group_invite_auth_reply_t;
#endif
