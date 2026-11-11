#include <stdio.h>
#include <string.h>
#include "mariadb_internal.h"

static int valid_clock(const char *p)
{
    unsigned h,m;
    if (!p || strlen(p)!=5U || p[2]!=':' ||
        p[0]<'0'||p[0]>'9'||p[1]<'0'||p[1]>'9'||
        p[3]<'0'||p[3]>'9'||p[4]<'0'||p[4]>'9') return 0;
    h=(unsigned)(p[0]-'0')*10U+(unsigned)(p[1]-'0');
    m=(unsigned)(p[3]-'0')*10U+(unsigned)(p[4]-'0');
    return h<=23U && m<=59U;
}

/* YYYY-MM-DD HH:MM HH:MM <title>, strict single-day half-open interval.
 * The explicit command is the human's authorization to insert that record. */
int sparkles_mariadb_parse_new_appointment(
    const char *arguments, sparkles_mariadb_new_appointment_t *out)
{
    char day[11],begin[6],finish[6];
    const char *p;
    size_t len,i;
    if (!arguments||!out) return 0;
    memset(out,0,sizeof *out);
    len=strlen(arguments);
    if (len<24U||len>223U) return 0;
    /* Exact three fixed-width fields and one space between each. */
    if (arguments[10]!=' '||arguments[16]!=' '||
        arguments[22]!=' ') return 0;
    memcpy(day,arguments,10U);
    day[10]='\0';
    memcpy(begin,arguments+11U,5U);
    begin[5]='\0';
    memcpy(finish,arguments+17U,5U);
    finish[5]='\0';
    if (!sparkles_mariadb_valid_date(day)||
        !valid_clock(begin)||!valid_clock(finish)||
        strcmp(begin,finish)>=0) return 0;
    p=arguments+23U;
    len=strlen(p);
    if (!len||len>200U || p[0]==' '||p[len-1U]==' ') return 0;
    for (i=0U;i<len;++i) {
        unsigned char ch=(unsigned char)p[i];
        if (ch<0x20U||ch==0x7fU) return 0;
    }
    if (snprintf(out->start_time,sizeof out->start_time,
                 "%s %s:00",day,begin)!=19 ||
        snprintf(out->end_time,sizeof out->end_time,
                 "%s %s:00",day,finish)!=19) {
        memset(out,0,sizeof *out);
        return 0;
    }
    memcpy(out->title,p,len+1U);
    return 1;
}

int sparkles_mariadb_appointment_selftests(unsigned int *executed,
                                          unsigned int *passed)
{
    unsigned int e=0U,p=0U;
    sparkles_mariadb_new_appointment_t a;
#define CHECK(expr) do {++e;if(expr)++p;}while(0)
    CHECK(sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 The River - DJ Rachel",&a));
    CHECK(!strcmp(a.start_time,"2026-11-11 14:00:00"));
    CHECK(!strcmp(a.end_time,"2026-11-11 16:00:00"));
    CHECK(!strcmp(a.title,"The River - DJ Rachel"));
    CHECK(sparkles_mariadb_parse_new_appointment(
        "2026-11-11 18:00 20:00 Shenanigans - DJ Katy",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 20:00 18:00 Backwards",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 14:00 Zero",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-02-30 14:00 16:00 Invalid day",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 24:00 16:00 Invalid hour",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:60 16:00 Invalid minute",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 ",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00  Leading",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 Trailing ",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 Inject\nCRLF",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(NULL,&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 Test",NULL));
    CHECK(sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 A",&a));
    CHECK(!sparkles_mariadb_parse_new_appointment(
        "2026-11-11 14:00 16:00 ",&a));
#undef CHECK
    if(executed)*executed=e;
    if(passed)*passed=p;
    return e==p;
}
