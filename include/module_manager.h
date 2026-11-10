#ifndef STNLABZ_LEGACY_MODULE_MANAGER_H
#define STNLABZ_LEGACY_MODULE_MANAGER_H
#include <stddef.h>
#define STNLABZ_LEGACY_MODULE_MAX 32
#define STNLABZ_LEGACY_MODULE_ID_MAX 64
#define STNLABZ_LEGACY_MODULE_ABI_MAJOR 1U
#define STNLABZ_LEGACY_MODULE_ABI_MINOR 0U
#define STNLABZ_LEGACY_MODULE_MIN_TESTS 1U

typedef enum {
    STNLABZ_LEGACY_MODULE_REGISTERED = 0,
    STNLABZ_LEGACY_MODULE_QUALIFIED = 1,
    STNLABZ_LEGACY_MODULE_ACTIVE = 2,
    STNLABZ_LEGACY_MODULE_STOPPED = 3
} stnlabz_legacy_module_state_t;
typedef enum {
    STNLABZ_LEGACY_MODULE_OK = 0,
    STNLABZ_LEGACY_MODULE_INVALID = 1,
    STNLABZ_LEGACY_MODULE_DUPLICATE = 2,
    STNLABZ_LEGACY_MODULE_FULL = 3,
    STNLABZ_LEGACY_MODULE_NOT_FOUND = 4,
    STNLABZ_LEGACY_MODULE_UNQUALIFIED = 5,
    STNLABZ_LEGACY_MODULE_CALLBACK_FAILED = 6
} stnlabz_legacy_module_result_t;
typedef struct {
    char id[STNLABZ_LEGACY_MODULE_ID_MAX];
    unsigned int abi_major, abi_minor;
    unsigned int version_major, version_minor, version_patch;
} stnlabz_legacy_module_descriptor_t;
typedef struct {
    unsigned int tests_executed, tests_passed, tests_failed;
    int negative_test_executed, negative_test_passed;
} stnlabz_legacy_module_qualification_t;
typedef struct {
    int (*start)(void *context);
    void (*stop)(void *context);
    void *context;
} stnlabz_legacy_module_callbacks_t;
typedef struct {
    stnlabz_legacy_module_descriptor_t descriptor;
    stnlabz_legacy_module_qualification_t qualification;
    stnlabz_legacy_module_callbacks_t callbacks;
    stnlabz_legacy_module_state_t state;
    int activation_authorized;
} stnlabz_legacy_module_record_t;
typedef struct {
    stnlabz_legacy_module_record_t modules[STNLABZ_LEGACY_MODULE_MAX];
    size_t count;
} stnlabz_legacy_module_manager_t;

void stnlabz_legacy_module_manager_init(stnlabz_legacy_module_manager_t *manager);
stnlabz_legacy_module_result_t stnlabz_legacy_module_register(stnlabz_legacy_module_manager_t *manager,
    const stnlabz_legacy_module_descriptor_t *descriptor, const stnlabz_legacy_module_callbacks_t *callbacks);
stnlabz_legacy_module_result_t stnlabz_legacy_module_qualify(stnlabz_legacy_module_manager_t *manager,
    const char *id, const stnlabz_legacy_module_qualification_t *evidence);
stnlabz_legacy_module_result_t stnlabz_legacy_module_authorize(stnlabz_legacy_module_manager_t *manager,
    const char *id, int operator_authorized);
stnlabz_legacy_module_result_t stnlabz_legacy_module_activate(stnlabz_legacy_module_manager_t *manager, const char *id);
stnlabz_legacy_module_result_t stnlabz_legacy_module_stop(stnlabz_legacy_module_manager_t *manager, const char *id);
void stnlabz_legacy_module_manager_shutdown(stnlabz_legacy_module_manager_t *manager);
const stnlabz_legacy_module_record_t *stnlabz_legacy_module_find(const stnlabz_legacy_module_manager_t *manager, const char *id);
#endif
