#ifndef SPARKLES_LLM_H
#define SPARKLES_LLM_H

#define SPARKLES_LLM_GENERATE_SERVICE "sparkles.llm.generate"

#define SPARKLES_LLM_PROMPT_MAX 2048
#define SPARKLES_LLM_RESPONSE_MAX 512

typedef struct {
    char prompt[SPARKLES_LLM_PROMPT_MAX];
} sparkles_llm_request_t;

typedef struct {
    char text[SPARKLES_LLM_RESPONSE_MAX];
} sparkles_llm_response_t;

#endif
