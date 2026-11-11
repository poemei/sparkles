#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "stnlabz_abi/module.h"
#include "sparkles_mariadb.h"
#include "mariadb_internal.h"

static MYSQL *db;
static pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;
static const stnlabz_module_host_t *host_owner;
static int registered;

static unsigned int schema_table_count(MYSQL *connection)
{
    static const char sql[] =
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name IN ("
        "'contacts','appointments','tasks','appointment_participants',"
        "'interactions','contact_identities','contact_permissions')";
    MYSQL_RES *res;
    MYSQL_ROW row;
    unsigned long n;
    char *end;
    if (mysql_query(connection, sql)) return 0U;
    res = mysql_store_result(connection);
    if (!res) return 0U;
    row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        mysql_free_result(res);
        return 0U;
    }
    n = strtoul(row[0], &end, 10);
    if (!*row[0] || *end || n > SPARKLES_MARIADB_EXPECTED_TABLES)
        n = 0;
    mysql_free_result(res);
    return (unsigned int)n;
}

static stnlabz_module_result_t status_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *used, void *ctx)
{
    sparkles_mariadb_status_t *out = response;
    unsigned int count;
    (void)ctx;
    if (used) *used = 0U;
    if (request || request_size || !out || response_size < sizeof *out)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    (void)pthread_mutex_lock(&db_lock);
    if (!db || mysql_ping(db)) {
        (void)pthread_mutex_unlock(&db_lock);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    count = schema_table_count(db);
    (void)pthread_mutex_unlock(&db_lock);
    if (count != SPARKLES_MARIADB_EXPECTED_TABLES)
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    out->connected = 1U;
    out->tables_found = count;
    if (used) *used = sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Read-only, bounded date-range query. Not yet routed to IRC/chat:
 * the current transport does not populate verified caller account identity. */
static stnlabz_module_result_t appointments_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *used, void *ctx)
{
    static const char sql[] =
        "SELECT id, title, "
        "DATE_FORMAT(start_time,'%Y-%m-%d %H:%i:%s'), "
        "DATE_FORMAT(end_time,'%Y-%m-%d %H:%i:%s') "
        "FROM appointments "
        "WHERE status='scheduled' AND start_time < ? AND end_time > ? "
        "ORDER BY start_time, id LIMIT 9";
    const sparkles_mariadb_appointments_request_t *in = request;
    sparkles_mariadb_appointments_reply_t *out = response;
    MYSQL_STMT *stmt = NULL;
    MYSQL_BIND params[2] = {{0}}, result[4] = {{0}};
    char from[20], until[20], title[801], start[20], end[20];
    unsigned long title_len = 0U, start_len = 0U, end_len = 0U;
    unsigned long params_len[2] = {19UL, 19UL};
    unsigned long long id = 0ULL;
    unsigned int count = 0U;
    int rc, okay = 1;
    (void)ctx;
    if (used) *used = 0U;
    if (!in || request_size != sizeof *in || !out || response_size < sizeof *out ||
        !memchr(in->from_date,0,sizeof in->from_date) ||
        !memchr(in->until_date,0,sizeof in->until_date) ||
        !sparkles_mariadb_valid_range(in->from_date,in->until_date))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    (void)snprintf(from,sizeof from,"%s 00:00:00",in->from_date);
    (void)snprintf(until,sizeof until,"%s 00:00:00",in->until_date);
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = until;
    params[0].buffer_length = 19U;
    params[0].length = &params_len[0];
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = from;
    params[1].buffer_length = 19U;
    params[1].length = &params_len[1];

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = title;
    result[1].buffer_length = sizeof title;
    result[1].length = &title_len;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = start;
    result[2].buffer_length = sizeof start;
    result[2].length = &start_len;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = end;
    result[3].buffer_length = sizeof end;
    result[3].length = &end_len;

    (void)pthread_mutex_lock(&db_lock);
    if (!db || mysql_ping(db)) okay = 0;
    if (okay) stmt = mysql_stmt_init(db);
    if (!stmt) okay = 0;
    if (okay && mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql))) okay = 0;
    if (okay && mysql_stmt_bind_param(stmt,params)) okay = 0;
    if (okay && mysql_stmt_execute(stmt)) okay = 0;
    if (okay && mysql_stmt_bind_result(stmt,result)) okay = 0;
    if (okay && mysql_stmt_store_result(stmt)) okay = 0;
    while (okay) {
        rc = mysql_stmt_fetch(stmt);
        if (rc == MYSQL_NO_DATA) break;
        if (rc != 0 || title_len >= sizeof title ||
            start_len != 19U || end_len != 19U) {
            okay = 0;
            break;
        }
        if (count == SPARKLES_MARIADB_APPOINTMENTS_MAX) {
            out->has_more = 1U;
            break;
        }
        out->records[count].id = id;
        /* Never leak a partially cut title as if it were complete. */
        if (title_len >= sizeof out->records[count].title) {
            okay = 0;
            break;
        }
        memcpy(out->records[count].title,title,title_len);
        out->records[count].title[title_len] = '\0';
        memcpy(out->records[count].start_time,start,19U);
        out->records[count].start_time[19] = '\0';
        memcpy(out->records[count].end_time,end,19U);
        out->records[count].end_time[19] = '\0';
        ++count;
    }
    if (stmt) mysql_stmt_close(stmt);
    (void)pthread_mutex_unlock(&db_lock);
    if (!okay) {
        memset(out,0,sizeof *out);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    out->count = count;
    if (used) *used = sizeof *out;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t qualify(stnlabz_module_qualification_result_t *q)
{
    unsigned int e = 0U, p = 0U;
    int ne = 0, np = 0;
    int ok;
    unsigned int date_e=0U, date_p=0U;
    if (!q) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(q, 0, sizeof *q);
    ok = sparkles_mariadb_selftests(&e, &p, &ne, &np);
    ok = sparkles_mariadb_date_selftests(&date_e,&date_p) && ok;
    e += date_e;
    p += date_p;
    q->tests_executed = e;
    q->tests_passed = p;
    q->tests_failed = e - p;
    q->negative_test_executed = ne;
    q->negative_test_passed = np;
    return ok ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_QUALIFICATION;
}

static stnlabz_module_result_t start(const stnlabz_module_host_t *h)
{
    sparkles_mariadb_config_t config;
    MYSQL *candidate;
    unsigned int timeout = 3U;
    if (!h || !h->register_service || !h->unregister_service)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    if (!sparkles_mariadb_load_config(SPARKLES_MARIADB_CONFIG, &config)) {
        fprintf(stderr, "[MARIADB] credential file missing or invalid\n");
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    candidate = mysql_init(NULL);
    if (!candidate) {
        memset(&config, 0, sizeof config);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    (void)mysql_options(candidate, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    (void)mysql_options(candidate, MYSQL_OPT_READ_TIMEOUT, &timeout);
    if (!mysql_real_connect(candidate, SPARKLES_MARIADB_HOST,
            SPARKLES_MARIADB_USER, config.password,
            SPARKLES_MARIADB_DATABASE, 0U, NULL, 0UL)) {
        fprintf(stderr, "[MARIADB] local database connection failed\n");
        memset(&config, 0, sizeof config);
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    memset(&config, 0, sizeof config);
    if (schema_table_count(candidate) != SPARKLES_MARIADB_EXPECTED_TABLES) {
        fprintf(stderr, "[MARIADB] expected seven tables not found\n");
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    if (!h->register_service(SPARKLES_MARIADB_STATUS_SERVICE,
                             status_service, NULL)) {
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    db = candidate;
    if (!h->register_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,
                             appointments_service, NULL)) {
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE, NULL);
        db = NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    host_owner = h;
    registered = 1;
    fprintf(stderr, "[MARIADB] local database connected; seven tables verified\n");
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t stop(void)
{
    if (registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE, NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    if (registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE, NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    registered = 0;
    host_owner = NULL;
    (void)pthread_mutex_lock(&db_lock);
    if (db) mysql_close(db);
    (void)pthread_mutex_unlock(&db_lock);
    db = NULL;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor = {
    "mariadb", "Sparkles MariaDB Context Foundation", 1U, 1U, 0U,
    STNLABZ_MODULE_API_MAJOR, STNLABZ_MODULE_API_MINOR,
    qualify, start, stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
