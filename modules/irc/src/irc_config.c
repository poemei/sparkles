#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>
#include "irc_internal.h"

enum {
    SEEN_SERVER = 1U << 0,
    SEEN_PORT = 1U << 1,
    SEEN_NICK = 1U << 2,
    SEEN_CHANNEL = 1U << 3,
    SEEN_ACCOUNT = 1U << 4,
    SEEN_PASSWORD = 1U << 5,
    SEEN_ALL = (1U << 6) - 1U
};

static int copy_value(char *destination, size_t size, const char *value)
{
    size_t length;
    if (!destination || !size || !value) return 0;
    length = strlen(value);
    if (!length || length >= size) return 0;
    memcpy(destination, value, length + 1U);
    return 1;
}

static int valid_identity(const char *value)
{
    const unsigned char *p = (const unsigned char *)value;
    if (!value || !*value) return 0;
    while (*p) {
        if (!(isalnum(*p) || *p == '-' || *p == '_' || *p == '[' ||
              *p == ']' || *p == '\\' || *p == '`' || *p == '^' ||
              *p == '{' || *p == '}' || *p == '|')) return 0;
        ++p;
    }
    return 1;
}

static int valid_channel(const char *value)
{
    const unsigned char *p = (const unsigned char *)value;
    unsigned int channels = 1U;
    if (!value || (value[0] != '#' && value[0] != '&') || !value[1]) return 0;
    while (*p) {
        if (*p == ',') {
            ++channels;
            if (channels > 2U || (p[1] != '#' && p[1] != '&') || !p[2]) return 0;
        } else if (*p <= 0x20 || *p == 0x7f) return 0;
        ++p;
    }
    return 1;
}

static int parse_port(const char *value, unsigned short *port)
{
    char *end = NULL;
    unsigned long parsed;
    if (!value || !*value || !port) return 0;
    parsed = strtoul(value, &end, 10);
    if (!end || *end || parsed == 0UL || parsed > 65535UL) return 0;
    *port = (unsigned short)parsed;
    return 1;
}

int sparkles_irc_config_load(const char *path, sparkles_irc_config_t *config)
{
    FILE *file;
    char line[1024];
    unsigned int seen = 0U;

    if (!path || !config) return 0;
    memset(config, 0, sizeof *config);
    file = fopen(path, "r");
    if (!file) return 0;

    while (fgets(line, sizeof line, file)) {
        char *equals;
        char *value;
        size_t length = strlen(line);
        unsigned int bit = 0U;

        if (length && line[length - 1] != '\n' && !feof(file)) {
            fclose(file);
            sparkles_irc_config_clear(config);
            return 0;
        }
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (!length || line[0] == '#') continue;
        equals = strchr(line, '=');
        if (!equals || equals == line) goto invalid;
        *equals = '\0';
        value = equals + 1;
        if (!*value) goto invalid;

        if (!strcmp(line, "SPARKLES_IRC_SERVER")) {
            bit = SEEN_SERVER;
            if (!copy_value(config->server, sizeof config->server, value)) goto invalid;
        } else if (!strcmp(line, "SPARKLES_IRC_PORT")) {
            bit = SEEN_PORT;
            if (!parse_port(value, &config->port)) goto invalid;
        } else if (!strcmp(line, "SPARKLES_IRC_NICK")) {
            bit = SEEN_NICK;
            if (!valid_identity(value) ||
                !copy_value(config->nick, sizeof config->nick, value)) goto invalid;
        } else if (!strcmp(line, "SPARKLES_IRC_CHANNEL")) {
            bit = SEEN_CHANNEL;
            if (!valid_channel(value) ||
                !copy_value(config->channel, sizeof config->channel, value)) goto invalid;
        } else if (!strcmp(line, "SPARKLES_IRC_ACCOUNT")) {
            bit = SEEN_ACCOUNT;
            if (!valid_identity(value) ||
                !copy_value(config->account, sizeof config->account, value)) goto invalid;
        } else if (!strcmp(line, "SPARKLES_IRC_PASSWORD")) {
            bit = SEEN_PASSWORD;
            if (!copy_value(config->password, sizeof config->password, value)) goto invalid;
        } else {
            goto invalid;
        }

        if (seen & bit) goto invalid;
        seen |= bit;
    }

    if (ferror(file) || seen != SEEN_ALL) goto invalid;
    fclose(file);
    return 1;

invalid:
    fclose(file);
    sparkles_irc_config_clear(config);
    return 0;
}

void sparkles_irc_config_clear(sparkles_irc_config_t *config)
{
    if (!config) return;
    OPENSSL_cleanse(config, sizeof *config);
}
