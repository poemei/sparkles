#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "policy_index_internal.h"

/* [AI:GPT-6 | 2026-11-04 05:39:50 UTC] */
static int token_match(const char *line, const char *word)
{
    size_t i, n;
    if (!line || !word || !*word) return 0;
    n = strlen(word);
    for (i = 0; line[i]; ++i) {
        size_t j;
        if (i && (isalnum((unsigned char)line[i - 1]) || line[i - 1] == '_'))
            continue;
        for (j = 0; j < n && line[i + j]; ++j) {
            if (tolower((unsigned char)line[i + j]) !=
                tolower((unsigned char)word[j])) break;
        }
        if (j == n &&
            !(isalnum((unsigned char)line[i + j]) || line[i + j] == '_'))
            return 1;
    }
    return 0;
}

int rictus_policy_index_selftest(unsigned *executed, unsigned *passed,
    int *negative_executed, int *negative_passed)
{
    unsigned e = 0U, p = 0U;
    int ne = 0, np = 0;
    ++e;
    if (token_match("Digit may require restart.", "digit")) ++p;
    ++e;
    if (token_match("restart Digit", "restart")) ++p;
    ne = 2;
    np += !token_match("Digital documentation", "digit");
    np += !token_match("restartable process", "restart");
    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = ne;
    if (negative_passed) *negative_passed = np;
    return e == p && ne == np;
}

int rictus_policy_index_scan(const char *path, const char *operation,
    const char *target, rictus_policy_evidence_response_t *result)
{
    FILE *file;
    char buffer[4096];
    rictus_policy_evidence_response_t response;
    if (!path || !operation || !*operation || !target || !*target || !result)
        return 0;

    memset(&response, 0, sizeof response);
    file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) {
            *result = response; /* missing index is not an empty index */
            return 1;
        }
        return 0;
    }
    response.index_available = 1;
    while (fgets(buffer, sizeof buffer, file)) {
        char *tab1, *tab2, *tab3, *text, *sha;
        unsigned line_no;
        size_t n = strlen(buffer);
        if (!n || (n == sizeof buffer - 1U && buffer[n - 1U] != '\n')) {
            fclose(file);
            return 0;
        }
        if (buffer[0] == '#') continue;
        tab1 = strchr(buffer, '\t');
        if (!tab1) { fclose(file); return 0; }
        *tab1++ = 0;
        tab2 = strchr(tab1, '\t');
        if (!tab2 || !buffer[0] ||
            strlen(buffer) >= sizeof response.operation_document ||
            sscanf(tab1, "%u", &line_no) != 1) {
            fclose(file);
            return 0;
        }
        *tab2++ = 0;
        tab3 = strchr(tab2, '\t');
        if (!tab3 || (size_t)(tab3 - tab2) != 64U) {
            fclose(file);
            return 0;
        }
        *tab3++ = 0;
        sha = tab2;
        text = tab3;
        /* [AI:GPT-6 | 2026-11-04 06:05:00 UTC] */
        if (token_match(text, operation) && token_match(text, target)) {
            ++response.same_line_matches;
            if (!response.same_line_document[0]) {
                memcpy(response.same_line_document, buffer, strlen(buffer) + 1U);
                response.same_line_line = line_no;
                memcpy(response.same_line_sha256, sha, 65U);
            }
        }
        /* [End AI:GPT-6] */
        if (token_match(text, operation)) {
            ++response.operation_matches;
            if (!response.operation_document[0]) {
                memcpy(response.operation_document, buffer, strlen(buffer) + 1U);
                response.operation_line = line_no;
                memcpy(response.operation_sha256, sha, 65U);
            }
        }
        if (token_match(text, target)) {
            ++response.target_matches;
            if (!response.target_document[0]) {
                memcpy(response.target_document, buffer, strlen(buffer) + 1U);
                response.target_line = line_no;
                memcpy(response.target_sha256, sha, 65U);
            }
        }
    }
    if (ferror(file) || fclose(file) != 0) return 0;
    *result = response;
    return 1;
}
/* [End AI:GPT-6] */
