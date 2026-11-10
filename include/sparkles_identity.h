#ifndef SPARKLES_IDENTITY_SERVICE_H
#define SPARKLES_IDENTITY_SERVICE_H

#define SPARKLES_IDENTITY_ANSWER_SERVICE "sparkles.identity.answer"
#define SPARKLES_IDENTITY_ANSWER_MAX 512

typedef struct {
    char question[512];
} sparkles_identity_question_t;

typedef struct {
    char answer[SPARKLES_IDENTITY_ANSWER_MAX];
} sparkles_identity_answer_t;

#endif
