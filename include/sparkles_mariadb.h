#ifndef SPARKLES_MARIADB_H
#define SPARKLES_MARIADB_H

/* Private Core ABI 1.5 services. Caller authentication is not part of the ABI.
 * Do not expose appointment lookup to IRC/chat until verified caller identity
 * and authorization are implemented. */
#define SPARKLES_MARIADB_STATUS_SERVICE "sparkles.mariadb.status"
#define SPARKLES_MARIADB_APPOINTMENTS_SERVICE "sparkles.mariadb.appointments.read"
#define SPARKLES_MARIADB_EXPECTED_TABLES 7U
#define SPARKLES_MARIADB_APPOINTMENTS_MAX 8U

typedef struct {
    unsigned int connected;
    unsigned int tables_found;
} sparkles_mariadb_status_t;

/* Date-only inclusive start and exclusive end; both YYYY-MM-DD. */
typedef struct {
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

#endif
