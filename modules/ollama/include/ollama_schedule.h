#ifndef SPARKLES_OLLAMA_SCHEDULE_H
#define SPARKLES_OLLAMA_SCHEDULE_H
#include <stddef.h>

#define SPARKLES_SCHEDULE_ITEMS_MAX 2U
typedef struct {
    char date[11]; /* UNKNOWN or YYYY-MM-DD */
    char begin[6];
    char end[8]; /* UNKNOWN or HH:MM */
    char title[201];
} sparkles_schedule_item_t;
typedef struct {
    unsigned int count;
    char guest_name[64]; /* Explicitly named guest for one appointment. */
    unsigned int public_show; /* Explicit entertainment-show intent only. */
    char show_venue[151];
    char show_dj[101];
    sparkles_schedule_item_t items[SPARKLES_SCHEDULE_ITEMS_MAX];
} sparkles_schedule_plan_t;

/* Returns 1 on strict NONE or 1-2 well-formed EVENT records.
 * Returns 0 for any malformed or ambiguous output. */
int sparkles_schedule_parse(const char *output, sparkles_schedule_plan_t *plan);
int sparkles_schedule_date(const char *text, char date[11]);
int sparkles_schedule_find_date(const char *text, char date[11]);
int sparkles_schedule_end_time(const char *text,const char *start,char end[8]);
int sparkles_schedule_selftests(unsigned int *executed,unsigned int *passed);
#endif
