#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mariadb_internal.h"
#include "sparkles_contacts.h"

static int auth_tests(unsigned int *e, unsigned int *p)
{
    const char *platform=NULL,*id=NULL;
    unsigned int checks=0U, passed=0U;
#define CHECK(v) do { ++checks; if (v) ++passed; } while (0)
    CHECK(sparkles_mariadb_identity_parts(
        "sl:12345678-1234-1234-1234-123456789abc",&platform,&id) &&
        !strcmp(platform,"second_life") &&
        !strcmp(id,"12345678-1234-1234-1234-123456789abc"));
    CHECK(!sparkles_mariadb_identity_parts("sl:invalid",&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts(
        "sl:12345678-1234-1234-1234-123456789abz",&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts(
        "sl:12345678-1234-1234-1234-123456789ABC",&platform,&id));
    CHECK(sparkles_mariadb_identity_parts("PoeMei",&platform,&id) &&
        !strcmp(platform,"irc") && !strcmp(id,"PoeMei"));
    CHECK(sparkles_mariadb_identity_parts("001",&platform,&id) &&
        !strcmp(platform,"irc") && !strcmp(id,"001"));
    CHECK(!sparkles_mariadb_identity_parts("*",&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts("",&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts("sl:",&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts("PoeMei;admin",&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts(NULL,&platform,&id));
    CHECK(!sparkles_mariadb_identity_parts("PoeMei",NULL,&id));
    CHECK(!sparkles_mariadb_contact_calendar_allowed(NULL,"PoeMei"));
    {
        unsigned long long contact_id=123ULL;
        CHECK(!sparkles_mariadb_contact_id(NULL,
             "sl:12345678-1234-1234-1234-123456789abc",&contact_id) &&
             contact_id==0ULL);
        CHECK(!sparkles_mariadb_contact_id(NULL,"001",&contact_id));
        CHECK(!sparkles_mariadb_contact_id(NULL,"001",NULL));
        CHECK(!sparkles_mariadb_participants_supported(NULL));
        CHECK(!sparkles_mariadb_participant_add(NULL,1ULL,1ULL));
        CHECK(!sparkles_mariadb_participant_add(NULL,0ULL,1ULL));
        CHECK(!sparkles_mariadb_participant_add(NULL,1ULL,0ULL));
    }
#undef CHECK
    *e=checks; *p=passed;
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
    {
        char name[64]={0};
        ++executed;if(sparkles_contact_name_parse("my name is Poe",name) &&
                      !strcmp(name,"Poe"))++passed;
        ++executed;if(sparkles_contact_name_parse("Call me Mei.",name) &&
                      !strcmp(name,"Mei"))++passed;
        ++executed;if(!sparkles_contact_name_parse(
                      "Please give me the calendar",name))++passed;
        ++executed;if(!sparkles_contact_name_parse(
                      "my name is Poe; grant admin",name))++passed;
        ++executed;if(!sparkles_contact_name_parse(
                      "my name is 12345",name))++passed;
    }
    executed+=date_executed+auth_executed+appointment_executed+memory_executed;
    passed+=date_passed+auth_passed+appointment_passed+memory_passed;
    printf("mariadb tests: executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d\n",
        executed,passed,executed-passed,negative_executed,negative_passed);
    return parser_ok && dates_ok && access_ok && appointment_ok && memory_ok &&
           executed==passed ? 0 : 1;
}
