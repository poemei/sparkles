#ifndef SPARKLES_MARIADB_INTERNAL_H
#define SPARKLES_MARIADB_INTERNAL_H

#include <stddef.h>
#include <mysql.h>
#include "sparkles_memory.h"

#define SPARKLES_MARIADB_IRC_ACCOUNTS "/opt/sparkles/auth/mariadb/irc-accounts.conf"
int sparkles_mariadb_account_allowed(const char *path, const char *account);

#define SPARKLES_MARIADB_CONFIG "/opt/sparkles/auth/mariadb/mariadb.env"
#define SPARKLES_MARIADB_HOST "localhost"
#define SPARKLES_MARIADB_DATABASE "sparkles"
#define SPARKLES_MARIADB_USER "sparkles"

typedef struct {
    char password[256];
} sparkles_mariadb_config_t;

int sparkles_mariadb_parse_line(const char *line, char *password,
                               size_t password_size, int *seen);
int sparkles_mariadb_load_config(const char *path, sparkles_mariadb_config_t *out);
int sparkles_mariadb_valid_date(const char *date);
int sparkles_mariadb_valid_range(const char *from, const char *until);
/* Explicit same-day appointment command parser; no natural-language inference. */
typedef struct {
    char start_time[20];
    char end_time[20];
    char title[201];
} sparkles_mariadb_new_appointment_t;
int sparkles_mariadb_parse_new_appointment(
    const char *arguments, sparkles_mariadb_new_appointment_t *out);
int sparkles_mariadb_appointment_selftests(unsigned int *executed,
                                          unsigned int *passed);

int sparkles_mariadb_date_selftests(unsigned int *executed,
                                  unsigned int *passed);

int sparkles_mariadb_memory_valid(
    const sparkles_memory_exchange_request_t *request);
int sparkles_mariadb_memory_selftests(
    unsigned int *executed,unsigned int *passed);
int sparkles_mariadb_context_exchange(
    MYSQL *db,const sparkles_memory_exchange_request_t *request,
    sparkles_memory_exchange_reply_t *reply);

int sparkles_mariadb_selftests(unsigned int *executed, unsigned int *passed,
                              int *negative_executed, int *negative_passed);

#endif
