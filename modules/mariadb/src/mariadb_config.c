#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mariadb_internal.h"

/* Strict single-key credential file, no shell evaluation or environment leaks. */
int sparkles_mariadb_parse_line(const char *line, char *password,
                               size_t password_size, int *seen)
{
    static const char key[] = "SPARKLES_DB_PASSWORD=";
    size_t n;
    if (!line || !password || !seen || *seen || !password_size) return 0;
    if (strncmp(line, key, sizeof key - 1U)) return 0;
    line += sizeof key - 1U;
    n = strcspn(line, "\r\n");
    if (!n || n >= password_size || line[n] == '\r' ||
        (line[n] && (line[n] != '\n' || line[n+1] != '\0'))) return 0;
    if (memchr(line, '\r', n) || memchr(line, '\n', n)) return 0;
    memcpy(password, line, n);
    password[n] = '\0';
    *seen = 1;
    return 1;
}

int sparkles_mariadb_load_config(const char *path, sparkles_mariadb_config_t *out)
{
    FILE *fp;
    struct stat st;
    char line[512];
    int seen = 0, valid = 1;
    if (!path || !out) return 0;
    memset(out, 0, sizeof *out);
    if (stat(path, &st) || !S_ISREG(st.st_mode) ||
        (st.st_mode & 077U) != 0U ||
        st.st_uid != geteuid()) return 0;
    fp = fopen(path, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof line, fp)) {
        if (!strchr(line, '\n') && !feof(fp)) { valid = 0; break; }
        if (!sparkles_mariadb_parse_line(line, out->password,
                                         sizeof out->password, &seen)) {
            valid = 0; break;
        }
    }
    if (ferror(fp)) valid = 0;
    fclose(fp);
    if (!valid || !seen) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    return 1;
}

static int check(int yes, unsigned int *e, unsigned int *p)
{
    ++*e;
    if (yes) ++*p;
    return yes;
}

int sparkles_mariadb_selftests(unsigned int *executed, unsigned int *passed,
                              int *negative_executed, int *negative_passed)
{
    unsigned int e = 0, p = 0;
    int seen, good, n = 0, np = 0;
    char pw[256];
    seen = 0;
    good = sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=secret\n",pw,sizeof pw,&seen);
    check(good && seen && !strcmp(pw,"secret"), &e,&p);
    seen = 0;
    good = sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=space word\n",pw,sizeof pw,&seen);
    check(good && !strcmp(pw,"space word"), &e,&p);
    seen = 0;
    check(sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=abc",pw,sizeof pw,&seen),&e,&p);
    check(seen && !strcmp(pw,"abc"),&e,&p);
    seen = 0;
    check(!sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=\n",pw,sizeof pw,&seen),&e,&p);
    check(!seen,&e,&p);
    check(!sparkles_mariadb_parse_line("SPARKLES_DB_USER=root\n",pw,sizeof pw,&seen),&e,&p);
    check(!sparkles_mariadb_parse_line("export SPARKLES_DB_PASSWORD=x\n",pw,sizeof pw,&seen),&e,&p);
    check(!sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=x\r\n",pw,sizeof pw,&seen),&e,&p);
    check(!sparkles_mariadb_parse_line(NULL,pw,sizeof pw,&seen),&e,&p);
    check(!sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=x",NULL,sizeof pw,&seen),&e,&p);
    check(!sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=x",pw,0,&seen),&e,&p);
    seen = 1;
    check(!sparkles_mariadb_parse_line("SPARKLES_DB_PASSWORD=x",pw,sizeof pw,&seen),&e,&p);
    n = 1;
    np = !sparkles_mariadb_load_config(NULL,NULL);
    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = n;
    if (negative_passed) *negative_passed = np;
    memset(pw,0,sizeof pw);
    return e == p && e >= 10U && n && np;
}
