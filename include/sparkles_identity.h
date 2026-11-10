#ifndef RICTUS_IDENTITY_SERVICE_H
#define RICTUS_IDENTITY_SERVICE_H

#define RICTUS_IDENTITY_ANSWER_SERVICE "rictus.identity.answer"
#define RICTUS_IDENTITY_ANSWER_MAX 512

typedef struct {
    char question[512];
} rictus_identity_question_t;

typedef struct {
    char answer[RICTUS_IDENTITY_ANSWER_MAX];
} rictus_identity_answer_t;

#endif
