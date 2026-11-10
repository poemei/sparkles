#ifndef RICTUS_WEB_PROTOCOL_H
#define RICTUS_WEB_PROTOCOL_H

#include <stddef.h>

#define RICTUS_WEB_MAX_REQUEST 8192U
#define RICTUS_WEB_MAX_BODY 2047U
#define RICTUS_WEB_TOKEN_MAX 128U

/*
 * Validates one complete, header-terminated HTTP/1.1 request.
 * Returns: 1 valid POST /ask, 0 malformed/unsupported, -1 too large,
 * -2 unauthenticated. 'body_offset' points past the header terminator.
 * The caller must additionally read exactly content_length bytes.
 */
int rictus_web_parse_headers(const char *bytes, size_t length,
                             const char *token, size_t *body_offset,
                             size_t *content_length);
int rictus_web_run_selftests(unsigned int *executed, unsigned int *passed,
                              int *negative_executed, int *negative_passed);
#endif
