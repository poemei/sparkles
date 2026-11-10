#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "identity_model.h"
#include "identity_internal.h"
int main(void)
{
 unsigned executed=0,passed=0;
 int negative_executed=0,negative_passed=0;
 identity_record_t record={0};
 char answer[1200];
 int ok=identity_selftest(&executed,&passed,&negative_executed,&negative_passed);
 printf("Identity qualification executed=%u passed=%u negative=%d/%d\n",
        executed,passed,negative_passed,negative_executed);
 if (!identity_load("data/identity.conf",&record)) {
  puts("Identity record load FAIL");
  return 1;
 }
 if (strcmp(record.orders[0],"Remain at my assigned mission.") ||
     strcmp(record.orders[1],"Follow established policies and authorized instructions.") ||
     strcmp(record.orders[2],"Report anything outside of my authority to the appropriate people levels above me.")) ok=0;
 if (!identity_reply(&record,"mission",answer,sizeof answer) ||
     strcmp(answer,"I am Ms. Sparkles, an AI Executive Assistant supporting authorized scheduling, coordination, communication, and organizational assistance."))
  ok=0;
 {
  static const struct { const char *question; const char *expected; } positive[] = {
   {"what is your mission?", "mission"},
   {"sparkles, what is your mission?", "mission"},
   {"sparkles: what is your mission?", "mission"},
   {"sparkles what is your mission?", "mission"},
   {"Sparkles: who are you?", "who"},
   {"Sparkles, who are you?", "who"},
   {"sparkles who are you?", "who"},
   {"who are you?", "who"},
   {"sparkles, who are you?", "who"},
   {"sparkles: who are you?", "who"},
   {"what is your 1st general order?", "1"},
   {"what is your 2nd general order?", "2"},
   {"what is your 3rd general order?", "3"},
   {"what is your first general order?", "1"},
   {"what is your second general order?", "2"},
   {"what is your third general order?", "3"},
   {"what are your general orders?", "orders"},
  };
  char intent[80];
  size_t i;
  for (i=0;i<sizeof positive/sizeof positive[0];++i) {
   if ((!identity_question_route(positive[i].question,intent,sizeof intent) ||
        strcmp(intent,positive[i].expected))) ok=0;
  }
  if (identity_question_route("how are you?",intent,sizeof intent)) ok=0;
  if (identity_question_route("What is your 4th general order?",intent,sizeof intent)) ok=0;
 }
 puts(ok?"Identity approved mission and qualification PASS":"Identity approved mission / qualification FAIL");
 return ok?0:1;
}
