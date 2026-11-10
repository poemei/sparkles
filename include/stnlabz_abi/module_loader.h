#ifndef STNLABZ_MODULE_LOADER_H
#define STNLABZ_MODULE_LOADER_H

#include <stddef.h>

#include "module.h"

#define STNLABZ_MODULE_LOADER_PATH_MAX 1024
#define STNLABZ_MODULE_DESCRIPTOR_EXPORT "stnlabz_module_get_descriptor"

typedef enum
{
    STNLABZ_MODULE_LOADER_OK = 0,
    STNLABZ_MODULE_LOADER_ERR_INVALID_ARGUMENT,
    STNLABZ_MODULE_LOADER_ERR_FULL,
    STNLABZ_MODULE_LOADER_ERR_ALREADY_LOADED,
    STNLABZ_MODULE_LOADER_ERR_LOAD_FAILED,
    STNLABZ_MODULE_LOADER_ERR_EXPORT_MISSING,
    STNLABZ_MODULE_LOADER_ERR_DESCRIPTOR_INVALID,
    STNLABZ_MODULE_LOADER_ERR_ID_MISMATCH,
    STNLABZ_MODULE_LOADER_ERR_NOT_FOUND
} stnlabz_module_loader_result_t;

typedef const stnlabz_module_descriptor_t *
(*stnlabz_module_get_descriptor_fn)(void);

typedef struct
{
    void *handle;
    char module_id[STNLABZ_MODULE_ID_MAX];
    char dll_path[STNLABZ_MODULE_LOADER_PATH_MAX];
    const stnlabz_module_descriptor_t *descriptor;
} stnlabz_loaded_module_t;

typedef struct
{
    stnlabz_loaded_module_t *modules;
    size_t count;
    size_t capacity;
} stnlabz_module_loader_t;

void stnlabz_module_loader_init(stnlabz_module_loader_t *loader);

stnlabz_module_loader_result_t
stnlabz_module_loader_load(
    stnlabz_module_loader_t *loader,
    const char *expected_module_id,
    const char *module_path,
    const stnlabz_module_descriptor_t **descriptor_out
);

stnlabz_module_loader_result_t
stnlabz_module_loader_unload(
    stnlabz_module_loader_t *loader,
    const char *module_id
);

/* Unloads handles and releases dynamic storage. */
void stnlabz_module_loader_unload_all(stnlabz_module_loader_t *loader);

const stnlabz_loaded_module_t *
stnlabz_module_loader_find(
    const stnlabz_module_loader_t *loader,
    const char *module_id
);

const char *
stnlabz_module_loader_result_string(
    stnlabz_module_loader_result_t result
);

#endif
