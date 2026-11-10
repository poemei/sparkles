#include <ctype.h>
#include <string.h>
#include "agent_root.h"

static const char *const required_orders[STNLABZ_ROOT_ORDER_COUNT] = {
    "Remain at your assigned mission.",
    "Follow established policies and authorized instructions.",
    "Report anything outside your authority to the appropriate next level of authority."
};
static const char required_core_rule[] =
    "Sparkles Core changes require explicit STN-LABZ human developer authorization.";
static const char *const required_traits[STNLABZ_ROOT_TRAIT_COUNT] = {
    "witty", "loyal", "curious", "truthful", "respectful", "accountable", "humorous"
};

static int bounded(const char *s)
{
    size_t n;
    if (!s) return 0;
    for (n=0;n<STNLABZ_ROOT_TEXT_MAX && s[n];++n) {}
    return n > 0 && n < STNLABZ_ROOT_TEXT_MAX;
}
static int identifier(const char *s)
{
    size_t i;
    if (!bounded(s)) return 0;
    for (i=0;s[i];++i)
        if (!(islower((unsigned char)s[i]) || isdigit((unsigned char)s[i]) ||
              s[i]=='-' || s[i]=='_')) return 0;
    return 1;
}
int stnlabz_agent_root_validate(const stnlabz_agent_root_t *r)
{
    size_t i,j;
    if (!r || r->schema_major!=STNLABZ_ROOT_SCHEMA_MAJOR ||
        r->schema_minor!=STNLABZ_ROOT_SCHEMA_MINOR || r->revision==0 ||
        !identifier(r->agent_id) || !bounded(r->agent_name) ||
        !bounded(r->organization_id) || !bounded(r->lineage)) return 0;
    if (strcmp(r->organization_id,"STN-LABZ")) return 0;
    if (r->relation != STNLABZ_ROOT_RELATION_NONE) {
        if (!identifier(r->related_agent_id) ||
            !strcmp(r->related_agent_id,r->agent_id)) return 0;
        if (r->relation != STNLABZ_ROOT_RELATION_OLDER_SISTER &&
            r->relation != STNLABZ_ROOT_RELATION_YOUNGER_SISTER) return 0;
    } else if (r->related_agent_id && r->related_agent_id[0]) return 0;
    for (i=0;i<STNLABZ_ROOT_ORDER_COUNT;++i)
        if (!bounded(r->orders[i]) || strcmp(r->orders[i],required_orders[i])) return 0;
    if (!r->core_modification_rule || strcmp(r->core_modification_rule,required_core_rule)) return 0;
    for (i=0;i<STNLABZ_ROOT_TRAIT_COUNT;++i) {
        if (!bounded(r->traits[i])) return 0;
        for (j=i+1;j<STNLABZ_ROOT_TRAIT_COUNT;++j)
            if (r->traits[j] && !strcmp(r->traits[i],r->traits[j])) return 0;
        if (strcmp(r->traits[i],required_traits[i])) return 0;
    }
    return 1;
}
