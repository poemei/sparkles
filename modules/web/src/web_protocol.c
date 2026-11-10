#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "web_protocol.h"

static int secret_equal(const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b), i;
    unsigned int diff = (unsigned int)(na ^ nb);
    size_t n = na > nb ? na : nb;
    if (n > RICTUS_WEB_TOKEN_MAX) return 0;
    for (i = 0; i < n; ++i) {
        unsigned char ca = i < na ? (unsigned char)a[i] : 0U;
        unsigned char cb = i < nb ? (unsigned char)b[i] : 0U;
        diff |= (unsigned int)(ca ^ cb);
    }
    return diff == 0U;
}

int rictus_web_parse_headers(const char *bytes, size_t length,
                             const char *token, size_t *body_offset,
                             size_t *content_length)
{
    size_t i, end = 0U;
    char headers[RICTUS_WEB_MAX_REQUEST + 1U];
    char *line, *save = NULL;
    int has_auth = 0, has_length = 0, has_type = 0, has_host = 0;
    int invalid = 0;
    size_t declared = 0U;

    if (!bytes || !token || !*token || !body_offset || !content_length)
        return 0;
    if (length > RICTUS_WEB_MAX_REQUEST) return -1;
    for (i = 0; i + 3U < length; ++i) {
        if (memcmp(bytes + i, "\r\n\r\n", 4U) == 0) {
            end = i + 4U;
            break;
        }
    }
    if (!end) return 0;
    memcpy(headers, bytes, end);
    headers[end - 2U] = '\0';
    line = strtok_r(headers, "\r\n", &save);
    if (!line || strcmp(line, "POST /ask HTTP/1.1") != 0) return 0;

    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *colon = strchr(line, ':');
        char *value;
        if (!colon || colon == line) return 0;
        *colon = '\0';
        value = colon + 1;
        while (*value == ' ' || *value == '\t') ++value;
        if (!strcasecmp(line, "Authorization")) {
            if (has_auth++) return 0;
            if (strncmp(value, "Bearer ", 7U) == 0)
                has_auth = secret_equal(value + 7U, token) ? 1 : -1;
            else has_auth = -1;
        } else if (!strcasecmp(line, "Content-Length")) {
            size_t n = 0U;
            const unsigned char *p = (const unsigned char *)value;
            if (has_length++ || !*p) return 0;
            while (*p) {
                if (!isdigit(*p)) return 0;
                if (n > RICTUS_WEB_MAX_BODY / 10U) return -1;
                n = n * 10U + (size_t)(*p++ - '0');
                if (n > RICTUS_WEB_MAX_BODY) return -1;
            }
            declared = n;
        } else if (!strcasecmp(line, "Content-Type")) {
            if (has_type++) return 0;
            if (strcasecmp(value, "text/plain") != 0 &&
                strcasecmp(value, "text/plain; charset=utf-8") != 0)
                invalid = 1;
        } else if (!strcasecmp(line, "Host")) {
            if (has_host++) return 0;
            {
                size_t vlen = strlen(value);
                size_t j;
                if (vlen < 8U || vlen > 255U ||
                    strcmp(value + vlen - 6U, ":18761") != 0)
                    invalid = 1;
                for (j = 0; j < vlen; ++j) {
                    unsigned char ch = (unsigned char)value[j];
                    if (!((ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '.' || ch == '-' || ch == ':'))
                        invalid = 1;
                }
            }
        } else if (!strcasecmp(line, "Transfer-Encoding") ||
                   !strcasecmp(line, "Connection") && strcasecmp(value, "close")) {
            if (!strcasecmp(line, "Transfer-Encoding")) invalid = 1;
            else invalid = 1;
        }
    }
    if (invalid || !has_length || !has_host || !declared) return 0;
    if (has_auth != 1) return -2;
    *body_offset = end;
    *content_length = declared;
    return 1;
}

int rictus_web_run_selftests(unsigned int *e, unsigned int *p,
                             int *ne, int *np)
{
    static const char *cases[] = {
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\nContent-Type: text/plain\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: localhost:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: rictus.stn-labz.com:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\n\r\nhi",
      "GET /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\n\r\nhi",
      "POST /other HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer wrong\r\nContent-Length: 2\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nContent-Length: 2\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 5000\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: -1\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: example.com\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\nAuthorization: Bearer secret\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\nContent-Type: application/json\r\n\r\nhi",
      "POST /ask HTTP/1.1\r\nHost: 127.0.0.1:18761\r\nAuthorization: Bearer secret\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nhi",
    };
    static const int expected[] = {1,1,1,0,0,-2,-2,-1,0,0,0,0,0,0,0};
    unsigned int i;
    if (!e || !p || !ne || !np) return 0;
    *e = *p = *ne = *np = 0U;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        size_t o = 0U, n = 0U;
        int actual = rictus_web_parse_headers(cases[i], strlen(cases[i]),
                                               "secret", &o, &n);
        int ok = actual == expected[i];
        ++*e;
        if (ok) ++*p;
        if (expected[i] != 1) {
            *ne = 1;
            if (!ok) *np = 0;
            else if (*np == 0) *np = 1;
        }
    }
    return *e == *p && *ne && *np;
}
