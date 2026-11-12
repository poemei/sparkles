#ifndef SPARKLES_CONTACTS_H
#define SPARKLES_CONTACTS_H
#include <stddef.h>
/* Verified transport principal. A name is a voluntary, unverified claim.
 * This service creates contact associations, NOT calendar authorization. */
#define SPARKLES_CONTACTS_SERVICE "sparkles.mariadb.contacts.identify"
typedef struct {
    char account[128];      /* native SL transport: sl:<lowercase UUID> */
    char avatar_name[128];  /* displayed Second Life agent name */
    char first_name[64];    /* optional self-reported preferred first name */
} sparkles_contact_request_t;
typedef struct {
    unsigned int found;
    unsigned int saved;
    char first_name[64];
} sparkles_contact_reply_t;
int sparkles_contact_name_parse(const char *message, char name[64]);
#endif
