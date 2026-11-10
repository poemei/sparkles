#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "stnlabz_abi/module.h"
#include "rictus_route.h"

#define WEB_VERSION_MAJOR 1U
#define WEB_VERSION_MINOR 0U
#define WEB_VERSION_PATCH 3U
#define WEB_TRANSPORT_URL "http://127.0.0.1/chat/transport"
#define WEB_REPLY_MAX 8192U
#define WEB_ID_LENGTH 32U

typedef struct {
    char bytes[WEB_REPLY_MAX];
    size_t used;
} http_buffer_t;

static const stnlabz_module_host_t *module_host;
static pthread_t worker;
static atomic_int stop_requested;
static int worker_started;

static void sleep_interval(unsigned milliseconds)
{
    struct timespec rest = {(time_t)(milliseconds / 1000U),
                            (long)((milliseconds % 1000U) * 1000000U)};
    while (nanosleep(&rest, &rest) != 0 && errno == EINTR) {}
}

static size_t capture(void *ptr, size_t size, size_t count, void *context)
{
    http_buffer_t *buffer = (http_buffer_t *)context;
    size_t n;
    if (size && count > (size_t)-1 / size) return 0;
    n = size * count;
    if (!buffer || n > sizeof buffer->bytes - 1U - buffer->used) return 0;
    memcpy(buffer->bytes + buffer->used, ptr, n);
    buffer->used += n;
    buffer->bytes[buffer->used] = 0;
    return n;
}

static int http_post(const char *payload, http_buffer_t *buffer, long *status)
{
    CURL *curl;
    CURLcode rc;
    struct curl_slist *headers = NULL;
    if (!payload || !buffer || !status) return 0;
    memset(buffer, 0, sizeof *buffer);
    *status = 0;
    curl = curl_easy_init();
    if (!curl) return 0;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, WEB_TRANSPORT_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK;
}

/* Bounded JSON string extraction for the trusted Chat transport envelope.
 * Only its known fields are consumed; no interpreted instructions. */
static int json_string(const char *body, const char *name, char *out, size_t cap)
{
    char key[64];
    const char *p;
    size_t used = 0U;
    if (!body || !name || !out || cap < 2U) return 0;
    if (snprintf(key, sizeof key, "\"%s\"", name) >= (int)sizeof key) return 0;
    p = strstr(body, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') ++p;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\') {
            ch = (unsigned char)*p++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch == '"' || ch == '\\' || ch == '/') {}
            else return 0;
        } else if (ch < 0x20U) return 0;
        if (used + 1U >= cap) return 0;
        out[used++] = (char)ch;
    }
    if (*p != '"' || used == 0U) return 0;
    out[used] = 0;
    return 1;
}

static int decode_job(const char *body, char *id, char *message)
{
    if (!body || !id || !message ||
        !json_string(body, "id", id, WEB_ID_LENGTH + 1U) ||
        !json_string(body, "message", message, RICTUS_ROUTE_TEXT_MAX))
        return 0;
    if (strlen(id) != WEB_ID_LENGTH) return 0;
    for (size_t i = 0; i < WEB_ID_LENGTH; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') ||
              (id[i] >= 'a' && id[i] <= 'f'))) return 0;
    return 1;
}

static int encode_reply(const char *id, int success, const char *reply,
                        char *json, size_t cap)
{
    size_t used;
    int n;
    if (!id || !reply || !json || !cap) return 0;
    n = snprintf(json, cap, "{\"type\":\"reply_rictus\",\"id\":\"%s\","
                 "\"status\":\"%s\",\"message\":\"",
                 id, success ? "ok" : "error");
    if (n < 0 || (size_t)n >= cap) return 0;
    used = (size_t)n;
    for (const unsigned char *p = (const unsigned char *)reply; *p; ++p) {
        char escape = 0;
        if (*p == '"' || *p == '\\') escape = '\\';
        if (*p < 0x20U && *p != '\n' && *p != '\r' && *p != '\t') return 0;
        if (*p == '\n') { if (used + 2U >= cap) return 0; json[used++]='\\'; json[used++]='n'; continue; }
        if (*p == '\r') { if (used + 2U >= cap) return 0; json[used++]='\\'; json[used++]='r'; continue; }
        if (*p == '\t') { if (used + 2U >= cap) return 0; json[used++]='\\'; json[used++]='t'; continue; }
        if (used + (escape ? 2U : 1U) + 3U >= cap) return 0;
        if (escape) json[used++] = escape;
        json[used++] = (char)*p;
    }
    json[used++] = '"';
    json[used++] = '}';
    json[used] = 0;
    return 1;
}

static void process_job(const char *id, const char *message)
{
    rictus_route_request_t request = {0};
    rictus_route_reply_t reply = {0};
    size_t used = 0;
    stnlabz_module_result_t result;
    char payload[2048];
    http_buffer_t returned;
    long code;
    const char *answer = "Rictus Core Router unavailable.";
    int success = 0;

    if (strlen(message) >= sizeof request.text) return;
    memcpy(request.sender, "web", sizeof "web");
    memcpy(request.account, "web", sizeof "web");
    memcpy(request.text, message, strlen(message) + 1U);
    result = module_host->invoke_service(RICTUS_ROUTER_DISPATCH_SERVICE,
                                          &request, sizeof request,
                                          &reply, sizeof reply, &used);
    if (result == STNLABZ_MODULE_OK && used == sizeof reply &&
        reply.replied && memchr(reply.text, 0, sizeof reply) &&
        reply.text[0]) {
        answer = reply.text;
        success = 1;
    }
    if (!encode_reply(id, success, answer, payload, sizeof payload)) return;
    if (!http_post(payload, &returned, &code) || code != 200L)
        fprintf(stderr, "[WEB] Rictus reply delivery failed HTTP=%ld\n", code);
}

static void *run(void *context)
{
    (void)context;
    while (!atomic_load(&stop_requested)) {
        http_buffer_t response;
        long code = 0L;
        char id[WEB_ID_LENGTH + 1U];
        char message[RICTUS_ROUTE_TEXT_MAX];
        if (http_post("{\"type\":\"poll_rictus\"}", &response, &code) &&
            code == 200L && decode_job(response.bytes, id, message))
            process_job(id, message);
        else if (code != 204L && code != 200L && code != 0L)
            fprintf(stderr, "[WEB] Rictus poll HTTP=%ld\n", code);
        for (unsigned i = 0; i < 10U && !atomic_load(&stop_requested); ++i)
            sleep_interval(200U);
    }
    return NULL;
}

static stnlabz_module_result_t qualify(stnlabz_module_qualification_result_t *q)
{
    char id[WEB_ID_LENGTH + 1U], message[RICTUS_ROUTE_TEXT_MAX];
    char encoded[2048];
    const char *good = "{\"id\":\"0123456789abcdef0123456789abcdef\",\"message\":\"hello\"}";
    int checks[12];
    unsigned pass = 0U;
    if (!q) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(q, 0, sizeof *q);
    checks[0] = decode_job(good,id,message);
    checks[1] = checks[0] && strcmp(message,"hello")==0;
    checks[2] = !decode_job("{}",id,message);
    checks[3] = !decode_job("{\"id\":\"short\",\"message\":\"hello\"}",id,message);
    checks[4] = !decode_job("{\"id\":\"0123456789ABCDEF0123456789abcdef\",\"message\":\"hello\"}",id,message);
    checks[5] = !decode_job("{\"id\":\"0123456789abcdef0123456789abcdef\"}",id,message);
    checks[6] = !decode_job("{\"id\":\"0123456789abcdef0123456789abcdef\",\"message\":\"\"}",id,message);
    checks[7] = !decode_job("{\"id\":\"0123456789abcdef0123456789abcdef\",\"message\":\"\\u0000\"}",id,message);
    checks[8] = encode_reply("0123456789abcdef0123456789abcdef",1,"hello",encoded,sizeof encoded);
    checks[9] = checks[8] && strstr(encoded,"reply_rictus") != NULL;
    checks[10] = encode_reply("0123456789abcdef0123456789abcdef",0,"bad \"quote\"",encoded,sizeof encoded);
    checks[11] = checks[10] && strstr(encoded,"\\\"quote\\\"") != NULL;
    for (unsigned i = 0U; i < 12U; ++i) if (checks[i]) ++pass;
    q->tests_executed = 12U;
    q->tests_passed = pass;
    q->tests_failed = 12U - pass;
    q->negative_test_executed = 1;
    q->negative_test_passed = checks[2] && checks[3] && checks[4] &&
                              checks[5] && checks[6] && checks[7];
    return q->tests_failed == 0U && q->negative_test_passed ?
           STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_QUALIFICATION;
}

static stnlabz_module_result_t start(const stnlabz_module_host_t *host)
{
    if (!host || !host->invoke_service)
        return STNLABZ_MODULE_ERR_START_FAILED;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return STNLABZ_MODULE_ERR_START_FAILED;
    module_host = host;
    atomic_store(&stop_requested, 0);
    if (pthread_create(&worker, NULL, run, NULL) != 0) {
        module_host = NULL;
        curl_global_cleanup();
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    worker_started = 1;
    fprintf(stderr, "[WEB] Rictus outbound Chat transport active\n");
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t stop(void)
{
    atomic_store(&stop_requested, 1);
    if (worker_started) {
        (void)pthread_join(worker, NULL);
        worker_started = 0;
    }
    module_host = NULL;
    curl_global_cleanup();
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor = {
    "web", "Rictus Chat Web Transport", WEB_VERSION_MAJOR, WEB_VERSION_MINOR,
    WEB_VERSION_PATCH, STNLABZ_MODULE_API_MAJOR, STNLABZ_MODULE_API_MINOR,
    qualify, start, stop
};
const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
