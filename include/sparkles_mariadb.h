#ifndef SPARKLES_MARIADB_H
#define SPARKLES_MARIADB_H

/* Core ABI 1.5 named-service contract: status only, no record access. */
#define SPARKLES_MARIADB_STATUS_SERVICE "sparkles.mariadb.status"
#define SPARKLES_MARIADB_EXPECTED_TABLES 7U

typedef struct {
    unsigned int connected;
    unsigned int tables_found;
} sparkles_mariadb_status_t;

#endif
