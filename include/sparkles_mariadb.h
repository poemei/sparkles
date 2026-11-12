#ifndef SPARKLES_MARIADB_H
#define SPARKLES_MARIADB_H

/* Private Core ABI 1.5 services. Callers must forward a verified transport
 * account; MariaDB checks the account allowlist before returning records. */
#define SPARKLES_MARIADB_CHECK_SERVICE "sparkles.mariadb.appointments.check"
#define SPARKLES_MARIADB_CREATE_SERVICE "sparkles.mariadb.appointments.create"
#define SPARKLES_MARIADB_MUTATE_SERVICE "sparkles.mariadb.appointments.mutate"
#define SPARKLES_MARIADB_STATUS_SERVICE "sparkles.mariadb.status"
#define SPARKLES_MARIADB_APPOINTMENTS_SERVICE "sparkles.mariadb.appointments.read"
#define SPARKLES_MARIADB_EXPECTED_TABLES 7U
#define SPARKLES_MARIADB_APPOINTMENTS_MAX 8U

typedef struct {
    unsigned int connected;
    unsigned int tables_found;
} sparkles_mariadb_status_t;

/* Caller supplies a server-verified account; MariaDB applies its allowlist.
 * Date-only inclusive start and exclusive end; both YYYY-MM-DD. */
typedef struct {
    char account[128];
    char from_date[11];
    char until_date[11];
} sparkles_mariadb_appointments_request_t;

typedef struct {
    unsigned long long id;
    char title[801];
    char start_time[20];
    char end_time[20];
} sparkles_mariadb_appointment_t;

typedef struct {
    unsigned int count;
    unsigned int has_more;
    sparkles_mariadb_appointment_t records[SPARKLES_MARIADB_APPOINTMENTS_MAX];
} sparkles_mariadb_appointments_reply_t;

/* Trusted in-process caller only. Account must originate from verified transport.
 * Database service enforces the same runtime allowlist as /appointment-add. */
typedef struct {
    char account[128];
    char start_time[20];
    char end_time[20];
    char title[201];
} sparkles_mariadb_create_request_t;

typedef enum {
    SPARKLES_MARIADB_CHECK_ERROR=0,
    SPARKLES_MARIADB_CHECK_FREE=1,
    SPARKLES_MARIADB_CHECK_CONFLICT=2,
    SPARKLES_MARIADB_CHECK_DENIED=3
} sparkles_mariadb_check_status_t;

typedef struct {
    unsigned int status;
} sparkles_mariadb_check_reply_t;

typedef enum {
    SPARKLES_MARIADB_CREATE_ERROR = 0,
    SPARKLES_MARIADB_CREATE_SAVED = 1,
    SPARKLES_MARIADB_CREATE_CONFLICT = 2,
    SPARKLES_MARIADB_CREATE_UNCERTAIN = 3,
    SPARKLES_MARIADB_CREATE_DENIED = 4
} sparkles_mariadb_create_status_t;

typedef struct {
    unsigned int status;
    unsigned long long id;
} sparkles_mariadb_create_reply_t;

/* Authenticated, optimistic compare-and-change: exact existing record must
 * still be scheduled and match title/start/end from the authorized read.
 * No model-generated SQL, no mutation without explicit human confirmation. */
typedef enum {
    SPARKLES_MARIADB_MUTATE_CANCEL=1,
    SPARKLES_MARIADB_MUTATE_UPDATE=2
} sparkles_mariadb_mutate_operation_t;
typedef struct {
    char account[128];
    unsigned long long id;
    unsigned int operation;
    char previous_title[801];
    char previous_start[20];
    char previous_end[20];
    char next_title[201];
    char next_start[20];
    char next_end[20];
} sparkles_mariadb_mutate_request_t;
typedef enum {
    SPARKLES_MARIADB_MUTATE_ERROR=0,
    SPARKLES_MARIADB_MUTATE_APPLIED=1,
    SPARKLES_MARIADB_MUTATE_STALE=2,
    SPARKLES_MARIADB_MUTATE_CONFLICT=3,
    SPARKLES_MARIADB_MUTATE_UNCERTAIN=4,
    SPARKLES_MARIADB_MUTATE_DENIED=5
} sparkles_mariadb_mutate_status_t;
typedef struct {
    unsigned int status;
} sparkles_mariadb_mutate_reply_t;

#endif
