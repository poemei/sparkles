#ifndef SPARKLES_MARIADB_INTERNAL_H
#define SPARKLES_MARIADB_INTERNAL_H

#include <stddef.h>

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
int sparkles_mariadb_date_selftests(unsigned int *executed,
                                  unsigned int *passed);

int sparkles_mariadb_selftests(unsigned int *executed, unsigned int *passed,
                              int *negative_executed, int *negative_passed);

#endif
