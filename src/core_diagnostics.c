#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core_diagnostics.h"

typedef struct {
    char name[STNLABZ_MODULE_SERVICE_NAME_MAX];
    char owner[STNLABZ_MODULE_ID_MAX];
    stnlabz_module_result_t result;
} sparkles_diag_service_t;

typedef struct {
    int valid;
    unsigned long sequence;
    char sender[STNLABZ_MODULE_COMMAND_SENDER_MAX];
    char account[STNLABZ_MODULE_COMMAND_ACCOUNT_MAX];
    char text[SPARKLES_DIAG_TEXT_MAX];
    char command[STNLABZ_MODULE_COMMAND_NAME_MAX];
    char command_owner[STNLABZ_MODULE_ID_MAX];
    stnlabz_module_result_t command_result;
    int command_seen;
    int replied;
    sparkles_diag_service_t services[SPARKLES_DIAG_SERVICE_MAX];
    size_t service_count;
} sparkles_diag_trace_t;

static sparkles_diag_trace_t traces[SPARKLES_DIAG_TRACE_MAX];
static unsigned long sequence_counter;
static atomic_flag trace_lock = ATOMIC_FLAG_INIT;
static _Thread_local int active_trace = -1;

static void lock_trace(void)
{
    while (atomic_flag_test_and_set_explicit(&trace_lock, memory_order_acquire)) {}
}

static void unlock_trace(void)
{
    atomic_flag_clear_explicit(&trace_lock, memory_order_release);
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t length;
    if (!dst || !dst_size) return;
    dst[0] = '\0';
    if (!src) return;
    length = strlen(src);
    if (length >= dst_size) length = dst_size - 1U;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static int same_origin(const sparkles_diag_trace_t *item,
                       const char *sender, const char *account)
{
    if (!item || !item->valid) return 0;
    if (account && *account && item->account[0])
        return !strcmp(item->account, account);
    return sender && *sender && !strcmp(item->sender, sender);
}

void sparkles_diag_begin(const char *sender, const char *account, const char *text)
{
    size_t i;
    size_t slot = SPARKLES_DIAG_TRACE_MAX;
    unsigned long oldest = 0UL;

    lock_trace();
    for (i = 0U; i < SPARKLES_DIAG_TRACE_MAX; ++i) {
        if (same_origin(&traces[i], sender, account)) {
            slot = i;
            break;
        }
        if (!traces[i].valid) {
            slot = i;
            break;
        }
        if (slot == SPARKLES_DIAG_TRACE_MAX ||
            traces[i].sequence < oldest) {
            slot = i;
            oldest = traces[i].sequence;
        }
    }

    if (slot < SPARKLES_DIAG_TRACE_MAX) {
        memset(&traces[slot], 0, sizeof traces[slot]);
        traces[slot].valid = 1;
        traces[slot].sequence = ++sequence_counter;
        copy_text(traces[slot].sender, sizeof traces[slot].sender, sender);
        copy_text(traces[slot].account, sizeof traces[slot].account, account);
        copy_text(traces[slot].text, sizeof traces[slot].text, text);
        active_trace = (int)slot;
    } else {
        active_trace = -1;
    }
    unlock_trace();
}

void sparkles_diag_route(const char *command)
{
    lock_trace();
    if (active_trace >= 0 &&
        active_trace < (int)SPARKLES_DIAG_TRACE_MAX &&
        traces[active_trace].valid)
        copy_text(traces[active_trace].command,
                  sizeof traces[active_trace].command, command);
    unlock_trace();
}

void sparkles_diag_command(const char *command, const char *owner,
                         stnlabz_module_result_t result, int replied)
{
    lock_trace();
    if (active_trace >= 0 &&
        active_trace < (int)SPARKLES_DIAG_TRACE_MAX &&
        traces[active_trace].valid) {
        sparkles_diag_trace_t *item = &traces[active_trace];
        if (command && *command)
            copy_text(item->command, sizeof item->command, command);
        copy_text(item->command_owner, sizeof item->command_owner,
                  owner && *owner ? owner : "core/unknown");
        item->command_result = result;
        item->command_seen = 1;
        item->replied = replied ? 1 : 0;
    }
    unlock_trace();
}

void sparkles_diag_service(const char *service, const char *owner,
                         stnlabz_module_result_t result)
{
    lock_trace();
    if (active_trace >= 0 &&
        active_trace < (int)SPARKLES_DIAG_TRACE_MAX &&
        traces[active_trace].valid) {
        sparkles_diag_trace_t *trace = &traces[active_trace];
        if (trace->service_count < SPARKLES_DIAG_SERVICE_MAX) {
            sparkles_diag_service_t *item =
                &trace->services[trace->service_count++];
            copy_text(item->name, sizeof item->name, service);
            copy_text(item->owner, sizeof item->owner,
                      owner && *owner ? owner : "core/unknown");
            item->result = result;
        }
    }
    unlock_trace();
}

static int appendf(char *output, size_t output_size, size_t *used,
                   const char *format, ...)
{
    va_list ap;
    int written;
    if (!output || !used || *used >= output_size) return 0;
    va_start(ap, format);
    written = vsnprintf(output + *used, output_size - *used, format, ap);
    va_end(ap);
    if (written < 0 || (size_t)written >= output_size - *used) return 0;
    *used += (size_t)written;
    return 1;
}

static int source_file(const char *name)
{
    size_t length;
    if (!name) return 0;
    length = strlen(name);
    return length > 2U &&
        (!strcmp(name + length - 2U, ".c") ||
         !strcmp(name + length - 2U, ".h"));
}

static int search_tree(const char *root, const char *relative,
                       const char *needle, unsigned int depth,
                       char *path, size_t path_size,
                       unsigned long *line_out,
                       char *matched, size_t matched_size)
{
    char directory_path[768];
    DIR *directory;
    struct dirent *entry;

    if (!root || !relative || !needle || !*needle || !path || !line_out)
        return 0;
    if (snprintf(directory_path, sizeof directory_path, "%s/%s",
                 root, relative) >= (int)sizeof directory_path)
        return 0;
    directory = opendir(directory_path);
    if (!directory) return 0;

    while ((entry = readdir(directory)) != NULL) {
        char child_rel[768];
        char full_path[1024];
        struct stat status;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, ".git") || !strcmp(entry->d_name, "build"))
            continue;
        if (snprintf(child_rel, sizeof child_rel, "%s/%s",
                     relative, entry->d_name) >= (int)sizeof child_rel)
            continue;
        if (snprintf(full_path, sizeof full_path, "%s/%s",
                     root, child_rel) >= (int)sizeof full_path)
            continue;
        if (stat(full_path, &status) != 0) continue;

        if (S_ISDIR(status.st_mode)) {
            if (depth && search_tree(root, child_rel, needle, depth - 1U,
                                     path, path_size, line_out,
                                     matched, matched_size)) {
                closedir(directory);
                return 1;
            }
            continue;
        }

        if (S_ISREG(status.st_mode) && source_file(entry->d_name)) {
            FILE *file = fopen(full_path, "r");
            char line[2048];
            unsigned long number = 0UL;
            if (!file) continue;
            while (fgets(line, sizeof line, file)) {
                ++number;
                if (strstr(line, needle)) {
                    if (snprintf(path, path_size, "%s", child_rel) <
                        (int)path_size) {
                        *line_out = number;
                        if (matched && matched_size)
                            copy_text(matched, matched_size, line);
                        fclose(file);
                        closedir(directory);
                        return 1;
                    }
                }
            }
            fclose(file);
        }
    }

    closedir(directory);
    return 0;
}

static int macro_name_from_define(const char *line,
                                  char *macro, size_t macro_size)
{
    const char *p = line;
    size_t used = 0U;
    if (!line || !macro || macro_size < 2U) return 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, "#define", 7) != 0) return 0;
    p += 7;
    while (*p == ' ' || *p == '\t') ++p;
    while (*p && *p != ' ' && *p != '\t' && *p != '(' &&
           used + 1U < macro_size)
        macro[used++] = *p++;
    if (!used) return 0;
    macro[used] = '\0';
    return 1;
}

static int handler_from_registration(const char *line,
                                     char *handler, size_t handler_size)
{
    const char *p;
    size_t used = 0U;
    if (!line || !handler || handler_size < 2U) return 0;
    p = strstr(line, "register_");
    if (!p) return 0;
    p = strchr(p, ',');
    if (!p) return 0;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '&') ++p;
    while (((*p >= 'A' && *p <= 'Z') ||
            (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '_') &&
           used + 1U < handler_size)
        handler[used++] = *p++;
    if (!used) return 0;
    handler[used] = '\0';
    return 1;
}

static int handler_definition(const char *root, const char *relative,
                              const char *registration_line,
                              char *path, size_t path_size,
                              unsigned long *line_out)
{
    char handler[256];
    char needle[300];
    if (!handler_from_registration(registration_line,
                                   handler, sizeof handler))
        return 0;
    if (snprintf(needle, sizeof needle, "%s(", handler) >= (int)sizeof needle)
        return 0;
    return search_tree(root, relative, needle, 6U,
                       path, path_size, line_out, NULL, 0U);
}

static int source_for_identifier(const char *owner, const char *identifier,
                                 int is_service,
                                 char *path, size_t path_size,
                                 unsigned long *line_out)
{
    const char *root = getenv("SPARKLES_SOURCE_ROOT");
    char relative[256];
    char matched[2048];
    char macro[256];
    char registration[512];
    unsigned long registration_line = 0UL;
    unsigned long definition_line = 0UL;

    if (!root || !*root) root = "/home/stnchain/sparkles";
    if (!owner || !*owner || !identifier || !*identifier) return 0;

    if (strcmp(owner, "core/unknown") != 0) {
        if (snprintf(relative, sizeof relative, "modules/%s", owner) >=
            (int)sizeof relative)
            return 0;

        if (!is_service) {
            if (snprintf(registration, sizeof registration,
                         "register_command(\"%s\"", identifier) <
                (int)sizeof registration &&
                search_tree(root, relative, registration, 6U,
                            path, path_size, &registration_line,
                            matched, sizeof matched)) {
                if (handler_definition(root, relative, matched,
                                       path, path_size, line_out))
                    return 1;
                *line_out = registration_line;
                return 1;
            }
        } else {
            if (snprintf(registration, sizeof registration,
                         "register_service(\"%s\"", identifier) <
                (int)sizeof registration &&
                search_tree(root, relative, registration, 6U,
                            path, path_size, &registration_line,
                            matched, sizeof matched)) {
                if (handler_definition(root, relative, matched,
                                       path, path_size, line_out))
                    return 1;
                *line_out = registration_line;
                return 1;
            }

            if (search_tree(root, "include", identifier, 4U,
                            path, path_size, &definition_line,
                            matched, sizeof matched) &&
                macro_name_from_define(matched, macro, sizeof macro) &&
                search_tree(root, relative, macro, 6U,
                            path, path_size, &registration_line,
                            matched, sizeof matched)) {
                if (handler_definition(root, relative, matched,
                                       path, path_size, line_out))
                    return 1;
                *line_out = registration_line;
                return 1;
            }
        }

        if (search_tree(root, relative, identifier, 6U,
                        path, path_size, line_out, NULL, 0U))
            return 1;
        return 0;
    }

    if (search_tree(root, "src", identifier, 4U,
                    path, path_size, line_out, NULL, 0U))
        return 1;
    return search_tree(root, "include", identifier, 4U,
                       path, path_size, line_out, NULL, 0U);
}

int sparkles_diag_render(const char *sender, const char *account,
                       char *output, size_t output_size)
{
    sparkles_diag_trace_t snapshot;
    const char *code_owner = NULL;
    const char *code_identifier = NULL;
    int code_is_service = 0;
    char source_path[512];
    unsigned long source_line = 0UL;
    size_t used = 0U;
    size_t i;
    size_t slot = SPARKLES_DIAG_TRACE_MAX;
    size_t start;

    if (!output || output_size < 2U) return 0;
    output[0] = '\0';

    lock_trace();
    for (i = 0U; i < SPARKLES_DIAG_TRACE_MAX; ++i) {
        if (same_origin(&traces[i], sender, account)) {
            slot = i;
            break;
        }
    }
    if (slot < SPARKLES_DIAG_TRACE_MAX) snapshot = traces[slot];
    unlock_trace();

    if (slot == SPARKLES_DIAG_TRACE_MAX)
        return appendf(output, output_size, &used,
                       "DSD: no prior request trace for this sender.");

    if (!appendf(output, output_size, &used,
                 "DSD request=\"%.72s\" route=%.24s owner=%.24s result=%s reply=%s services=%zu\n",
                 snapshot.text[0] ? snapshot.text : "(empty)",
                 snapshot.command[0] ? snapshot.command : "(unresolved)",
                 snapshot.command_owner[0] ? snapshot.command_owner : "(unknown)",
                 snapshot.command_seen ?
                    stnlabz_module_result_string(snapshot.command_result) :
                    "NOT_REACHED",
                 snapshot.replied ? "yes" : "no",
                 snapshot.service_count))
        return 0;

    for (i = 0U; i < snapshot.service_count; ++i) {
        if (snapshot.services[i].result != STNLABZ_MODULE_OK) {
            code_owner = snapshot.services[i].owner;
            code_identifier = snapshot.services[i].name;
            code_is_service = 1;
        }
    }

    start = snapshot.service_count > 2U ? snapshot.service_count - 2U : 0U;
    for (i = start; i < snapshot.service_count; ++i) {
        if (!appendf(output, output_size, &used,
                     "svc=%.48s owner=%.20s result=%s\n",
                     snapshot.services[i].name,
                     snapshot.services[i].owner,
                     stnlabz_module_result_string(snapshot.services[i].result)))
            break;
    }

    if (!code_identifier && snapshot.command[0]) {
        code_owner = snapshot.command_owner[0] ?
            snapshot.command_owner : "core/unknown";
        code_identifier = snapshot.command;
        code_is_service = 0;
    }

    if (code_identifier &&
        source_for_identifier(code_owner, code_identifier, code_is_service,
                              source_path, sizeof source_path, &source_line))
        (void)appendf(output, output_size, &used,
                      "Code: %.96s:%lu\n", source_path, source_line);

    if (!snapshot.command_seen ||
        snapshot.command_result != STNLABZ_MODULE_OK)
        (void)appendf(output, output_size, &used,
                      "Disposition: route/controller failure.");
    else if (!snapshot.replied && snapshot.service_count == 0U)
        (void)appendf(output, output_size, &used,
                      "Disposition: completed without reply/action.");
    else
        (void)appendf(output, output_size, &used,
                      "Disposition: repair evidence captured.");

    return output[0] != '\0';
}
