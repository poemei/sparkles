#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "logging.h"
#include "module_manager.h"

static stnlabz_legacy_module_result_t recorded(const char *event, const char *id,
    stnlabz_legacy_module_result_t result)
{
    char detail[160];
    /* No admission or lifecycle decision may succeed without its audit trail. */
    if (!stnlabz_log_is_open()) return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    (void)snprintf(detail, sizeof detail, "id=%s result=%d",
                   id ? id : "unknown", (int)result);
    if (!stnlabz_log_event("MODULE", event, detail) || !stnlabz_log_sync())
        return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    return result;
}

/* Admission gate: reject malformed evidence before changing module state. */
static int valid_qualification(const stnlabz_legacy_module_qualification_t *q)
{
    return q && q->tests_executed >= STNLABZ_LEGACY_MODULE_MIN_TESTS &&
        q->tests_failed == 0 &&
        q->tests_passed == q->tests_executed &&
        q->negative_test_executed == 1 &&
        q->negative_test_passed == 1;
}

static int valid_id(const char *s)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < STNLABZ_LEGACY_MODULE_ID_MAX; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c) return i > 0;
        if (!(islower(c) || isdigit(c) || c == '_' || c == '-')) return 0;
    }
    return 0;
}
static stnlabz_legacy_module_record_t *find_mut(stnlabz_legacy_module_manager_t *m, const char *id)
{
    size_t i;
    if (!m || !valid_id(id)) return NULL;
    for (i = 0; i < m->count; ++i)
        if (strcmp(m->modules[i].descriptor.id, id) == 0) return &m->modules[i];
    return NULL;
}
const stnlabz_legacy_module_record_t *stnlabz_legacy_module_find(const stnlabz_legacy_module_manager_t *m, const char *id)
{
    size_t i;
    if (!m || !valid_id(id)) return NULL;
    for (i = 0; i < m->count; ++i)
        if (strcmp(m->modules[i].descriptor.id, id) == 0) return &m->modules[i];
    return NULL;
}
void stnlabz_legacy_module_manager_init(stnlabz_legacy_module_manager_t *m)
{
    if (m) memset(m, 0, sizeof(*m));
    if (stnlabz_log_is_open()) (void)stnlabz_log_event("MODULE", "MANAGER_INIT", "registered=0");
}
stnlabz_legacy_module_result_t stnlabz_legacy_module_register(stnlabz_legacy_module_manager_t *m,
    const stnlabz_legacy_module_descriptor_t *d, const stnlabz_legacy_module_callbacks_t *cb)
{
    stnlabz_legacy_module_record_t *r;
    if (!m || !d || !cb || !valid_id(d->id) || !cb->start || !cb->stop ||
        d->abi_major != STNLABZ_LEGACY_MODULE_ABI_MAJOR ||
        d->abi_minor > STNLABZ_LEGACY_MODULE_ABI_MINOR) return recorded("REGISTER", d ? d->id : NULL, STNLABZ_LEGACY_MODULE_INVALID);
    if (find_mut(m, d->id)) return recorded("REGISTER", d ? d->id : NULL, STNLABZ_LEGACY_MODULE_DUPLICATE);
    if (m->count >= STNLABZ_LEGACY_MODULE_MAX) return recorded("REGISTER", d ? d->id : NULL, STNLABZ_LEGACY_MODULE_FULL);
    if (recorded("REGISTER", d->id, STNLABZ_LEGACY_MODULE_OK) != STNLABZ_LEGACY_MODULE_OK)
        return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    r = &m->modules[m->count++];
    memset(r, 0, sizeof(*r));
    r->descriptor = *d;
    r->callbacks = *cb;
    r->state = STNLABZ_LEGACY_MODULE_REGISTERED;
    return STNLABZ_LEGACY_MODULE_OK;
}
stnlabz_legacy_module_result_t stnlabz_legacy_module_qualify(stnlabz_legacy_module_manager_t *m,
    const char *id, const stnlabz_legacy_module_qualification_t *q)
{
    stnlabz_legacy_module_record_t *r = find_mut(m, id);
    if (!r) return recorded("QUALIFY", id, STNLABZ_LEGACY_MODULE_NOT_FOUND);
    if (r->state != STNLABZ_LEGACY_MODULE_REGISTERED || !valid_qualification(q))
        return recorded("QUALIFY", id, STNLABZ_LEGACY_MODULE_UNQUALIFIED);
    if (recorded("QUALIFY", id, STNLABZ_LEGACY_MODULE_OK) != STNLABZ_LEGACY_MODULE_OK)
        return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    r->qualification = *q;
    r->state = STNLABZ_LEGACY_MODULE_QUALIFIED;
    return STNLABZ_LEGACY_MODULE_OK;
}
stnlabz_legacy_module_result_t stnlabz_legacy_module_authorize(stnlabz_legacy_module_manager_t *m,
    const char *id, int operator_authorized)
{
    stnlabz_legacy_module_record_t *r = find_mut(m, id);
    if (!r) return recorded("AUTHORIZE", id, STNLABZ_LEGACY_MODULE_NOT_FOUND);
    if (r->state != STNLABZ_LEGACY_MODULE_QUALIFIED && r->state != STNLABZ_LEGACY_MODULE_STOPPED)
        return recorded("AUTHORIZE", id, STNLABZ_LEGACY_MODULE_UNQUALIFIED);
    if (operator_authorized != 1) {
        r->activation_authorized = 0;
        return recorded("AUTHORIZE", id, STNLABZ_LEGACY_MODULE_UNQUALIFIED);
    }
    if (!valid_qualification(&r->qualification))
        return recorded("AUTHORIZE", id, STNLABZ_LEGACY_MODULE_UNQUALIFIED);
    if (recorded("AUTHORIZE", id, STNLABZ_LEGACY_MODULE_OK) != STNLABZ_LEGACY_MODULE_OK)
        return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    r->activation_authorized = 1;
    return STNLABZ_LEGACY_MODULE_OK;
}
stnlabz_legacy_module_result_t stnlabz_legacy_module_activate(stnlabz_legacy_module_manager_t *m, const char *id)
{
    stnlabz_legacy_module_record_t *r = find_mut(m, id);
    if (!r) return recorded("ACTIVATE", id, STNLABZ_LEGACY_MODULE_NOT_FOUND);
    if ((r->state != STNLABZ_LEGACY_MODULE_QUALIFIED && r->state != STNLABZ_LEGACY_MODULE_STOPPED) ||
        !valid_qualification(&r->qualification) || !r->activation_authorized)
        return recorded("ACTIVATE", id, STNLABZ_LEGACY_MODULE_UNQUALIFIED);
    if (recorded("ACTIVATE_ATTEMPT", id, STNLABZ_LEGACY_MODULE_OK) != STNLABZ_LEGACY_MODULE_OK)
        return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    if (!r->callbacks.start(r->callbacks.context)) return recorded("ACTIVATE", id, STNLABZ_LEGACY_MODULE_CALLBACK_FAILED);
    r->state = STNLABZ_LEGACY_MODULE_ACTIVE;
    return recorded("ACTIVATE", id, STNLABZ_LEGACY_MODULE_OK);
}
stnlabz_legacy_module_result_t stnlabz_legacy_module_stop(stnlabz_legacy_module_manager_t *m, const char *id)
{
    stnlabz_legacy_module_record_t *r = find_mut(m, id);
    if (!r) return recorded("STOP", id, STNLABZ_LEGACY_MODULE_NOT_FOUND);
    if (r->state != STNLABZ_LEGACY_MODULE_ACTIVE) return recorded("STOP", id, STNLABZ_LEGACY_MODULE_INVALID);
    if (recorded("STOP_ATTEMPT", id, STNLABZ_LEGACY_MODULE_OK) != STNLABZ_LEGACY_MODULE_OK)
        return STNLABZ_LEGACY_MODULE_CALLBACK_FAILED;
    r->callbacks.stop(r->callbacks.context);
    r->state = STNLABZ_LEGACY_MODULE_STOPPED;
    r->activation_authorized = 0;
    return recorded("STOP", id, STNLABZ_LEGACY_MODULE_OK);
}
void stnlabz_legacy_module_manager_shutdown(stnlabz_legacy_module_manager_t *m)
{
    size_t i;
    if (!m) return;
    for (i = m->count; i > 0; --i) {
        stnlabz_legacy_module_record_t *r = &m->modules[i-1];
        if (r->state == STNLABZ_LEGACY_MODULE_ACTIVE) {
            r->callbacks.stop(r->callbacks.context);
            r->state = STNLABZ_LEGACY_MODULE_STOPPED;
            (void)recorded("SHUTDOWN_STOP", r->descriptor.id, STNLABZ_LEGACY_MODULE_OK);
        }
        r->activation_authorized = 0;
    }
    (void)recorded("MANAGER_SHUTDOWN", NULL, STNLABZ_LEGACY_MODULE_OK);
}
