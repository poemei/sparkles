#ifndef STNLABZ_AGENT_ROOT_H
#define STNLABZ_AGENT_ROOT_H

#include <stddef.h>
#include <stdint.h>

#define STNLABZ_ROOT_SCHEMA_MAJOR 1U
#define STNLABZ_ROOT_SCHEMA_MINOR 0U
#define STNLABZ_ROOT_ORDER_COUNT 3U
#define STNLABZ_ROOT_TRAIT_COUNT 7U
#define STNLABZ_ROOT_TEXT_MAX 128U

typedef enum {
    STNLABZ_ROOT_RELATION_NONE = 0,
    STNLABZ_ROOT_RELATION_OLDER_SISTER = 1,
    STNLABZ_ROOT_RELATION_YOUNGER_SISTER = 2
} stnlabz_root_relation_t;

typedef struct {
    uint32_t schema_major;
    uint32_t schema_minor;
    uint32_t revision;
    const char *agent_id;
    const char *agent_name;
    const char *organization_id;
    const char *lineage;
    stnlabz_root_relation_t relation;
    const char *related_agent_id;
    const char *orders[STNLABZ_ROOT_ORDER_COUNT];
    const char *core_modification_rule;
    const char *traits[STNLABZ_ROOT_TRAIT_COUNT];
} stnlabz_agent_root_t;

/* A read-only descriptor; no mutable Root state or permissions are exposed. */
const stnlabz_agent_root_t *stnlabz_agent_root_identity(void);
int stnlabz_agent_root_validate(const stnlabz_agent_root_t *root);
#endif
