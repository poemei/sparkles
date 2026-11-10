#ifndef STNLABZ_MODULE_H
#define STNLABZ_MODULE_H

/*
 * STN-LABZ
 * Module ABI 1.5
 *
 * module.h
 *
 * Cross-application module ABI definitions.
 */

#include <stddef.h>

#define STNLABZ_MODULE_ID_MAX 64
#define STNLABZ_MODULE_NAME_MAX 64
#define STNLABZ_MODULE_COMMAND_NAME_MAX 64
#define STNLABZ_MODULE_COMMAND_ARGUMENTS_MAX 512
#define STNLABZ_MODULE_COMMAND_SENDER_MAX 128
#define STNLABZ_MODULE_COMMAND_ACCOUNT_MAX 128
#define STNLABZ_MODULE_SERVICE_NAME_MAX 128
#define STNLABZ_MODULE_MIN_TESTS 10

#define STNLABZ_MODULE_API_MAJOR 1
#define STNLABZ_MODULE_API_MINOR 5

typedef enum
{
    STNLABZ_MODULE_STATE_DISCOVERED = 0,
    STNLABZ_MODULE_STATE_UNVERIFIED,
    STNLABZ_MODULE_STATE_TESTING,
    STNLABZ_MODULE_STATE_QUALIFIED,
    STNLABZ_MODULE_STATE_ACTIVE,
    STNLABZ_MODULE_STATE_STOPPED,
    STNLABZ_MODULE_STATE_FAILED,
    STNLABZ_MODULE_STATE_QUARANTINED,
    STNLABZ_MODULE_STATE_UNREGISTERED
} stnlabz_module_state_t;

typedef enum
{
    STNLABZ_MODULE_OK = 0,
    STNLABZ_MODULE_ERR_INVALID_ARGUMENT,
    STNLABZ_MODULE_ERR_INVALID_IDENTITY,
    STNLABZ_MODULE_ERR_DUPLICATE,
    STNLABZ_MODULE_ERR_REGISTRY_FULL,
    STNLABZ_MODULE_ERR_NOT_FOUND,
    STNLABZ_MODULE_ERR_INCOMPATIBLE,
    STNLABZ_MODULE_ERR_INVALID_STATE,
    STNLABZ_MODULE_ERR_QUALIFICATION,
    STNLABZ_MODULE_ERR_NOT_QUALIFIED,
    STNLABZ_MODULE_ERR_NOT_AUTHORIZED,
    STNLABZ_MODULE_ERR_QUARANTINED,
    STNLABZ_MODULE_ERR_AUDIT_FULL,
    STNLABZ_MODULE_ERR_START_FAILED,
    STNLABZ_MODULE_ERR_STOP_FAILED
} stnlabz_module_result_t;

typedef struct
{
    unsigned int tests_executed;
    unsigned int tests_passed;
    unsigned int tests_failed;
    int negative_test_executed;
    int negative_test_passed;
} stnlabz_module_qualification_result_t;

typedef struct
{
    char sender[STNLABZ_MODULE_COMMAND_SENDER_MAX];
    char account[STNLABZ_MODULE_COMMAND_ACCOUNT_MAX];
    char name[STNLABZ_MODULE_COMMAND_NAME_MAX];
    char arguments[STNLABZ_MODULE_COMMAND_ARGUMENTS_MAX];
} stnlabz_module_command_t;

typedef int (*stnlabz_module_command_reply_fn)(void *reply_context, const char *message);

typedef stnlabz_module_result_t (*stnlabz_module_command_handler_fn)(
    const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,
    void *reply_context,
    void *handler_context
);

/*
 * ABI 1.5 service boundary.
 *
 * Modules register named capabilities with Core. Consumers invoke those
 * capabilities through Core and never dlopen, locate, or bind directly to
 * another module. Input/output are opaque byte buffers whose schema belongs
 * to the named service contract.
 */
typedef stnlabz_module_result_t (*stnlabz_module_service_handler_fn)(
    const void *request,
    size_t request_size,
    void *response,
    size_t response_size,
    size_t *response_used,
    void *handler_context
);

typedef int (*stnlabz_module_send_message_fn)(const char *message);
typedef int (*stnlabz_module_send_private_message_fn)(
    const char *target,
    const char *message
);

typedef int (*stnlabz_module_register_command_fn)(
    const char *name,
    stnlabz_module_command_handler_fn handler,
    void *handler_context
);

typedef int (*stnlabz_module_unregister_command_fn)(
    const char *name,
    void *handler_context
);

typedef int (*stnlabz_module_register_service_fn)(
    const char *name,
    stnlabz_module_service_handler_fn handler,
    void *handler_context
);

typedef int (*stnlabz_module_unregister_service_fn)(
    const char *name,
    void *handler_context
);

typedef stnlabz_module_result_t (*stnlabz_module_invoke_service_fn)(
    const char *name,
    const void *request,
    size_t request_size,
    void *response,
    size_t response_size,
    size_t *response_used
);

typedef struct
{
    stnlabz_module_send_message_fn send_message;
    stnlabz_module_send_private_message_fn send_private_message;
    stnlabz_module_register_command_fn register_command;
    stnlabz_module_unregister_command_fn unregister_command;
    stnlabz_module_register_service_fn register_service;
    stnlabz_module_unregister_service_fn unregister_service;
    stnlabz_module_invoke_service_fn invoke_service;
} stnlabz_module_host_t;

typedef stnlabz_module_result_t (*stnlabz_module_qualify_fn)(
    stnlabz_module_qualification_result_t *result
);

typedef stnlabz_module_result_t (*stnlabz_module_start_fn)(
    const stnlabz_module_host_t *host
);

typedef stnlabz_module_result_t (*stnlabz_module_stop_fn)(void);

typedef struct
{
    char id[STNLABZ_MODULE_ID_MAX];
    char name[STNLABZ_MODULE_NAME_MAX];
    unsigned int version_major;
    unsigned int version_minor;
    unsigned int version_patch;
    unsigned int required_core_api_major;
    unsigned int required_core_api_minor;
    stnlabz_module_qualify_fn qualify;
    stnlabz_module_start_fn start;
    stnlabz_module_stop_fn stop;
} stnlabz_module_descriptor_t;

const char *stnlabz_module_state_string(stnlabz_module_state_t state);
const char *stnlabz_module_result_string(stnlabz_module_result_t result);

#endif
