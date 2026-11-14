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
/* Read-only, bounded marketing group selection for an authenticated HUD
 * operator. Names come from active business_group relationships; the native
 * SL notice sender independently re-resolves each group before enqueue. */
#define SPARKLES_SL_MARKETING_GROUPS_SERVICE "sparkles.mariadb.sl_groups.marketing"
#define SPARKLES_SL_MARKETING_GROUPS_MAX 16U
typedef struct {
    char account[128]; /* explicitly authenticated sl:<operator UUID> */
} sparkles_sl_marketing_groups_request_t;
typedef struct {
    unsigned int count;
    char names[SPARKLES_SL_MARKETING_GROUPS_MAX][64];
} sparkles_sl_marketing_groups_reply_t;
#endif
