#ifndef SPARKLES_OLLAMA_INTERNAL_H
#define SPARKLES_OLLAMA_INTERNAL_H

#include <stddef.h>

#define SPARKLES_OLLAMA_ENDPOINT "http://127.0.0.1:11434/api/generate"
#define SPARKLES_OLLAMA_MODEL "sparkles-executive:v0.3.1"
#define SPARKLES_OLLAMA_HTTP_MAX (256U * 1024U)

int sparkles_ollama_build_request(const char *model, const char *prompt,
                                char *output, size_t output_size);
int sparkles_ollama_extract_response(const char *json,
                                   char *output, size_t output_size);
int sparkles_ollama_run_selftests(unsigned int *executed,
                                unsigned int *passed,
                                int *negative_executed,
                                int *negative_passed);

#endif
