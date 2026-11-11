#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stnlabz_abi/module.h"
#include "sparkles_mariadb.h"
#include "mariadb_internal.h"

static MYSQL *db;
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
    if (!db || mysql_ping(db))
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    count = schema_table_count(db);
    if (count != SPARKLES_MARIADB_EXPECTED_TABLES)
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    out->connected = 1U;
    out->tables_found = count;
    if (used) *used = sizeof *out;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t qualify(stnlabz_module_qualification_result_t *q)
{
    unsigned int e = 0U, p = 0U;
    int ne = 0, np = 0;
    int ok;
    if (!q) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(q, 0, sizeof *q);
    ok = sparkles_mariadb_selftests(&e, &p, &ne, &np);
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
    registered = 0;
    host_owner = NULL;
    if (db) mysql_close(db);
    db = NULL;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor = {
    "mariadb", "Sparkles MariaDB Context Foundation", 1U, 0U, 0U,
    STNLABZ_MODULE_API_MAJOR, STNLABZ_MODULE_API_MINOR,
    qualify, start, stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
