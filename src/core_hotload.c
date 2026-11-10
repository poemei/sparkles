#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/evp.h>
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

/* SHA-256 checksums are part of Core module admission and snapshot integrity. */
static void sha256_hex(const unsigned char hash[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < 32U; ++i) {
        out[i * 2U] = hex[hash[i] >> 4];
        out[i * 2U + 1U] = hex[hash[i] & 0x0fU];
    }
    out[64] = '\0';
}

static int digest_file(const char *path, char out[65])
{
    unsigned char buffer[16384], hash[32];
    EVP_MD_CTX *context = NULL;
    unsigned int hash_length = 0U;
    ssize_t amount;
    int fd, ok = 0;

    if (!path || !out) return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1)
        goto done;

    for (;;) {
        amount = read(fd, buffer, sizeof buffer);
        if (amount < 0 && errno == EINTR) continue;
        if (amount < 0) goto done;
        if (amount == 0) break;
        if (EVP_DigestUpdate(context, buffer, (size_t)amount) != 1)
            goto done;
    }
    if (EVP_DigestFinal_ex(context, hash, &hash_length) != 1 ||
        hash_length != sizeof hash)
        goto done;
    if (close(fd) != 0) {
        fd = -1;
        goto done;
    }
    fd = -1;
    sha256_hex(hash, out);
    ok = 1;

done:
    EVP_MD_CTX_free(context);
    if (fd >= 0) (void)close(fd);
    return ok;
}

static int snapshot(const char *path, char *copy, size_t copy_size, char sha[65])
{
    unsigned char buffer[16384], hash[32];
    EVP_MD_CTX *context = NULL;
    unsigned int hash_length = 0U;
    ssize_t amount;
    size_t offset;
    int input = -1, output = -1;
    int ok = 0;

    if (!path || !copy || !copy_size || !sha) return 0;

    /* [AI:GPT-6 | 2026-10-26] Installed modules may be read-only.
     * Stage candidates beneath Sparkles's existing writable log directory.
     * A private directory prevents untrusted replacement of candidate paths. */
    {
        struct stat state;
        const char *stage = STNLABZ_LOG_DIRECTORY "/.module-staging";
#ifdef SPARKLES_HOTLOAD_TEST_STAGE
        /* Test binary only: production builds never read this override. */
        stage = getenv("SPARKLES_HOTLOAD_TEST_STAGE");
        if (!stage || stage[0] != '/') return 0;
#endif
        if (mkdir(stage, 0700) != 0 && errno != EEXIST) return 0;
        if (lstat(stage, &state) != 0 || !S_ISDIR(state.st_mode) ||
            state.st_uid != geteuid() || (state.st_mode & 0777) != 0700)
            return 0;
        if (snprintf(copy, copy_size, "%s/candidate.XXXXXX", stage) >= (int)copy_size)
            return 0;
    }

    output = mkstemp(copy);
    if (output < 0) return 0;
    input = open(path, O_RDONLY | O_CLOEXEC);
    if (input < 0) goto done;
    context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1)
        goto done;

    for (;;) {
        amount = read(input, buffer, sizeof buffer);
        if (amount < 0 && errno == EINTR) continue;
        if (amount < 0) goto done;
        if (amount == 0) break;
        if (EVP_DigestUpdate(context, buffer, (size_t)amount) != 1)
            goto done;
        for (offset = 0; offset < (size_t)amount;) {
            ssize_t written = write(output, buffer + offset, (size_t)amount - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) goto done;
            offset += (size_t)written;
        }
    }
    if (EVP_DigestFinal_ex(context, hash, &hash_length) != 1 ||
        hash_length != sizeof hash)
        goto done;
    if (close(input) != 0) {
        input = -1;
        goto done;
    }
    input = -1;
    if (close(output) != 0) {
        output = -1;
        goto done;
    }
    output = -1;
    sha256_hex(hash, sha);
    ok = 1;

done:
    EVP_MD_CTX_free(context);
    if (input >= 0) (void)close(input);
    if (output >= 0) (void)close(output);
    if (!ok) (void)unlink(copy);
    return ok;
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

static void log_qualification(const char *id, const char *sha,
                              const stnlabz_module_qualification_result_t *q,
                              stnlabz_module_result_t result)
{
    char detail[512];
    (void)snprintf(detail, sizeof detail,
        "module=%.63s candidate_sha256=%.64s result=%d executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d",
        id, sha, (int)result, q->tests_executed, q->tests_passed,
        q->tests_failed, q->negative_test_executed, q->negative_test_passed);
    (void)stnlabz_log_event("MODULE",
        result == STNLABZ_MODULE_OK ? "QUALIFICATION_GREEN" : "QUALIFICATION_FAILED",
        detail);
    (void)stnlabz_log_sync();
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
    if (result == STNLABZ_MODULE_OK) {
        result = stnlabz_module_registry_qualify(watch->registry, id);
        record = (stnlabz_module_record_t *)
            stnlabz_module_registry_find(watch->registry, id);
        if (record) log_qualification(id, sha, &record->qualification, result);
    }
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
    const stnlabz_module_descriptor_t *old_descriptor;
    const stnlabz_loaded_module_t *old_loaded;
    const stnlabz_module_record_t *current;
    stnlabz_module_qualification_result_t qualification = {0};
    stnlabz_module_qualification_result_t old_qualification;
    stnlabz_module_registry_t isolated;
    stnlabz_module_result_t result;
    stnlabz_loaded_module_t *reserved;
    observed_t *entry;
    size_t capacity;
    int candidate_registered = 0;

    if (!snapshot(binary, staged, sizeof staged, staged_sha)) {
        mark_rejected(watch, id, sha, "candidate_snapshot_failed");
        return;
    }
    if (strcmp(sha, staged_sha)) {
        log_result("HOTLOAD_FAILED", id, sha, "candidate_changed_during_snapshot");
        (void)unlink(staged);
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

    stnlabz_module_registry_init(&isolated);
    result = stnlabz_module_registry_discover(&isolated, descriptor);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_verify(&isolated, id);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_qualify(&isolated, id);
    current = stnlabz_module_registry_find(&isolated, id);
    if (current) qualification = current->qualification;
    log_qualification(id, sha, &qualification, result);
    stnlabz_module_registry_destroy(&isolated);
    if (result != STNLABZ_MODULE_OK) {
        mark_rejected(watch, id, sha, "qualification_failed_incumbent_retained");
        goto done;
    }

    current = stnlabz_module_registry_find(watch->registry, id);
    old_loaded = stnlabz_module_loader_find(watch->loader, id);
    if (!current || current->state != STNLABZ_MODULE_STATE_ACTIVE ||
        !old_loaded || !old_loaded->descriptor) {
        mark_rejected(watch, id, sha, "incumbent_not_available");
        goto done;
    }
    old_descriptor = old_loaded->descriptor;
    old_qualification = current->qualification;

    /* Reserve space for candidate ownership before stopping incumbent. */
    if (watch->loader->count == watch->loader->capacity) {
        capacity = watch->loader->capacity ?
            watch->loader->capacity * 2U : 16U;
        reserved = realloc(watch->loader->modules, capacity * sizeof *reserved);
        if (!reserved) {
            mark_rejected(watch, id, sha, "candidate_ownership_capacity_failed");
            goto done;
        }
        watch->loader->modules = reserved;
        watch->loader->capacity = capacity;
    }

    /*
     * The old shared object stays loaded until candidate activation succeeds.
     * If activation fails, restore the incumbent using its retained descriptor.
     */
    if (!stnlabz_core_stop(watch->registry, id)) {
        mark_rejected(watch, id, sha, "incumbent_stop_failed");
        goto done;
    }
    if (stnlabz_module_registry_unregister(watch->registry, id) !=
        STNLABZ_MODULE_OK) {
        mark_rejected(watch, id, sha, "incumbent_unregister_failed");
        goto restore;
    }
    result = stnlabz_module_registry_discover(watch->registry, descriptor);
    if (result == STNLABZ_MODULE_OK) candidate_registered = 1;
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_verify(watch->registry, id);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_module_registry_restore_qualification(
            watch->registry, id, &qualification);
    if (result == STNLABZ_MODULE_OK)
        result = stnlabz_core_activate_qualified(watch->registry, id, watch->host);
    if (result != STNLABZ_MODULE_OK) {
        mark_rejected(watch, id, sha, "replacement_activation_failed");
        goto restore;
    }

    /* Capacity was reserved; take candidate ownership before retiring old. */
    if (!take_loaded_module(watch->loader, &candidate)) {
        log_result("HOTLOAD_FAILED", id, sha, "candidate_ownership_failed");
        /* An active module must not be unloaded even if transfer fails. */
        candidate.count = 0U;
        goto done;
    }
    if (stnlabz_module_loader_unload(watch->loader, id) !=
        STNLABZ_MODULE_LOADER_OK) {
        log_result("HOTLOAD_FAILED", id, sha, "incumbent_unload_failed");
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
    goto done;

restore:
    if (candidate_registered) {
        const stnlabz_module_record_t *failed =
            stnlabz_module_registry_find(watch->registry, id);
        if (failed && failed->state == STNLABZ_MODULE_STATE_ACTIVE)
            (void)stnlabz_core_stop(watch->registry, id);
        (void)stnlabz_module_registry_unregister(watch->registry, id);
    }
    current = stnlabz_module_registry_find(watch->registry, id);
    if (current && current->state == STNLABZ_MODULE_STATE_STOPPED) {
        result = stnlabz_core_activate_qualified(watch->registry, id, watch->host);
    } else {
        result = stnlabz_module_registry_discover(watch->registry, old_descriptor);
        if (result == STNLABZ_MODULE_OK)
            result = stnlabz_module_registry_verify(watch->registry, id);
        if (result == STNLABZ_MODULE_OK)
            result = stnlabz_module_registry_restore_qualification(
                watch->registry, id, &old_qualification);
        if (result == STNLABZ_MODULE_OK)
            result = stnlabz_core_activate_qualified(
                watch->registry, id, watch->host);
    }
    log_result(result == STNLABZ_MODULE_OK ?
               "HOTLOAD_ROLLBACK_ACTIVE" : "HOTLOAD_ROLLBACK_FAILED",
               id, sha, result == STNLABZ_MODULE_OK ?
               "incumbent_restored" : "incumbent_restoration_failed");

done:
    stnlabz_module_loader_unload_all(&candidate);
    (void)unlink(staged);
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
