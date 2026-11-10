#ifndef SPARKLES_IDENTITY_MODEL_H
#define SPARKLES_IDENTITY_MODEL_H
#include <stddef.h>
typedef struct {
 char name[80];
 char organization[80];
 char role[160];
 char mission[1024];
 char orders[3][256];
} identity_record_t;
int identity_load(const char *path, identity_record_t *out);
int identity_reply(const identity_record_t *record, const char *query, char *out, size_t cap);
int identity_selftest(unsigned *executed,unsigned *passed,int *negative_executed,int *negative_passed);
#endif
