#ifndef SPARKLES_MARIADB_INTERNAL_H
#define SPARKLES_MARIADB_INTERNAL_H

#include <stddef.h>
#include <mysql.h>
#include "sparkles_memory.h"
#include "sparkles_contacts.h"
#include "sparkles_show.h"

/* Calendar permission: authenticated platform identity linked to contacts. */
int sparkles_mariadb_identity_parts(const char *account,
                                   const char **platform,
                                   const char **identifier);
int sparkles_mariadb_contact_calendar_allowed(MYSQL *db,const char *account);
int sparkles_mariadb_contact_id(MYSQL *db,const char *account,
                               unsigned long long *contact_id);
int sparkles_mariadb_participants_supported(MYSQL *db);
int sparkles_mariadb_participant_add(MYSQL *db,
                                    unsigned long long appointment_id,
                                    unsigned long long contact_id);
/* Unique first-name contact lookup; no name-based identity authorization. */
int sparkles_mariadb_guest_id(MYSQL *db,const char *name,
                             unsigned long long *contact_id);
int sparkles_mariadb_participant_invite(MYSQL *db,
                                       unsigned long long appointment_id,
                                       unsigned long long contact_id);

/* Called only while MariaDB module's db_lock is held. */
int sparkles_mariadb_show_claim(MYSQL *db,sparkles_show_claim_reply_t *reply);
int sparkles_mariadb_show_respond(MYSQL *db,
    const sparkles_show_response_request_t *request,
    sparkles_show_response_reply_t *reply);
int sparkles_mariadb_show_attempt(MYSQL *db,
    const sparkles_show_attempt_request_t *request);

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

int sparkles_mariadb_contact_identify(
    MYSQL *db,const sparkles_contact_request_t *req,
    sparkles_contact_reply_t *reply);
int sparkles_mariadb_selftests(unsigned int *executed, unsigned int *passed,
                              int *negative_executed, int *negative_passed);

#endif
