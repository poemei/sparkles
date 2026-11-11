#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include "stnlabz_abi/module.h"
#include "sparkles_transport.h"
#include "sparkles_route.h"
#include "sparkles_memory.h"
#include "irc_internal.h"

#define IRC_VERSION_MAJOR 1U
#define IRC_VERSION_MINOR 1U
#define IRC_VERSION_PATCH 4U
#define IRC_OUTBOUND_MAX 32U
#define IRC_STARTUP_WAIT_SECONDS 30U

typedef struct {
    int private_message;
    char target[SPARKLES_TRANSPORT_TARGET_MAX];
    char message[SPARKLES_TRANSPORT_MESSAGE_MAX];
} irc_outbound_t;

static pthread_t worker;
static pthread_mutex_t outbound_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int stop_requested;
static atomic_int active_socket;
static atomic_int online;
static int worker_started;
static int services_registered;
static const stnlabz_module_host_t *module_host;
static sparkles_irc_config_t config;
static char channels[2][SPARKLES_IRC_CHANNEL_MAX];
static unsigned int channel_count;
static irc_outbound_t outbound[IRC_OUTBOUND_MAX];
static size_t outbound_head;
static size_t outbound_tail;
static size_t outbound_count;

static int configure_channels(void)
{
    const char *comma = strchr(config.channel, ',');
    size_t first = comma ? (size_t)(comma - config.channel) : strlen(config.channel);
    if (!first || first >= sizeof channels[0]) return 0;
    memset(channels, 0, sizeof channels);
    memcpy(channels[0], config.channel, first);
    channel_count = 1U;
    if (comma) {
        size_t second = strlen(comma + 1);
        if (!second || second >= sizeof channels[1] ||
            !strcasecmp(channels[0], comma + 1)) return 0;
        memcpy(channels[1], comma + 1, second + 1U);
        channel_count = 2U;
    }
    return 1;
}

static int channel_index(const char *name)
{
    unsigned int i;
    if (!name) return -1;
    for (i = 0U; i < channel_count; ++i)
        if (!strcasecmp(name, channels[i])) return (int)i;
    return -1;
}

static int channel_in_params(const char *params, const char *name)
{
    const char *p = params;
    size_t len = strlen(name);
    while (p && *p) {
        if ((p == params || p[-1] == ' ' || p[-1] == ':') &&
            !strncasecmp(p, name, len) &&
            (p[len] == 0 || p[len] == ' ' || p[len] == ':')) return 1;
        p = strchr(p, ' ');
        if (p) ++p;
    }
    return 0;
}

static void status(const char *message)
{
    if (message && *message) {
        fprintf(stderr, "[IRC] %s\n", message);
        fflush(stderr);
    }
}

static int sleep_milliseconds(long milliseconds)
{
    struct timespec delay;
    struct timespec remaining;

    delay.tv_sec = milliseconds / 1000L;
    delay.tv_nsec = (milliseconds % 1000L) * 1000000L;
    remaining = delay;
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno == EINTR) continue;
        return 0;
    }
    return 1;
}

static int sleep_interruptible(unsigned int seconds)
{
    unsigned int i;
    for (i = 0; i < seconds; ++i) {
        if (atomic_load(&stop_requested)) return 0;
        if (!sleep_milliseconds(1000L)) return 0;
    }
    return !atomic_load(&stop_requested);
}

static int tcp_connect(const char *server, unsigned short port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *item;
    char service[16];
    int fd = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (snprintf(service, sizeof service, "%u", (unsigned)port) < 0) return -1;
    if (getaddrinfo(server, service, &hints, &results) != 0) return -1;
    for (item = results; item; item = item->ai_next) {
        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(results);
    return fd;
}

static int ssl_write_all(SSL *ssl, const char *data, size_t length)
{
    size_t offset = 0U;
    while (offset < length) {
        int written = SSL_write(ssl, data + offset, (int)(length - offset));
        if (written <= 0) return 0;
        offset += (size_t)written;
    }
    return 1;
}

static int send_line(SSL *ssl, const char *line)
{
    char buffer[SPARKLES_IRC_LINE_MAX + 3U];
    int written;
    if (!ssl || !line) return 0;
    written = snprintf(buffer, sizeof buffer, "%s\r\n", line);
    if (written < 0 || (size_t)written >= sizeof buffer) return 0;
    return ssl_write_all(ssl, buffer, (size_t)written);
}

static int queue_outbound(int private_message,
                          const char *target,
                          const char *message)
{
    irc_outbound_t *slot;
    size_t target_length = 0U;
    size_t message_length;

    if (!message || !*message || !atomic_load(&online)) return 0;
    message_length = strlen(message);
    if (message_length >= SPARKLES_TRANSPORT_MESSAGE_MAX) return 0;
    if (!target || !*target) return 0;
    target_length = strlen(target);
    if (target_length >= SPARKLES_TRANSPORT_TARGET_MAX) return 0;

    if (pthread_mutex_lock(&outbound_lock) != 0) return 0;
    if (outbound_count >= IRC_OUTBOUND_MAX) {
        (void)pthread_mutex_unlock(&outbound_lock);
        return 0;
    }

    slot = &outbound[outbound_tail];
    memset(slot, 0, sizeof *slot);
    slot->private_message = private_message;
    memcpy(slot->target, target, target_length + 1U);
    memcpy(slot->message, message, message_length + 1U);
    outbound_tail = (outbound_tail + 1U) % IRC_OUTBOUND_MAX;
    outbound_count++;
    (void)pthread_mutex_unlock(&outbound_lock);
    return 1;
}

static int flush_outbound(SSL *ssl)
{
    for (;;) {
        irc_outbound_t item;
        char line[SPARKLES_IRC_LINE_MAX];
        int written;

        if (pthread_mutex_lock(&outbound_lock) != 0) return 0;
        if (!outbound_count) {
            (void)pthread_mutex_unlock(&outbound_lock);
            return 1;
        }
        item = outbound[outbound_head];
        memset(&outbound[outbound_head], 0, sizeof outbound[outbound_head]);
        outbound_head = (outbound_head + 1U) % IRC_OUTBOUND_MAX;
        outbound_count--;
        (void)pthread_mutex_unlock(&outbound_lock);

        written = snprintf(line, sizeof line, "PRIVMSG %s :%s",
                           item.target,
                           item.message);
        if (written < 0 || (size_t)written >= sizeof line || !send_line(ssl, line))
            return 0;
    }
}

static stnlabz_module_result_t channel_send_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *response_used,
    void *handler_context)
{
    const sparkles_transport_channel_request_t *message = request;
    (void)response;
    (void)response_size;
    (void)handler_context;
    if (response_used) *response_used = 0U;
    if (!message || request_size != sizeof *message ||
        !memchr(message->message, '\0', sizeof message->message))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    return queue_outbound(0, channels[0], message->message) ?
        STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
}

static stnlabz_module_result_t private_send_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *response_used,
    void *handler_context)
{
    const sparkles_transport_private_request_t *message = request;
    (void)response;
    (void)response_size;
    (void)handler_context;
    if (response_used) *response_used = 0U;
    if (!message || request_size != sizeof *message ||
        !memchr(message->target, '\0', sizeof message->target) ||
        !memchr(message->message, '\0', sizeof message->message))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    return queue_outbound(1, message->target, message->message) ?
        STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
}

static int register_transport_services(const stnlabz_module_host_t *host)
{
    if (!host || !host->register_service || !host->unregister_service)
        return 0;
    if (!host->register_service(SPARKLES_TRANSPORT_CHANNEL_SEND_SERVICE,
                                channel_send_service, NULL))
        return 0;
    if (!host->register_service(SPARKLES_TRANSPORT_PRIVATE_SEND_SERVICE,
                                private_send_service, NULL)) {
        (void)host->unregister_service(SPARKLES_TRANSPORT_CHANNEL_SEND_SERVICE, NULL);
        return 0;
    }
    services_registered = 1;
    module_host = host;
    return 1;
}

static void unregister_transport_services(void)
{
    if (!services_registered || !module_host) return;
    (void)module_host->unregister_service(SPARKLES_TRANSPORT_PRIVATE_SEND_SERVICE, NULL);
    (void)module_host->unregister_service(SPARKLES_TRANSPORT_CHANNEL_SEND_SERVICE, NULL);
    services_registered = 0;
    module_host = NULL;
}


static int channel_prompt(const char *message, const char **prompt_out)
{
    size_t nick_length;
    const char *p;

    if (!message || !prompt_out) return 0;
    nick_length = strlen(config.nick);
    if (strncasecmp(message, config.nick, nick_length) != 0) return 0;
    p = message + nick_length;
    if (*p && *p != ':' && *p != ',' && *p != ' ' && *p != '\t') return 0;
    while (*p == ':' || *p == ',' || *p == ' ' || *p == '\t') ++p;
    if (!*p) return 0;
    *prompt_out = p;
    return 1;
}

static void sanitize_irc_text(char *text)
{
    char *p;
    if (!text) return;
    for (p = text; *p; ++p) {
        if (*p == '\r' || *p == '\n') *p = ' ';
    }
}

static void handle_conversation_message(const char *line)
{
    char sender[SPARKLES_IRC_NICK_MAX];
    char verified_account[SPARKLES_IRC_ACCOUNT_MAX] = {0};
    char target[SPARKLES_IRC_CHANNEL_MAX];
    char message[SPARKLES_ROUTE_TEXT_MAX];
    const char *input = NULL;
    int private_message = 0;
    sparkles_route_request_t request;
    sparkles_route_reply_t response;
    size_t input_length, response_used = 0U;
    stnlabz_module_result_t result;

    if (!module_host || !module_host->invoke_service) {
        status("PRIVMSG rejected: Core routing host unavailable");
        return;
    }
    if (!sparkles_irc_parse_privmsg(line, sender, sizeof sender,
                                target, sizeof target,
                                message, sizeof message)) {
        status("PRIVMSG rejected: invalid or unsupported message format");
        return;
    }
    if (!strcasecmp(sender, config.nick)) return;
    if (!strcasecmp(target, config.nick)) {
        private_message = 1;
        input = message;
    } else if (channel_index(target) >= 0) {
        if (!channel_prompt(message, &input)) {
            /* Ambient channel observations are memory-only; never trigger
             * a conversational reply. The server-verified account is the
             * only persistent identity; nickname alone is insufficient.
             * Confine this to channels already explicitly configured for
             * Sparkles and do not capture private third-party messages. */
            sparkles_memory_exchange_request_t observation={{0}};
            sparkles_memory_exchange_reply_t ignored={{0}};
            size_t ignored_used=0U;
            if(sparkles_irc_parse_account_tag(line,verified_account,
                                              sizeof verified_account) &&
               strlen(message)<=SPARKLES_MEMORY_TEXT_MAX &&
               strlen(verified_account)<sizeof observation.account) {
                memcpy(observation.account,verified_account,
                       strlen(verified_account)+1U);
                memcpy(observation.utterance,message,strlen(message)+1U);
                observation.record=1U;
                (void)module_host->invoke_service(SPARKLES_MEMORY_EXCHANGE_SERVICE,
                       &observation,sizeof observation,&ignored,sizeof ignored,
                       &ignored_used);
            }
            return;
        }
    } else {
        status("PRIVMSG ignored: target not a configured channel or private message");
        return;
    }

    input_length = strlen(input);
    if (!input_length || input_length >= sizeof request.text ||
        strlen(sender) >= sizeof request.sender) return;
    memset(&request, 0, sizeof request);
    memset(&response, 0, sizeof response);
    memcpy(request.sender, sender, strlen(sender) + 1U);
    /* account-tag is asserted by the TLS-authenticated IRC server, not by
     * the sender nickname; missing/anonymous tags remain empty. */
    if (sparkles_irc_parse_account_tag(line,verified_account,
                                       sizeof verified_account))
        memcpy(request.account,verified_account,strlen(verified_account)+1U);
    memcpy(request.text, input, input_length + 1U);

    result = module_host->invoke_service(SPARKLES_ROUTER_DISPATCH_SERVICE,
                                         &request, sizeof request,
                                         &response, sizeof response,
                                         &response_used);
    if (result != STNLABZ_MODULE_OK) {
        char detail[128];
        (void)snprintf(detail, sizeof detail,
                       "Core route unavailable result=%d", (int)result);
        status(detail);
        return;
    }
    if (response_used != sizeof response || !response.replied ||
        !memchr(response.text, '\0', sizeof response.text) ||
        !response.text[0]) return;
    sanitize_irc_text(response.text);
    if (private_message)
        (void)queue_outbound(1, sender, response.text);
    else
        (void)queue_outbound(0, target, response.text);
}

static int send_authenticate_plain(SSL *ssl)
{
    char encoded[1024];
    size_t length;
    size_t offset = 0U;

    if (!sparkles_irc_build_plain(config.account, config.password,
                                encoded, sizeof encoded)) return 0;
    length = strlen(encoded);
    while (offset < length) {
        char line[420];
        size_t chunk = length - offset;
        int written;
        if (chunk > 400U) chunk = 400U;
        written = snprintf(line, sizeof line, "AUTHENTICATE %.*s",
                           (int)chunk, encoded + offset);
        if (written < 0 || (size_t)written >= sizeof line || !send_line(ssl, line)) {
            OPENSSL_cleanse(encoded, sizeof encoded);
            return 0;
        }
        offset += chunk;
    }
    if (length % 400U == 0U && !send_line(ssl, "AUTHENTICATE +")) {
        OPENSSL_cleanse(encoded, sizeof encoded);
        return 0;
    }
    OPENSSL_cleanse(encoded, sizeof encoded);
    return 1;
}

static int cap_is_continuation(const char *params)
{
    return params && strstr(params, " LS * ") != NULL;
}

/* Log only trusted numeric reasons: server text can contain secrets. */
static void server_failure(const char *command)
{
    const char *reason = "IRC server rejected session";
    if (!strcmp(command,"433")) reason="IRC nick already in use (433)";
    else if (!strcmp(command,"432")) reason="IRC invalid nick (432)";
    else if (!strcmp(command,"464")) reason="IRC authentication rejected (464)";
    else if (!strcmp(command,"465")) reason="IRC server ban (465)";
    else if (!strcmp(command,"471")) reason="IRC channel full (471)";
    else if (!strcmp(command,"473")) reason="IRC invite only (473)";
    else if (!strcmp(command,"474")) reason="IRC channel banned (474)";
    else if (!strcmp(command,"475")) reason="IRC channel key required (475)";
    else if (!strcmp(command,"476")) reason="IRC invalid channel (476)";
    else if (!strcmp(command,"477")) reason="IRC registered nick required (477)";
    else if (!strcmp(command,"489")) reason="IRC secure channel required (489)";
    else if (!strcmp(command,"904")) reason="SASL credentials rejected (904)";
    else if (!strcmp(command,"905")) reason="SASL message too long (905)";
    else if (!strcmp(command,"906")) reason="SASL aborted (906)";
    else if (!strcmp(command,"907")) reason="SASL already authenticated (907)";
    status(reason);
}
static int handle_line(SSL *ssl, const char *line, int *sasl_seen,
                       int *sasl_requested, int *authenticated,
                       int *registered, int *join_sent, unsigned int *joined)
{
    char command[32];
    const char *params;

    if (!sparkles_irc_line_command(line, command, sizeof command, &params)) return 1;
    if (!strcmp(command, "ERROR")) {
        status("IRC server closed session (ERROR); text redacted");
        return 0;
    }
    if (!strcmp(command, "432") || !strcmp(command, "433") ||
        !strcmp(command, "464") || !strcmp(command, "465")) {
        server_failure(command);
        return 0;
    }

    if (!strcmp(command, "PRIVMSG")) {
        handle_conversation_message(line);
        return 1;
    }

    if (!strcmp(command, "PING")) {
        char response[SPARKLES_IRC_LINE_MAX];
        int written = snprintf(response, sizeof response, "PONG %s", params);
        return written >= 0 && (size_t)written < sizeof response && send_line(ssl, response);
    }

    if (!strcmp(command, "CAP")) {
        /*
         * Match Sparkles's proven Libera.Chat negotiation behavior:
         * capability values may be bare or valued (for example sasl=...),
         * so treat substring presence in CAP payloads as authoritative.
         */
        if (strstr(params, " LS ") || strstr(params, " LS :")) {
            if (strstr(params, "sasl")) *sasl_seen = 1;
            if (!cap_is_continuation(params) && !*sasl_requested) {
                if (!*sasl_seen) {
                    status("CAP LS completed without SASL");
                    return 0;
                }
                if (!strstr(params, "account-tag") && !*sasl_requested) {
                    /*
                     * account-tag may have appeared in an earlier continuation.
                     * Request it with SASL exactly as Sparkles does; the server will
                     * NAK if unavailable.
                     */
                }
                if (!send_line(ssl, "CAP REQ :sasl account-tag")) return 0;
                *sasl_requested = 1;
            }
        } else if ((strstr(params, " ACK ") || strstr(params, " ACK :")) &&
                   strstr(params, "sasl")) {
            if (!strstr(params, "account-tag")) {
                status("CAP ACK missing account-tag");
                return 0;
            }
            if (!send_line(ssl, "AUTHENTICATE PLAIN")) return 0;
        } else if (strstr(params, " NAK ") || strstr(params, " NAK :")) {
            status("CAP request rejected");
            return 0;
        }
        return 1;
    }

    if (!strcmp(command, "AUTHENTICATE") && !strcmp(params, "+"))
        return send_authenticate_plain(ssl);

    if (!strcmp(command, "903")) {
        *authenticated = 1;
        status("SASL authentication successful");
        return send_line(ssl, "CAP END");
    }
    if (!strcmp(command, "904") || !strcmp(command, "905") ||
        !strcmp(command, "906") || !strcmp(command, "907")) {
        server_failure(command);
        return 0;
    }

    if (!strcmp(command, "001")) {
        *registered = 1;
        /* JOIN accepts a comma-separated channel list on IRC. */
        if (*authenticated && !*join_sent) {
            char join[SPARKLES_IRC_CHANNEL_MAX + 8U];
            int written = snprintf(join, sizeof join, "JOIN %s", config.channel);
            if (written < 0 || (size_t)written >= sizeof join || !send_line(ssl, join))
                return 0;
            *join_sent = 1;
            status("authenticated; channel joins requested");
        }
        return 1;
    }

    if (!strcmp(command, "366") && *join_sent) {
        unsigned int i;
        for (i = 0U; i < channel_count; ++i)
            if (channel_in_params(params, channels[i]))
                *joined |= 1U << i;
        if (*joined == ((1U << channel_count) - 1U) && !atomic_load(&online)) {
            atomic_store(&online, 1);
            status("joined all configured IRC channels; Sparkles online");
        }
        return 1;
    }

    if (!strcmp(command, "471") || !strcmp(command, "473") ||
        !strcmp(command, "474") || !strcmp(command, "475") ||
        !strcmp(command, "476") || !strcmp(command, "477") ||
        !strcmp(command, "489")) {
        server_failure(command);
        return 0;
    }

    if (!strcmp(command, "KICK") && strstr(params, config.nick)) {
        unsigned int i;
        for (i = 0U; i < channel_count; ++i) {
            if (channel_in_params(params, channels[i])) {
                char join[SPARKLES_IRC_CHANNEL_MAX + 8U];
                int written = snprintf(join, sizeof join, "JOIN %s", channels[i]);
                if (written < 0 || (size_t)written >= sizeof join) return 0;
                *joined &= ~(1U << i);
                atomic_store(&online, 0);
                if (!send_line(ssl, join)) return 0;
                status("rejoin requested after kick");
                break;
            }
        }
    }

    if (*registered && *authenticated && !*join_sent) {
        char join[SPARKLES_IRC_CHANNEL_MAX + 8U];
        int written = snprintf(join, sizeof join, "JOIN %s", config.channel);
        if (written < 0 || (size_t)written >= sizeof join || !send_line(ssl, join))
            return 0;
        *join_sent = 1;
        status("authenticated; join requested");
    }
    return 1;
}

static int run_session(void)
{
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    struct pollfd pollfd;
    int fd = -1;
    int ok = 0;
    int sasl_seen = 0;
    int sasl_requested = 0;
    int authenticated = 0;
    int registered = 0;
    int join_sent = 0;
    unsigned int joined = 0U;
    char receive_buffer[8192];
    size_t receive_used = 0U;

    atomic_store(&online, 0);
    fd = tcp_connect(config.server, config.port);
    if (fd < 0) goto done;
    atomic_store(&active_socket, fd);

    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) goto done;
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) goto done;
    ssl = SSL_new(ctx);
    if (!ssl) goto done;
    if (SSL_set_tlsext_host_name(ssl, config.server) != 1) goto done;
    if (X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), config.server, 0) != 1) goto done;
    if (SSL_set_fd(ssl, fd) != 1 || SSL_connect(ssl) != 1) goto done;
    if (SSL_get_verify_result(ssl) != X509_V_OK) goto done;

    if (!send_line(ssl, "CAP LS 302")) goto done;
    {
        char line[SPARKLES_IRC_NICK_MAX + 8U];
        int written = snprintf(line, sizeof line, "NICK %s", config.nick);
        if (written < 0 || (size_t)written >= sizeof line || !send_line(ssl, line)) goto done;
    }
    {
        char line[SPARKLES_IRC_NICK_MAX + 64U];
        int written = snprintf(line, sizeof line, "USER %s 0 * :STN-LABZ Sparkles", config.nick);
        if (written < 0 || (size_t)written >= sizeof line || !send_line(ssl, line)) goto done;
    }
    status("TLS connected; negotiating CAP and SASL");

    memset(&pollfd, 0, sizeof pollfd);
    pollfd.fd = fd;
    pollfd.events = POLLIN;

    while (!atomic_load(&stop_requested)) {
        int polled;
        int received;
        char *newline;

        if (!flush_outbound(ssl)) goto done;

        /* OpenSSL can hold decrypted bytes after the socket becomes idle. */
        if (SSL_pending(ssl) == 0) {
            pollfd.revents = 0;
            polled = poll(&pollfd, 1, 1000);
            if (polled < 0) {
                if (errno == EINTR) continue;
                goto done;
            }
            if (polled == 0) continue;
            if (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                status("IRC socket poll error or hangup");
                goto done;
            }
            if (!(pollfd.revents & POLLIN)) continue;
        }

        if (receive_used >= sizeof receive_buffer - 1U) goto done;
        received = SSL_read(ssl, receive_buffer + receive_used,
                            (int)(sizeof receive_buffer - receive_used - 1U));
        if (received <= 0) {
            char detail[96];
            int ssl_error = SSL_get_error(ssl, received);
            (void)snprintf(detail, sizeof detail, "IRC TLS read failed (SSL error=%d)", ssl_error);
            status(detail);
            goto done;
        }
        receive_used += (size_t)received;
        receive_buffer[receive_used] = '\0';

        while ((newline = strstr(receive_buffer, "\r\n")) != NULL) {
            size_t line_length = (size_t)(newline - receive_buffer);
            char line[SPARKLES_IRC_LINE_MAX];
            size_t consumed = line_length + 2U;
            if (line_length >= sizeof line) goto done;
            memcpy(line, receive_buffer, line_length);
            line[line_length] = '\0';
            memmove(receive_buffer, receive_buffer + consumed, receive_used - consumed);
            receive_used -= consumed;
            receive_buffer[receive_used] = '\0';
            if (!handle_line(ssl, line, &sasl_seen, &sasl_requested,
                             &authenticated, &registered, &join_sent, &joined))
                goto done;
        }
    }
    ok = 1;

done:
    atomic_store(&online, 0);
    atomic_store(&active_socket, -1);
    if (ssl) {
        if (ok) (void)SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ctx) SSL_CTX_free(ctx);
    if (fd >= 0) close(fd);
    return ok;
}

static void *irc_worker(void *unused)
{
    unsigned int backoff = 5U;
    (void)unused;
    while (!atomic_load(&stop_requested)) {
        if (run_session()) break;
        if (atomic_load(&stop_requested)) break;
        status("connection/session lost; retrying");
        if (!sleep_interruptible(backoff)) break;
        if (backoff < 60U) {
            backoff *= 2U;
            if (backoff > 60U) backoff = 60U;
        }
    }
    return NULL;
}

static void reset_outbound(void)
{
    if (pthread_mutex_lock(&outbound_lock) != 0) return;
    memset(outbound, 0, sizeof outbound);
    outbound_head = 0U;
    outbound_tail = 0U;
    outbound_count = 0U;
    (void)pthread_mutex_unlock(&outbound_lock);
}

static stnlabz_module_result_t irc_qualify(stnlabz_module_qualification_result_t *result)
{
    unsigned int executed = 0U;
    unsigned int passed = 0U;
    int negative_executed = 0;
    int negative_passed = 0;
    int ok;

    if (!result) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(result, 0, sizeof *result);
    ok = sparkles_irc_run_selftests(&executed, &passed,
                                  &negative_executed, &negative_passed);
    result->tests_executed = executed;
    result->tests_passed = passed;
    result->tests_failed = executed - passed;
    result->negative_test_executed = negative_executed;
    result->negative_test_passed = negative_passed;
    return ok ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_QUALIFICATION;
}

static stnlabz_module_result_t irc_start(const stnlabz_module_host_t *host)
{
    unsigned int waited;

    if (worker_started) return STNLABZ_MODULE_ERR_INVALID_STATE;
    if (!register_transport_services(host)) {
        status("ABI communication host unavailable");
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    if (!sparkles_irc_config_load(SPARKLES_IRC_ENV_PATH, &config)) {
        status("credential/config load failed");
        unregister_transport_services();
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    if (!configure_channels()) {
        status("duplicate or invalid channel list");
        sparkles_irc_config_clear(&config);
        unregister_transport_services();
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    if (OPENSSL_init_ssl(0, NULL) != 1) {
        sparkles_irc_config_clear(&config);
        unregister_transport_services();
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    reset_outbound();
    atomic_store(&stop_requested, 0);
    atomic_store(&active_socket, -1);
    atomic_store(&online, 0);

    if (pthread_create(&worker, NULL, irc_worker, NULL) != 0) {
        sparkles_irc_config_clear(&config);
        unregister_transport_services();
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    worker_started = 1;

    for (waited = 0U; waited < IRC_STARTUP_WAIT_SECONDS * 10U; ++waited) {
        if (atomic_load(&online)) {
            status("transport active");
            return STNLABZ_MODULE_OK;
        }
        if (!sleep_milliseconds(100L)) break;
    }

    status("startup did not reach authenticated channel state");
    atomic_store(&stop_requested, 1);
    {
        int fd = atomic_load(&active_socket);
        if (fd >= 0) (void)shutdown(fd, SHUT_RDWR);
    }
    (void)pthread_join(worker, NULL);
    worker_started = 0;
    reset_outbound();
    sparkles_irc_config_clear(&config);
    unregister_transport_services();
    return STNLABZ_MODULE_ERR_START_FAILED;
}

static stnlabz_module_result_t irc_stop(void)
{
    int fd;
    if (!worker_started) return STNLABZ_MODULE_ERR_INVALID_STATE;
    atomic_store(&stop_requested, 1);
    fd = atomic_load(&active_socket);
    if (fd >= 0) (void)shutdown(fd, SHUT_RDWR);
    if (pthread_join(worker, NULL) != 0) return STNLABZ_MODULE_ERR_STOP_FAILED;
    worker_started = 0;
    atomic_store(&online, 0);
    reset_outbound();
    sparkles_irc_config_clear(&config);
    unregister_transport_services();
    status("transport worker stopped");
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor = {
    "irc",
    "Sparkles IRC Transport",
    IRC_VERSION_MAJOR,
    IRC_VERSION_MINOR,
    IRC_VERSION_PATCH,
    STNLABZ_MODULE_API_MAJOR,
    STNLABZ_MODULE_API_MINOR,
    irc_qualify,
    irc_start,
    irc_stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
