#define _POSIX_C_SOURCE 200809L
#include "sl_config.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int add_value(sparkles_sl_login_config_t *cfg,
                     const char *key, const char *value, unsigned int *found)
{
    struct field { const char *name; char *dst; size_t capacity; };
    struct field fields[] = {
        {"first", cfg->first, sizeof cfg->first},
        {"last", cfg->last, sizeof cfg->last},
        {"password", cfg->password, sizeof cfg->password},
        {"start", cfg->start, sizeof cfg->start},
        {"group_chat", cfg->group_chat, sizeof cfg->group_chat}
    };
    size_t i, length = strlen(value);
    if (!length || strchr(value, '\r') || strchr(value, '\n')) return 0;
    for (i = 0U; i < sizeof fields / sizeof fields[0]; ++i) {
        if (strcmp(key, fields[i].name)) continue;
        if ((*found & (1U << i)) || length >= fields[i].capacity)
            return 0;
        memcpy(fields[i].dst, value, length + 1U);
        *found |= 1U << i;
        return 1;
    }
    return 0; /* Unknown field is an error, not silently ignored. */
}
int sparkles_sl_config_load(const char *path, sparkles_sl_login_config_t *out)
{
    sparkles_sl_login_config_t candidate = {0};
    FILE *stream = NULL;
    struct stat st;
    unsigned int found = 0U;
    char line[512];
    int fd, success = 0;
    if (!path || !out || path[0] != '/') return 0;
    memset(out, 0, sizeof *out);
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() || (st.st_mode & 077U) ||
        st.st_size < 1 || st.st_size > 4096) goto done;
    stream = fdopen(fd, "r");
    if (!stream) goto done;
    fd = -1;
    while (fgets(line, sizeof line, stream)) {
        char *key = line, *equals, *value, *end;
        size_t n = strlen(line);
        if (!n || (line[n - 1U] != '\n' && !feof(stream))) goto done;
        if (n && line[n - 1U] == '\n') line[--n] = '\0';
        if (n && line[n - 1U] == '\r') line[--n] = '\0';
        while (*key == ' ' || *key == '\t') ++key;
        if (!*key || *key == '#') continue;
        equals = strchr(key, '=');
        if (!equals) goto done;
        *equals = '\0';
        end = equals;
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        value = equals + 1;
        while (*value == ' ' || *value == '\t') ++value;
        end = value + strlen(value);
        while (end > value && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        if (!add_value(&candidate, key, value, &found)) goto done;
    }
    if (ferror(stream) || (found & 0x0fU) != 0x0fU) goto done;
    if (candidate.group_chat[0]) {
        unsigned char uuid[16];
        if (!sparkles_sl_uuid_parse(candidate.group_chat,uuid)) goto done;
    }
    *out = candidate;
    success = 1;
done:
    OPENSSL_cleanse(&candidate, sizeof candidate);
    OPENSSL_cleanse(line, sizeof line);
    if (stream) fclose(stream);
    if (fd >= 0) close(fd);
    return success;
}
void sparkles_sl_config_clear(sparkles_sl_login_config_t *config)
{
    if (config) OPENSSL_cleanse(config, sizeof *config);
}
