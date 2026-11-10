#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "governance_internal.h"

#define GOVERNANCE_LINE_MAX 512U

typedef struct {
    int mission_known;
    int within_mission;
    int policy_fact_known;
    int policy_fact_allow;
    int directive_known;
    int directive_allow;
    int instruction_known;
    int instruction_authorized;
    int authority_known;
    int authority_granted;
    int escalation_known;
    char escalation_target[128];
} governance_accum_t;

static int copy_text(char *out, size_t capacity, const char *value)
{
    size_t length;
    if (!out || !capacity || !value) return 0;
    length = strlen(value);
    if (!length || length >= capacity) return 0;
    memcpy(out, value, length + 1U);
    return 1;
}

static int valid_token(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!text || !*text) return 0;
    while (*p) {
        if (*p < 0x21U || *p == 0x7fU || *p == '|') return 0;
        ++p;
    }
    return 1;
}

static int decision_value(const char *text, int *value)
{
    if (!text || !value) return 0;
    if (!strcmp(text, "allow")) {
        *value = 1;
        return 1;
    }
    if (!strcmp(text, "deny")) {
        *value = 0;
        return 1;
    }
    return 0;
}

static void lowercase(char *text)
{
    unsigned char *p = (unsigned char *)text;
    while (p && *p) {
        *p = (unsigned char)tolower(*p);
        ++p;
    }
}

int rictus_control_parse(const char *text,
    char *operation, size_t operation_size,
    char *target, size_t target_size)
{
    char buffer[256];
    char *save = NULL;
    char *first;
    char *second;
    char *extra;

    if (!text || !operation || !target ||
        !operation_size || !target_size ||
        !*text || strlen(text) >= sizeof buffer)
        return 0;

    memcpy(buffer, text, strlen(text) + 1U);
    first = strtok_r(buffer, " \t", &save);
    second = strtok_r(NULL, " \t", &save);
    extra = strtok_r(NULL, " \t", &save);

    if (!first || !second || extra ||
        !valid_token(first) || !valid_token(second) ||
        !copy_text(operation, operation_size, first) ||
        !copy_text(target, target_size, second))
        return 0;

    lowercase(operation);
    lowercase(target);
    return 1;
}

static int split_fields(char *line, char **fields, size_t maximum, size_t *count)
{
    char *cursor;
    size_t used = 0U;

    if (!line || !fields || !maximum || !count) return 0;
    cursor = line;

    while (used < maximum) {
        char *separator;
        fields[used++] = cursor;
        separator = strchr(cursor, '|');
        if (!separator) {
            *count = used;
            return 1;
        }
        if (used == maximum) return 0;
        *separator = '\0';
        cursor = separator + 1;
    }

    return 0;
}

static int set_fact(int *known, int *value, int next)
{
    if (!known || !value || *known) return 0;
    *known = 1;
    *value = next;
    return 1;
}

static int apply_record(char *line,
    const char *actor, const char *operation, const char *target,
    governance_accum_t *state)
{
    char *fields[5];
    size_t count = 0U;
    size_t length;
    int allow;

    if (!line || !actor || !operation || !target || !state) return 0;

    length = strlen(line);
    while (length && (line[length - 1U] == '\n' ||
                      line[length - 1U] == '\r' ||
                      line[length - 1U] == ' ' ||
                      line[length - 1U] == '\t'))
        line[--length] = '\0';

    while (*line == ' ' || *line == '\t') ++line;
    if (!*line || *line == '#') return 1;

    if (!split_fields(line, fields, 5U, &count)) return 0;
    for (size_t i = 0U; i < count; ++i)
        if (!valid_token(fields[i])) return 0;

    if (!strcmp(fields[0], "escalation")) {
        if (count != 2U || state->escalation_known ||
            !copy_text(state->escalation_target,
                sizeof state->escalation_target, fields[1]))
            return 0;
        state->escalation_known = 1;
        return 1;
    }

    if (!strcmp(fields[0], "mission")) {
        if (count != 4U || strcmp(fields[1], operation) ||
            strcmp(fields[2], target))
            return count == 4U;
        if (!decision_value(fields[3], &allow)) return 0;
        return set_fact(&state->mission_known,
            &state->within_mission, allow);
    }

    if (!strcmp(fields[0], "policy")) {
        if (count != 4U || strcmp(fields[1], operation) ||
            strcmp(fields[2], target))
            return count == 4U;
        if (!decision_value(fields[3], &allow)) return 0;
        return set_fact(&state->policy_fact_known,
            &state->policy_fact_allow, allow);
    }

    if (!strcmp(fields[0], "directive")) {
        if (count != 4U || strcmp(fields[1], operation) ||
            strcmp(fields[2], target))
            return count == 4U;
        if (!decision_value(fields[3], &allow)) return 0;
        return set_fact(&state->directive_known,
            &state->directive_allow, allow);
    }

    if (!strcmp(fields[0], "instruction")) {
        if (count != 5U || strcmp(fields[1], actor) ||
            strcmp(fields[2], operation) || strcmp(fields[3], target))
            return count == 5U;
        if (!decision_value(fields[4], &allow)) return 0;
        return set_fact(&state->instruction_known,
            &state->instruction_authorized, allow);
    }

    if (!strcmp(fields[0], "authority")) {
        if (count != 5U || strcmp(fields[1], "rictus") ||
            strcmp(fields[2], operation) || strcmp(fields[3], target))
            return count == 5U;
        if (!decision_value(fields[4], &allow)) return 0;
        return set_fact(&state->authority_known,
            &state->authority_granted, allow);
    }

    return 0;
}

static void finish_result(const governance_accum_t *state,
    rictus_governance_result_t *out)
{
    memset(out, 0, sizeof *out);
    out->mission_known = state->mission_known;
    out->within_mission = state->within_mission;

    if ((state->policy_fact_known && !state->policy_fact_allow) ||
        (state->directive_known && !state->directive_allow)) {
        out->policy_known = 1;
        out->complies_with_policy = 0;
    } else if (state->policy_fact_known && state->policy_fact_allow &&
               state->directive_known && state->directive_allow) {
        out->policy_known = 1;
        out->complies_with_policy = 1;
    }

    out->instruction_known = state->instruction_known;
    out->instruction_authorized = state->instruction_authorized;
    out->authority_known = state->authority_known;
    out->authority_granted = state->authority_granted;

    if (state->escalation_known)
        (void)copy_text(out->escalation_target,
            sizeof out->escalation_target, state->escalation_target);
}

int rictus_governance_resolve_text(const char *text,
    const char *actor, const char *operation, const char *target,
    rictus_governance_result_t *out)
{
    char buffer[4096];
    char *save = NULL;
    char *line;
    governance_accum_t state;

    if (!text || !actor || !operation || !target || !out ||
        !valid_token(actor) || !valid_token(operation) || !valid_token(target) ||
        strlen(text) >= sizeof buffer)
        return 0;

    memset(&state, 0, sizeof state);
    memcpy(buffer, text, strlen(text) + 1U);

    line = strtok_r(buffer, "\n", &save);
    while (line) {
        if (!apply_record(line, actor, operation, target, &state))
            return 0;
        line = strtok_r(NULL, "\n", &save);
    }

    finish_result(&state, out);
    return 1;
}

int rictus_governance_resolve_file(const char *path,
    const char *actor, const char *operation, const char *target,
    rictus_governance_result_t *out)
{
    FILE *file;
    char line[GOVERNANCE_LINE_MAX];
    governance_accum_t state;

    if (!path || !actor || !operation || !target || !out ||
        !valid_token(actor) || !valid_token(operation) || !valid_token(target))
        return 0;

    memset(&state, 0, sizeof state);
    file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) {
            finish_result(&state, out);
            return 1;
        }
        return 0;
    }

    while (fgets(line, sizeof line, file)) {
        size_t length = strlen(line);
        if (length == sizeof line - 1U && line[length - 1U] != '\n') {
            fclose(file);
            return 0;
        }
        if (!apply_record(line, actor, operation, target, &state)) {
            fclose(file);
            return 0;
        }
    }

    if (ferror(file) || fclose(file) != 0) return 0;
    finish_result(&state, out);
    return 1;
}

static void check(int ok, unsigned *executed, unsigned *passed)
{
    ++*executed;
    if (ok) ++*passed;
}

int rictus_governance_selftest(unsigned *executed, unsigned *passed,
    int *negative_executed, int *negative_passed)
{
    static const char allow_record[] =
        "escalation|STN6\n"
        "mission|restart|digit|allow\n"
        "policy|restart|digit|allow\n"
        "directive|restart|digit|allow\n"
        "instruction|PoeMei|restart|digit|allow\n"
        "authority|rictus|restart|digit|allow\n";
    static const char deny_policy[] =
        "mission|restart|digit|allow\n"
        "policy|restart|digit|deny\n";
    rictus_governance_result_t result;
    char operation[128];
    char target[64];
    unsigned e = 0U, p = 0U;
    int ne = 0, np = 0;

    check(rictus_control_parse("restart Digit", operation, sizeof operation,
        target, sizeof target), &e, &p);
    check(!strcmp(operation, "restart"), &e, &p);
    check(!strcmp(target, "digit"), &e, &p);
    check(rictus_governance_resolve_text(allow_record, "PoeMei",
        operation, target, &result), &e, &p);
    check(result.mission_known && result.within_mission, &e, &p);
    check(result.policy_known && result.complies_with_policy, &e, &p);
    check(result.instruction_known && result.instruction_authorized, &e, &p);
    check(result.authority_known && result.authority_granted, &e, &p);
    check(!strcmp(result.escalation_target, "STN6"), &e, &p);
    check(rictus_governance_resolve_text(deny_policy, "PoeMei",
        "restart", "digit", &result), &e, &p);
    check(result.policy_known && !result.complies_with_policy, &e, &p);
    check(!result.instruction_known && !result.authority_known, &e, &p);
    check(rictus_governance_resolve_text("escalation|STN6\n", "PoeMei",
        "restart", "digit", &result), &e, &p);
    check(!result.mission_known && !result.policy_known &&
        !result.instruction_known && !result.authority_known, &e, &p);
    check(!strcmp(result.escalation_target, "STN6"), &e, &p);

    ne = 7;
    np += !rictus_control_parse("", operation, sizeof operation,
        target, sizeof target);
    np += !rictus_control_parse("restart", operation, sizeof operation,
        target, sizeof target);
    np += !rictus_control_parse("restart digit extra", operation, sizeof operation,
        target, sizeof target);
    np += !rictus_governance_resolve_text("bogus|x\n", "PoeMei",
        "restart", "digit", &result);
    np += !rictus_governance_resolve_text(
        "policy|restart|digit|maybe\n", "PoeMei",
        "restart", "digit", &result);
    np += !rictus_governance_resolve_text(
        "mission|restart|digit|allow\nmission|restart|digit|deny\n",
        "PoeMei", "restart", "digit", &result);
    np += !rictus_governance_resolve_text(
        "escalation|STN6\nescalation|other\n",
        "PoeMei", "restart", "digit", &result);

    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = ne;
    if (negative_passed) *negative_passed = np;
    return e == 15U && p == 15U && ne == 7 && np == 7;
}
