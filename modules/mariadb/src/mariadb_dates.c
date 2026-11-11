#include <string.h>
#include "mariadb_internal.h"

static int leap(unsigned y)
{
    return (y % 4U == 0U) && (y % 100U != 0U || y % 400U == 0U);
}

int sparkles_mariadb_valid_date(const char *date)
{
    unsigned y,m,d;
    unsigned ndays;
    size_t i;
    static const unsigned month_days[12] = {
        31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U
    };
    if (!date || strlen(date) != 10U ||
        date[4] != '-' || date[7] != '-') return 0;
    for (i = 0; i < 10U; ++i) {
        if (i == 4U || i == 7U) continue;
        if (date[i] < '0' || date[i] > '9') return 0;
    }
    y = (unsigned)(date[0]-'0')*1000U+(unsigned)(date[1]-'0')*100U+
        (unsigned)(date[2]-'0')*10U+(unsigned)(date[3]-'0');
    m = (unsigned)(date[5]-'0')*10U+(unsigned)(date[6]-'0');
    d = (unsigned)(date[8]-'0')*10U+(unsigned)(date[9]-'0');
    if (y < 2000U || m < 1U || m > 12U || d < 1U) return 0;
    ndays = month_days[m-1U] + (m == 2U && leap(y) ? 1U : 0U);
    return d <= ndays;
}

int sparkles_mariadb_valid_range(const char *from, const char *until)
{
    return sparkles_mariadb_valid_date(from) &&
           sparkles_mariadb_valid_date(until) && strcmp(from, until) < 0;
}

int sparkles_mariadb_date_selftests(unsigned int *executed,
                                  unsigned int *passed)
{
    unsigned int e=0U,p=0U;
#define CHECK(x) do { ++e; if(x) ++p; } while(0)
    CHECK(sparkles_mariadb_valid_date("2026-11-11"));
    CHECK(sparkles_mariadb_valid_date("2024-02-29"));
    CHECK(!sparkles_mariadb_valid_date("2026-02-29"));
    CHECK(!sparkles_mariadb_valid_date("2026-13-01"));
    CHECK(!sparkles_mariadb_valid_date("2026-00-01"));
    CHECK(!sparkles_mariadb_valid_date("2026-01-32"));
    CHECK(!sparkles_mariadb_valid_date("2026-1-11"));
    CHECK(!sparkles_mariadb_valid_date("2026-11-11x"));
    CHECK(!sparkles_mariadb_valid_date(NULL));
    CHECK(sparkles_mariadb_valid_range("2026-11-11","2026-11-12"));
    CHECK(!sparkles_mariadb_valid_range("2026-11-11","2026-11-11"));
    CHECK(!sparkles_mariadb_valid_range("2026-11-12","2026-11-11"));
#undef CHECK
    if (executed) *executed=e;
    if (passed) *passed=p;
    return e==p;
}
