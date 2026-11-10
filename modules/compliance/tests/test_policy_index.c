#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "policy_index_internal.h"

/* [AI:GPT-6 | 2026-11-04 05:40:58 UTC] */
static int test_read_only_index(void)
{
    char path[] = "/tmp/rictus-policy-evidence-XXXXXX";
    int fd = mkstemp(path);
    FILE *stream;
    rictus_policy_evidence_response_t result;
    int ok = 0;
    if (fd < 0) return 0;
    stream = fdopen(fd, "w");
    if (!stream) { close(fd); unlink(path); return 0; }
    {
        int wrote = fputs("20260729.0\t12\t"
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\t"
                         "Digital documentation does not restart systems.\n"
                         "20260805.0.R1\t7\t"
                         "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\t"
                         "Digit may require an authorized restart.\n", stream);
        int closed = fclose(stream);
        if (wrote < 0 || closed != 0) { unlink(path); return 0; }
    }
    if (rictus_policy_index_scan(path, "restart", "digit", &result) &&
        result.index_available == 1 &&
        result.operation_matches == 2 &&
        result.target_matches == 1 &&
        !strcmp(result.target_document, "20260805.0.R1") &&
        !strcmp(result.target_sha256,
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") &&
        result.target_line == 7U &&
        /* [AI:GPT-6 | 2026-11-04 06:05:00 UTC] */
        result.same_line_matches == 1U &&
        !strcmp(result.same_line_document, "20260805.0.R1") &&
        result.same_line_line == 7U &&
        !strcmp(result.same_line_sha256,
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
        /* [End AI:GPT-6] */
        ) ok = 1;
    unlink(path);
    return ok;
}

/* [AI:GPT-6 | 2026-11-04 06:05:00 UTC] */
static int test_separate_lines_do_not_correlate(void)
{
    char path[] = "/tmp/rictus-policy-separate-XXXXXX";
    int fd = mkstemp(path);
    FILE *stream;
    rictus_policy_evidence_response_t result;
    int ok = 0, wrote, closed;
    if (fd < 0) return 0;
    stream = fdopen(fd, "w");
    if (!stream) { close(fd); unlink(path); return 0; }
    wrote = fputs(
        "20260805.0\t4\t"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\t"
        "Restart request evidence.\n"
        "20260805.0\t5\t"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\t"
        "Digit is named separately.\n", stream);
    closed = fclose(stream);
    if (wrote >= 0 && closed == 0 &&
        rictus_policy_index_scan(path, "restart", "digit", &result) &&
        result.operation_matches == 1U &&
        result.target_matches == 1U &&
        result.same_line_matches == 0U &&
        result.same_line_document[0] == 0 &&
        result.same_line_line == 0U)
        ok = 1;
    unlink(path);
    return ok;
}
/* [End AI:GPT-6] */

static int test_absent_index(void)
{
    rictus_policy_evidence_response_t result;
    return rictus_policy_index_scan("/tmp/rictus-policy-index-not-installed-xyz",
            "restart", "digit", &result) &&
        !result.index_available && !result.operation_matches &&
        !result.target_matches && !result.same_line_matches;
}

int main(void)
{
    int passed = 0;
    passed += test_read_only_index();
    passed += test_absent_index();
    /* [AI:GPT-6 | 2026-11-04 06:05:00 UTC] */
    passed += test_separate_lines_do_not_correlate();
    /* [End AI:GPT-6] */
    printf("Policy evidence qualification executed=3 passed=%d\n", passed);
    return passed == 3 ? 0 : 1;
}
/* [End AI:GPT-6] */
