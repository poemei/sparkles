#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include "core_hotload.h"
#include "core_activation.h"
#include "logging.h"

typedef struct {
    char id[STNLABZ_MODULE_ID_MAX];
    char active_sha[65];
    char rejected_sha[65];
    unsigned int major;
    unsigned int minor;
    unsigned int patch;
} observed_t;

struct stnlabz_hotload {
    char directory[1024];
    stnlabz_module_registry_t *registry;
    stnlabz_module_loader_t *loader;
    const stnlabz_module_host_t *host;
    observed_t *seen;
    size_t count;
    size_t capacity;
};

static observed_t *find_seen(stnlabz_hotload_t *watch, const char *id)
{
    size_t i;
    for (i = 0; i < watch->count; ++i)
        if (!strcmp(watch->seen[i].id, id)) return &watch->seen[i];
    return NULL;
}

static observed_t *ensure_seen(stnlabz_hotload_t *watch, const char *id)
{
    observed_t *entry = find_seen(watch, id);
    if (entry) return entry;
    if (watch->count == watch->capacity) {
        size_t capacity = watch->capacity ? watch->capacity * 2U : 16U;
        observed_t *next = realloc(watch->seen, capacity * sizeof *next);
        if (!next) return NULL;
        watch->seen = next;
        watch->capacity = capacity;
    }
    entry = &watch->seen[watch->count++];
    memset(entry, 0, sizeof *entry);
    (void)snprintf(entry->id, sizeof entry->id, "%s", id);
    return entry;
}

static int digest_file(const char *path, char out[65])
{
    unsigned char buffer[16384], hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX context;
    ssize_t amount;
    size_t i;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    if (!SHA256_Init(&context)) { close(fd); return 0; }
    while ((amount = read(fd, buffer, sizeof buffer)) > 0)
        SHA256_Update(&context, buffer, (size_t)amount);
    if (amount < 0 || !SHA256_Final(hash, &context)) { close(fd); return 0; }
    close(fd);
    for (i = 0; i < sizeof hash; ++i)
        (void)sprintf(out + i * 2U, "%02x", hash[i]);
    out[64] = '\0';
    return 1;
}

static int snapshot(const char *path, char *copy, size_t copy_size, char sha[65])
{
    char buffer[16384];
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX context;
    ssize_t amount;
    size_t offset, i;
    int input, output;

    if (snprintf(copy, copy_size, "%s.candidate.XXXXXX", path) >= (int)copy_size)
        return 0;
    output = mkstemp(copy);
    if (output < 0) return 0;
    input = open(path, O_RDONLY | O_CLOEXEC);
    if (input < 0) { close(output); unlink(copy); return 0; }
    if (!SHA256_Init(&context)) { close(input); close(output); unlink(copy); return 0; }

    while ((amount = read(input, buffer, sizeof buffer)) > 0) {
        SHA256_Update(&context, buffer, (size_t)amount);
        for (offset = 0; offset < (size_t)amount;) {
            ssize_t written = write(output, buffer + offset, (size_t)amount - offset);
            if (written <= 0) {
                close(input); close(output); unlink(copy); return 0;
            }
            offset += (size_t)written;
        }
    }
    close(input);
    if (amount < 0 || close(output) != 0 || !SHA256_Final(hash, &context)) {
        unlink(copy);
        return 0;
    }
    for (i = 0; i < sizeof hash; ++i)
        (void)sprintf(sha + i * 2U, "%02x", hash[i]);
    sha[64] = '\0';
    return 1;
}

static void log_result(const char *event, const char *id,
                       const char *sha, const char *reason)
{
    char detail[512];
    (void)snprintf(detail, sizeof detail,
        "module=%.63s candidate_sha256=%.64s reason=%.300s",
        id ? id : "unknown", sha ? sha : "unknown",
        reason ? reason : "unknown");
    (void)stnlabz_log_event("MODULE", event, detail);
    (void)stnlabz_log_sync();
}

static int take_loaded_module(stnlabz_module_loader_t *destination,
                              stnlabz_module_loader_t *candidate)
{
    stnlabz_loaded_module_t *next;
    size_t capacity;
    if (!destination || !candidate || candidate->count != 1U) return 0;
    if (destination->count == destination->capacity) {
        capacity = destination->capacity ? destination->capacity * 2U : 16U;
        next = realloc(destination->modules, capacity * sizeof *next);
        if (!next) return 0;
        destination->modules = next;
        destination->capacity = capacity;
    }
    destination->modules[destination->count++] = candidate->modules[0];
    candidate->count = 0U;
    return 1;
}

static void mark_rejected(stnlabz_hotload_t *watch, const char *id,
                          const char *sha, const char *reason)
{
    observed_t *entry = ensure_seen(watch, id);
    if (entry) (void)snprintf(entry->rejected_sha, sizeof entry->rejected_sha, "%s", sha);
    log_result("HOTLOAD_DENIED", id, sha, reason);
}

static int version_newer(const observed_t *old,
                         const stnlabz_module_descriptor_t *candidate)
{
    if (candidate->version_major != old->major)
        return candidate->version_major > old->major;
    if (candidate->version_minor != old->minor)
        return candidate->version_minor > old->minor;
    return candidate->version_patch > old->patch;
}

static int qualify_isolated(const char *id,
                            const stnlabz_module_descriptor_t *descriptor,
                            stnlabz_module_qualification_result_t *qualification)
{
    stnlabz_module_registry_t isolated;
    const stnlabz_module_record_t *record;
    stnlabz_module_result_t result;

    stnlabz_module_registry_init(&isolated);
    result = stnlabz_module_registry_discover(&isolated, descriptor);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_verify(&isolated, id);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_qualify(&isolated, id);
    if (result == STNLABZ_MODULE_OK) {
        record = stnlabz_module_registry_find(&isolated, id);
        if (!record) result = STNLABZ_MODULE_ERR_NOT_FOUND;
        else *qualification = record->qualification;
    }
    stnlabz_module_registry_destroy(&isolated);
    return result == STNLABZ_MODULE_OK;
}

static void admit_new(stnlabz_hotload_t *watch, const char *id,
                      const char *binary, const char *sha)
{
    char staged[1400], staged_sha[65];
    stnlabz_module_loader_t candidate;
    const stnlabz_module_descriptor_t *descriptor = NULL;
    stnlabz_module_result_t result;
    stnlabz_module_record_t *record;
    observed_t *entry;
    int admitted = 0;

    if (!snapshot(binary, staged, sizeof staged, staged_sha)) {
        mark_rejected(watch, id, sha, "candidate_snapshot_failed");
        return;
    }
    if (strcmp(sha, staged_sha)) {
        log_result("HOTLOAD_FAILED", id, sha, "candidate_changed_during_snapshot");
        unlink(staged);
        return;
    }

    stnlabz_module_loader_init(&candidate);
    if (stnlabz_module_loader_load(&candidate, id, staged, &descriptor) !=
        STNLABZ_MODULE_LOADER_OK) {
        mark_rejected(watch, id, sha, "candidate_load_failed");
        goto done;
    }

    result = stnlabz_module_registry_discover(watch->registry, descriptor);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_verify(watch->registry, id);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_qualify(watch->registry, id);
    if (result != STNLABZ_MODULE_OK) {
        mark_rejected(watch, id, sha, "qualification_failed");
        goto rollback;
    }

    result = stnlabz_core_activate_qualified(watch->registry, id, watch->host);
    if (result != STNLABZ_MODULE_OK) {
        mark_rejected(watch, id, sha, "activation_failed");
        goto rollback;
    }

    if (!take_loaded_module(watch->loader, &candidate)) {
        (void)stnlabz_core_stop(watch->registry, id);
        mark_rejected(watch, id, sha, "loader_ownership_failed");
        goto rollback;
    }

    entry = ensure_seen(watch, id);
    if (entry) {
        (void)snprintf(entry->active_sha, sizeof entry->active_sha, "%s", sha);
        entry->rejected_sha[0] = '\0';
        entry->major = descriptor->version_major;
        entry->minor = descriptor->version_minor;
        entry->patch = descriptor->version_patch;
    }
    admitted = 1;
    log_result("HOTLOAD_ACTIVE", id, sha, "qualification_and_activation_passed");

rollback:
    if (!admitted) {
        record = (stnlabz_module_record_t *)stnlabz_module_registry_find(watch->registry, id);
        if (record && record->state != STNLABZ_MODULE_STATE_ACTIVE)
            (void)stnlabz_module_registry_unregister(watch->registry, id);
    }

done:
    stnlabz_module_loader_unload_all(&candidate);
    unlink(staged);
}

static void replace_active(stnlabz_hotload_t *watch, const char *id,
                           const char *binary, const char *sha,
                           observed_t *known)
{
    char staged[1400], staged_sha[65];
    stnlabz_module_loader_t candidate;
    const stnlabz_module_descriptor_t *descriptor = NULL;
    stnlabz_module_qualification_result_t qualification = {0};
    const stnlabz_module_record_t *current;
    stnlabz_module_result_t result;
    observed_t *entry;
    int live_owned = 0;

    /*
     * ABI hot replacement rule:
     * preflight the candidate before disturbing the ACTIVE incumbent.
     */
    if (!snapshot(binary, staged, sizeof staged, staged_sha)) {
        mark_rejected(watch, id, sha, "candidate_snapshot_failed");
        return;
    }
    if (strcmp(sha, staged_sha)) {
        log_result("HOTLOAD_FAILED", id, sha, "candidate_changed_during_snapshot");
        unlink(staged);
        return;
    }

    stnlabz_module_loader_init(&candidate);
    if (stnlabz_module_loader_load(&candidate, id, staged, &descriptor) !=
        STNLABZ_MODULE_LOADER_OK) {
        mark_rejected(watch, id, sha, "candidate_load_failed");
        goto done;
    }

    if (!version_newer(known, descriptor)) {
        mark_rejected(watch, id, sha, "version_not_incremented_or_regressed");
        goto done;
    }

    if (!qualify_isolated(id, descriptor, &qualification)) {
        mark_rejected(watch, id, sha, "qualification_failed_incumbent_retained");
        goto done;
    }

    current = stnlabz_module_registry_find(watch->registry, id);
    if (!current || current->state != STNLABZ_MODULE_STATE_ACTIVE) {
        mark_rejected(watch, id, sha, "incumbent_not_active");
        goto done;
    }

    /*
     * Candidate is GREEN. Cross the ABI replacement boundary in order:
     * ACTIVE -> STOPPED -> UNREGISTERED -> old shared object unloaded.
     */
    if (!stnlabz_core_stop(watch->registry, id)) {
        mark_rejected(watch, id, sha, "incumbent_stop_failed");
        goto done;
    }
    if (stnlabz_module_registry_unregister(watch->registry, id) !=
        STNLABZ_MODULE_OK) {
        mark_rejected(watch, id, sha, "incumbent_unregister_failed");
        goto done;
    }
    if (stnlabz_module_loader_unload(watch->loader, id) !=
        STNLABZ_MODULE_LOADER_OK) {
        mark_rejected(watch, id, sha, "incumbent_unload_failed");
        goto done;
    }

    /*
     * The candidate handle was loaded only for preflight. Transfer ownership
     * to the live loader after the old revision has crossed UNREGISTERED.
     */
    if (!take_loaded_module(watch->loader, &candidate)) {
        mark_rejected(watch, id, sha, "candidate_loader_ownership_failed");
        goto done;
    }
    live_owned = 1;

    result = stnlabz_module_registry_discover(watch->registry, descriptor);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_verify(watch->registry, id);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_restore_qualification(
            watch->registry, id, &qualification);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_core_activate_qualified(
            watch->registry, id, watch->host);

    if (result != STNLABZ_MODULE_OK) {
        if (stnlabz_module_registry_find(watch->registry, id)) {
            const stnlabz_module_record_t *failed =
                stnlabz_module_registry_find(watch->registry, id);
            if (failed && failed->state == STNLABZ_MODULE_STATE_ACTIVE)
                (void)stnlabz_core_stop(watch->registry, id);
            (void)stnlabz_module_registry_unregister(watch->registry, id);
        }
        if (live_owned)
            (void)stnlabz_module_loader_unload(watch->loader, id);
        mark_rejected(watch, id, sha, "replacement_activation_failed");
        goto done;
    }

    entry = ensure_seen(watch, id);
    if (entry) {
        (void)snprintf(entry->active_sha, sizeof entry->active_sha, "%s", sha);
        entry->rejected_sha[0] = '\0';
        entry->major = descriptor->version_major;
        entry->minor = descriptor->version_minor;
        entry->patch = descriptor->version_patch;
    }
    log_result("HOTLOAD_ACTIVE", id, sha, "replacement_active");

done:
    stnlabz_module_loader_unload_all(&candidate);
    unlink(staged);
}

static void scan(stnlabz_hotload_t *watch)
{
    DIR *directory = opendir(watch->directory);
    struct dirent *entry;
    if (!directory) return;

    while ((entry = readdir(directory)) != NULL) {
        char id[STNLABZ_MODULE_ID_MAX];
        char module_dir[1200], binary[1200], sha[65];
        struct stat status;
        const stnlabz_module_record_t *record;
        observed_t *known;
        size_t length;

        if (entry->d_name[0] == '.') continue;
        length = strlen(entry->d_name);
        if (!length || length >= sizeof id) continue;
        memcpy(id, entry->d_name, length + 1U);

        if (snprintf(module_dir, sizeof module_dir, "%s/%s",
                     watch->directory, id) >= (int)sizeof module_dir)
            continue;
        if (stat(module_dir, &status) != 0 || !S_ISDIR(status.st_mode))
            continue;

        if (snprintf(binary, sizeof binary, "%s/%s.so",
                     module_dir, id) >= (int)sizeof binary)
            continue;
        if (stat(binary, &status) != 0 || !S_ISREG(status.st_mode))
            continue;
        if (!digest_file(binary, sha)) continue;

        record = stnlabz_module_registry_find(watch->registry, id);
        known = find_seen(watch, id);

        if (!record) {
            if (known && !strcmp(known->rejected_sha, sha)) continue;
            log_result("HOTLOAD_DETECTED", id, sha, "new_module");
            admit_new(watch, id, binary, sha);
            continue;
        }

        if (record->state != STNLABZ_MODULE_STATE_ACTIVE) continue;
        if (!known) {
            known = ensure_seen(watch, id);
            if (!known) continue;
            (void)snprintf(known->active_sha, sizeof known->active_sha, "%s", sha);
            known->major = record->descriptor.version_major;
            known->minor = record->descriptor.version_minor;
            known->patch = record->descriptor.version_patch;
            continue;
        }

        if (!strcmp(known->active_sha, sha) || !strcmp(known->rejected_sha, sha))
            continue;

        log_result("HOTLOAD_DETECTED", id, sha, "updated_module");
        replace_active(watch, id, binary, sha, known);
    }
    closedir(directory);
}

stnlabz_hotload_t *stnlabz_hotload_create(
    const char *directory,
    stnlabz_module_registry_t *registry,
    stnlabz_module_loader_t *loader,
    const stnlabz_module_host_t *host)
{
    stnlabz_hotload_t *watch;
    struct stat status;
    char detail[400];

    if (!directory || !registry || !loader) return NULL;
    if (stat(directory, &status) != 0) {
        if (mkdir(directory, 0755) != 0) return NULL;
    } else if (!S_ISDIR(status.st_mode)) {
        return NULL;
    }

    watch = calloc(1, sizeof *watch);
    if (!watch) return NULL;
    if (strlen(directory) >= sizeof watch->directory) {
        free(watch);
        return NULL;
    }
    (void)snprintf(watch->directory, sizeof watch->directory, "%s", directory);
    watch->registry = registry;
    watch->loader = loader;
    watch->host = host;

    (void)snprintf(detail, sizeof detail, "directory=%.310s status=ready", directory);
    if (!stnlabz_log_event("MODULE", "HOTLOAD_WATCH_READY", detail) ||
        !stnlabz_log_sync()) {
        free(watch);
        return NULL;
    }
    return watch;
}

void stnlabz_hotload_poll(stnlabz_hotload_t *watch)
{
    if (watch) scan(watch);
}

void stnlabz_hotload_destroy(stnlabz_hotload_t *watch)
{
    if (!watch) return;
    free(watch->seen);
    free(watch);
}
