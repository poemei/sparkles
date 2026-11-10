#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compliance_internal.h"

static int copy_text(char *out, size_t cap, const char *value)
{
    size_t n;
    if (!out || !cap || !value) return 0;
    n = strlen(value);
    if (!n || n >= cap) return 0;
    memcpy(out, value, n + 1U);
    return 1;
}

static int parse_bool(const char *value, int *out)
{
    if (!value || !out) return 0;
    if (!strcmp(value, "1") || !strcmp(value, "yes") || !strcmp(value, "true")) {
        *out = 1;
        return 1;
    }
    if (!strcmp(value, "0") || !strcmp(value, "no") || !strcmp(value, "false")) {
        *out = 0;
        return 1;
    }
    return 0;
}

int rictus_compliance_parse(const char *text, rictus_compliance_scenario_t *out)
{
    char buffer[768];
    char *save = NULL;
    char *token;
    unsigned positional = 0U;
    unsigned seen = 0U;
    rictus_compliance_scenario_t result;

    if (!text || !out || !*text || strlen(text) >= sizeof buffer) return 0;
    memset(&result, 0, sizeof result);
    memcpy(buffer, text, strlen(text) + 1U);

    token = strtok_r(buffer, " \t", &save);
    while (token) {
        char *eq = strchr(token, '=');
        if (!eq) {
            if (positional == 0U) {
                if (!copy_text(result.operation, sizeof result.operation, token))
                    return 0;
            } else if (positional == 1U) {
                if (!copy_text(result.module_id, sizeof result.module_id, token))
                    return 0;
            } else {
                return 0;
            }
            ++positional;
        } else {
            *eq++ = '\0';
            if (!strcmp(token, "mission")) {
                if ((seen & 1U) || !parse_bool(eq, &result.within_mission)) return 0;
                seen |= 1U;
            } else if (!strcmp(token, "policy")) {
                if ((seen & 2U) || !parse_bool(eq, &result.complies_with_policy)) return 0;
                seen |= 2U;
            } else if (!strcmp(token, "instruction")) {
                if ((seen & 4U) || !parse_bool(eq, &result.instruction_authorized)) return 0;
                seen |= 4U;
            } else if (!strcmp(token, "authority")) {
                if ((seen & 8U) || !parse_bool(eq, &result.authority_granted)) return 0;
                seen |= 8U;
            } else if (!strcmp(token, "escalate")) {
                if ((seen & 16U) || !copy_text(result.escalation_target,
                    sizeof result.escalation_target, eq)) return 0;
                seen |= 16U;
            } else {
                return 0;
            }
        }
        token = strtok_r(NULL, " \t", &save);
    }

    if (positional != 2U || (seen & 15U) != 15U) return 0;
    *out = result;
    return 1;
}

static void check(int ok, unsigned *executed, unsigned *passed)
{
    ++*executed;
    if (ok) ++*passed;
}

int rictus_compliance_selftest(unsigned *executed, unsigned *passed,
    int *negative_executed, int *negative_passed)
{
    rictus_compliance_scenario_t s;
    unsigned e = 0U, p = 0U;
    int ne = 0, np = 0;

    check(rictus_compliance_parse("restart digit mission=1 policy=1 instruction=1 authority=1", &s), &e, &p);
    check(!strcmp(s.operation, "restart"), &e, &p);
    check(!strcmp(s.module_id, "digit"), &e, &p);
    check(s.within_mission == 1, &e, &p);
    check(s.complies_with_policy == 1, &e, &p);
    check(s.instruction_authorized == 1, &e, &p);
    check(s.authority_granted == 1, &e, &p);
    check(rictus_compliance_parse("restart digit mission=yes policy=no instruction=yes authority=yes escalate=STN6", &s), &e, &p);
    check(s.complies_with_policy == 0, &e, &p);
    check(!strcmp(s.escalation_target, "STN6"), &e, &p);
    check(rictus_compliance_parse("stop digit mission=true policy=true instruction=true authority=false", &s), &e, &p);
    check(s.authority_granted == 0, &e, &p);

    ne = 5;
    np += !rictus_compliance_parse("", &s);
    np += !rictus_compliance_parse("restart mission=1 policy=1 instruction=1 authority=1", &s);
    np += !rictus_compliance_parse("restart digit mission=maybe policy=1 instruction=1 authority=1", &s);
    np += !rictus_compliance_parse("restart digit mission=1 mission=1 policy=1 instruction=1 authority=1", &s);
    np += !rictus_compliance_parse("restart digit mission=1 policy=1 instruction=1 authority=1 surprise=1", &s);

    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = ne;
    if (negative_passed) *negative_passed = np;
    return e == 12U && p == 12U && ne == 5 && np == 5;
}
