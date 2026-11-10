#include <stdatomic.h>
#include <string.h>

#include "module_host.h"
#include "sparkles_transport.h"
#include "sparkles_route.h"

#define SPARKLES_HOST_SERVICE_MAX 32U
#define SPARKLES_HOST_COMMAND_MAX 32U

typedef struct {
    char name[STNLABZ_MODULE_SERVICE_NAME_MAX];
    char owner[STNLABZ_MODULE_ID_MAX];
    stnlabz_module_service_handler_fn handler;
    void *context;
    unsigned int in_flight;
    int withdrawing;
} sparkles_service_entry_t;

typedef struct {
    char name[STNLABZ_MODULE_COMMAND_NAME_MAX];
    char owner[STNLABZ_MODULE_ID_MAX];
    stnlabz_module_command_handler_fn handler;
    void *context;
    unsigned int in_flight;
    int withdrawing;
} sparkles_command_entry_t;

static sparkles_service_entry_t services[SPARKLES_HOST_SERVICE_MAX];
static sparkles_command_entry_t commands[SPARKLES_HOST_COMMAND_MAX];
static atomic_flag service_lock = ATOMIC_FLAG_INIT;
static atomic_flag command_lock = ATOMIC_FLAG_INIT;
static _Thread_local char registering_module[STNLABZ_MODULE_ID_MAX];
/* A callback cannot synchronously withdraw itself: its own in_flight
 * reference would make unregister wait forever. Keep a per-thread stack
 * to catch both direct and nested reentrant withdrawal attempts. */
typedef struct sparkles_service_frame {
    const sparkles_service_entry_t *entry;
    struct sparkles_service_frame *previous;
} sparkles_service_frame_t;
typedef struct sparkles_command_frame {
    const sparkles_command_entry_t *entry;
    struct sparkles_command_frame *previous;
} sparkles_command_frame_t;
static _Thread_local sparkles_service_frame_t *current_service_frame;
static _Thread_local sparkles_command_frame_t *current_command_frame;

static int on_service_stack(const sparkles_service_entry_t *entry)
{
    const sparkles_service_frame_t *frame;
    for (frame = current_service_frame; frame; frame = frame->previous)
        if (frame->entry == entry) return 1;
    return 0;
}
static int on_command_stack(const sparkles_command_entry_t *entry)
{
    const sparkles_command_frame_t *frame;
    for (frame = current_command_frame; frame; frame = frame->previous)
        if (frame->entry == entry) return 1;
    return 0;
}


static size_t bounded_length(const char *text, size_t maximum)
{
    size_t length = 0U;
    if (!text) return maximum;
    while (length < maximum && text[length]) ++length;
    return length;
}

static void lock(atomic_flag *flag)
{
    while (atomic_flag_test_and_set_explicit(flag, memory_order_acquire)) {}
}

static void unlock(atomic_flag *flag)
{
    atomic_flag_clear_explicit(flag, memory_order_release);
}

void sparkles_module_host_begin_registration(const char *module_id)
{
    size_t length;
    registering_module[0] = '\0';
    if (!module_id) return;
    length = bounded_length(module_id, sizeof registering_module);
    if (!length || length >= sizeof registering_module) return;
    memcpy(registering_module, module_id, length + 1U);
}

void sparkles_module_host_end_registration(void)
{
    registering_module[0] = '\0';
}

static int register_service(const char *name,
                            stnlabz_module_service_handler_fn handler,
                            void *context)
{
    size_t i;
    size_t free_slot = SPARKLES_HOST_SERVICE_MAX;
    size_t length;

    if (!name || !handler || !strcmp(name, SPARKLES_ROUTER_DISPATCH_SERVICE)) return 0;
    length = bounded_length(name, STNLABZ_MODULE_SERVICE_NAME_MAX);
    if (!length || length >= STNLABZ_MODULE_SERVICE_NAME_MAX) return 0;

    lock(&service_lock);
    for (i = 0; i < SPARKLES_HOST_SERVICE_MAX; ++i) {
        if (services[i].handler && !strcmp(services[i].name, name)) {
            unlock(&service_lock);
            return 0;
        }
        if (!services[i].handler && free_slot == SPARKLES_HOST_SERVICE_MAX)
            free_slot = i;
    }
    if (free_slot == SPARKLES_HOST_SERVICE_MAX) {
        unlock(&service_lock);
        return 0;
    }

    memcpy(services[free_slot].name, name, length + 1U);
    if (registering_module[0])
        memcpy(services[free_slot].owner, registering_module,
               strlen(registering_module) + 1U);
    services[free_slot].handler = handler;
    services[free_slot].context = context;
    unlock(&service_lock);
    return 1;
}

static int unregister_service(const char *name, void *context)
{
    sparkles_service_entry_t *entry = NULL;
    size_t i;

    if (!name) return 0;
    lock(&service_lock);
    for (i = 0; i < SPARKLES_HOST_SERVICE_MAX; ++i) {
        if (services[i].handler &&
            !services[i].withdrawing &&
            services[i].context == context &&
            !strcmp(services[i].name, name)) {
            if (on_service_stack(&services[i])) {
                unlock(&service_lock);
                return 0;
            }
            services[i].withdrawing = 1;
            entry = &services[i];
            break;
        }
    }
    unlock(&service_lock);
    if (!entry) return 0;

    for (;;) {
        lock(&service_lock);
        if (!entry->in_flight) {
            memset(entry, 0, sizeof *entry);
            unlock(&service_lock);
            return 1;
        }
        unlock(&service_lock);
    }
}

static stnlabz_module_result_t route_dispatch_service(
    const void *, size_t, void *, size_t, size_t *, void *);

static stnlabz_module_result_t invoke_service(
    const char *name,
    const void *request,
    size_t request_size,
    void *response,
    size_t response_size,
    size_t *response_used)
{
    sparkles_service_entry_t *entry = NULL;
    stnlabz_module_service_handler_fn handler = NULL;
    void *context = NULL;
    stnlabz_module_result_t result;
    size_t i;

    if (response_used) *response_used = 0U;
    if (!name || (request_size && !request) || (response_size && !response))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    lock(&service_lock);
    for (i = 0; i < SPARKLES_HOST_SERVICE_MAX; ++i) {
        if (services[i].handler &&
            !services[i].withdrawing &&
            !strcmp(services[i].name, name)) {
            entry = &services[i];
            entry->in_flight++;
            handler = entry->handler;
            context = entry->context;
            break;
        }
    }
    unlock(&service_lock);

    if (!handler && !strcmp(name, SPARKLES_ROUTER_DISPATCH_SERVICE))
        return route_dispatch_service(request, request_size, response,
                                      response_size, response_used, NULL);
    if (!handler) return STNLABZ_MODULE_ERR_NOT_FOUND;

    {
        sparkles_service_frame_t frame = {entry, current_service_frame};
        current_service_frame = &frame;
        result = handler(request, request_size, response, response_size,
                         response_used, context);
        current_service_frame = frame.previous;
    }

    lock(&service_lock);
    entry->in_flight--;
    unlock(&service_lock);
    return result;
}

static int send_message(const char *message)
{
    sparkles_transport_channel_request_t request;
    size_t length;

    if (!message) return 0;
    length = bounded_length(message, sizeof request.message);
    if (!length || length >= sizeof request.message) return 0;
    memset(&request, 0, sizeof request);
    memcpy(request.message, message, length + 1U);

    return invoke_service(SPARKLES_TRANSPORT_CHANNEL_SEND_SERVICE,
                          &request, sizeof request,
                          NULL, 0U, NULL) == STNLABZ_MODULE_OK;
}

static int send_private_message(const char *target, const char *message)
{
    sparkles_transport_private_request_t request;
    size_t target_length;
    size_t message_length;

    if (!target || !message) return 0;
    target_length = bounded_length(target, sizeof request.target);
    message_length = bounded_length(message, sizeof request.message);
    if (!target_length || target_length >= sizeof request.target ||
        !message_length || message_length >= sizeof request.message)
        return 0;

    memset(&request, 0, sizeof request);
    memcpy(request.target, target, target_length + 1U);
    memcpy(request.message, message, message_length + 1U);

    return invoke_service(SPARKLES_TRANSPORT_PRIVATE_SEND_SERVICE,
                          &request, sizeof request,
                          NULL, 0U, NULL) == STNLABZ_MODULE_OK;
}

static int register_command(const char *name,
                            stnlabz_module_command_handler_fn handler,
                            void *context)
{
    size_t i;
    size_t free_slot = SPARKLES_HOST_COMMAND_MAX;
    size_t length;

    if (!name || !handler) return 0;
    length = bounded_length(name, STNLABZ_MODULE_COMMAND_NAME_MAX);
    if (!length || length >= STNLABZ_MODULE_COMMAND_NAME_MAX) return 0;

    lock(&command_lock);
    for (i = 0; i < SPARKLES_HOST_COMMAND_MAX; ++i) {
        if (commands[i].handler && !strcmp(commands[i].name, name)) {
            unlock(&command_lock);
            return 0;
        }
        if (!commands[i].handler && free_slot == SPARKLES_HOST_COMMAND_MAX)
            free_slot = i;
    }
    if (free_slot == SPARKLES_HOST_COMMAND_MAX) {
        unlock(&command_lock);
        return 0;
    }

    memcpy(commands[free_slot].name, name, length + 1U);
    if (registering_module[0])
        memcpy(commands[free_slot].owner, registering_module,
               strlen(registering_module) + 1U);
    commands[free_slot].handler = handler;
    commands[free_slot].context = context;
    unlock(&command_lock);
    return 1;
}

static int unregister_command(const char *name, void *context)
{
    sparkles_command_entry_t *entry = NULL;
    size_t i;
    if (!name) return 0;
    lock(&command_lock);
    for (i = 0; i < SPARKLES_HOST_COMMAND_MAX; ++i) {
        if (commands[i].handler && !commands[i].withdrawing &&
            commands[i].context == context &&
            !strcmp(commands[i].name, name)) {
            if (on_command_stack(&commands[i])) {
                unlock(&command_lock);
                return 0;
            }
            commands[i].withdrawing = 1;
            entry = &commands[i];
            break;
        }
    }
    unlock(&command_lock);
    if (!entry) return 0;
    for (;;) {
        lock(&command_lock);
        if (!entry->in_flight) {
            memset(entry, 0, sizeof *entry);
            unlock(&command_lock);
            return 1;
        }
        unlock(&command_lock);
    }
}

/* Core performs only named dispatch. Controller handlers own internal routing. */
static int sparkles_route_capture(void *ctx, const char *message)
{
    sparkles_route_reply_t *out = ctx;
    size_t len;
    if (!out || !message || out->replied) return 0;
    len = bounded_length(message, sizeof out->text);
    if (!len || len >= sizeof out->text) return 0;
    memcpy(out->text, message, len + 1U);
    out->replied = 1;
    return 1;
}


static stnlabz_module_result_t dispatch_command(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *response_used,
    void *context)
{
    const stnlabz_module_command_t *cmd = request;
    sparkles_route_reply_t *out = response;
    stnlabz_module_command_handler_fn handler = NULL;
    sparkles_command_entry_t *entry = NULL;
    stnlabz_module_result_t result;
    void *handler_context = NULL;
    size_t i;
    (void)context;
    if (response_used) *response_used = 0U;
    if (!cmd || request_size != sizeof *cmd || !out ||
        response_size < sizeof *out ||
        !memchr(cmd->name, 0, sizeof cmd->name) || !cmd->name[0] ||
        !memchr(cmd->arguments, 0, sizeof cmd->arguments) ||
        !memchr(cmd->sender, 0, sizeof cmd->sender) ||
        !memchr(cmd->account, 0, sizeof cmd->account))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    lock(&command_lock);
    for (i = 0; i < SPARKLES_HOST_COMMAND_MAX; ++i) {
        if (commands[i].handler && !commands[i].withdrawing &&
            !strcmp(commands[i].name, cmd->name)) {
            entry = &commands[i];
            entry->in_flight++;
            handler = entry->handler;
            handler_context = entry->context;
            break;
        }
    }
    unlock(&command_lock);
    if (!handler) return STNLABZ_MODULE_ERR_NOT_FOUND;
    {
        sparkles_command_frame_t frame = {entry, current_command_frame};
        current_command_frame = &frame;
        result = handler(cmd, sparkles_route_capture, out, handler_context);
        current_command_frame = frame.previous;
    }
    lock(&command_lock);
    entry->in_flight--;
    unlock(&command_lock);
    if (result == STNLABZ_MODULE_OK && response_used)
        *response_used = sizeof *out;
    return result;
}

/* A Core-owned endpoint; no module has to register or know another module. */
static stnlabz_module_result_t route_dispatch_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *response_used,
    void *context)
{
    const sparkles_route_request_t *in = request;
    stnlabz_module_command_t command;
    const char *cursor, *end;
    size_t name_len, arg_len;
    (void)context;
    if (response_used) *response_used = 0U;
    if (!in || request_size != sizeof *in ||
        !memchr(in->sender, 0, sizeof in->sender) ||
        !memchr(in->account, 0, sizeof in->account) ||
        !memchr(in->text, 0, sizeof in->text) || !in->text[0])
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    memset(&command, 0, sizeof command);
    memcpy(command.sender, in->sender, sizeof command.sender);
    memcpy(command.account, in->account, sizeof command.account);

    /* Explicit /module operation; ordinary conversation uses registered chat. */
    cursor = in->text;
    if (*cursor == '/') {
        ++cursor;
        end = cursor;
        while (*end && *end != ' ' && *end != '\t') ++end;
        name_len = (size_t)(end - cursor);
        if (!name_len || name_len >= sizeof command.name)
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
        memcpy(command.name, cursor, name_len);
        while (*end == ' ' || *end == '\t') ++end;
        cursor = end;
    } else {
        memcpy(command.name, "chat", sizeof "chat");
    }
    arg_len = bounded_length(cursor, sizeof command.arguments);
    if (arg_len >= sizeof command.arguments)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memcpy(command.arguments, cursor, arg_len + 1U);
    return dispatch_command(&command, sizeof command,
                            response, response_size, response_used, NULL);
}

static const stnlabz_module_host_t host = {
    .send_message = send_message,
    .send_private_message = send_private_message,
    .register_command = register_command,
    .unregister_command = unregister_command,
    .register_service = register_service,
    .unregister_service = unregister_service,
    .invoke_service = invoke_service
};

const stnlabz_module_host_t *sparkles_module_host(void)
{
    return &host;
}
