#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mariadb_internal.h"

static int auth_tests(unsigned int *e, unsigned int *p)
{
    char name[] = "/tmp/sparkles-mariadb-auth-XXXXXX";
    int fd, good;
    FILE *fp;
    unsigned int checks=0U, passed=0U;
#define CHECK(v) do { ++checks; if (v) ++passed; } while (0)
    CHECK(!sparkles_mariadb_account_allowed("/does/not/exist","PoeMei"));
    CHECK(!sparkles_mariadb_account_allowed(NULL,"PoeMei"));
    CHECK(!sparkles_mariadb_account_allowed("/does/not/exist",""));
    fd=mkstemp(name);
    if (fd<0) { *e=checks+1U;*p=passed;return 0; }
    fp=fdopen(fd,"w");
    if (!fp) { close(fd);unlink(name);*e=checks+1U;*p=passed;return 0; }
    good=fputs("PoeMei\n",fp)>=0 && fflush(fp)==0;
    if (!good) { fclose(fp);unlink(name);*e=checks+1U;*p=passed;return 0; }
    CHECK(sparkles_mariadb_account_allowed(name,"PoeMei"));
    CHECK(sparkles_mariadb_account_allowed(name,"poemei"));
    CHECK(!sparkles_mariadb_account_allowed(name,"other"));
    CHECK(!sparkles_mariadb_account_allowed(name,"PoeMei;admin"));
    CHECK(!sparkles_mariadb_account_allowed(name,"*"));
    CHECK(!sparkles_mariadb_account_allowed(name,NULL));
    fclose(fp);
    unlink(name);
#undef CHECK
    *e=checks;
    *p=passed;
    return checks==passed;
}

int main(void)
{
    unsigned int executed=0U,passed=0U;
    unsigned int date_executed=0U,date_passed=0U;
    unsigned int auth_executed=0U,auth_passed=0U;
    unsigned int appointment_executed=0U,appointment_passed=0U;
    unsigned int memory_executed=0U,memory_passed=0U;
    int negative_executed=0,negative_passed=0;
    int parser_ok=sparkles_mariadb_selftests(
        &executed,&passed,&negative_executed,&negative_passed);
    int dates_ok=sparkles_mariadb_date_selftests(&date_executed,&date_passed);
    int access_ok=auth_tests(&auth_executed,&auth_passed);
    int appointment_ok=sparkles_mariadb_appointment_selftests(
        &appointment_executed,&appointment_passed);
    int memory_ok=sparkles_mariadb_memory_selftests(
        &memory_executed,&memory_passed);
    executed+=date_executed+auth_executed+appointment_executed+memory_executed;
    passed+=date_passed+auth_passed+appointment_passed+memory_passed;
    printf("mariadb tests: executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d\n",
        executed,passed,executed-passed,negative_executed,negative_passed);
    return parser_ok && dates_ok && access_ok && appointment_ok && memory_ok ? 0 : 1;
}
