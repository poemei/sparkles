#ifndef SPARKLES_CORE_DIAGNOSTICS_H
#define SPARKLES_CORE_DIAGNOSTICS_H

#include <stddef.h>
#include "stnlabz_abi/module.h"

#define SPARKLES_DIAG_TEXT_MAX 2048
#define SPARKLES_DIAG_SERVICE_MAX 16
#define SPARKLES_DIAG_TRACE_MAX 16

void sparkles_diag_begin(const char *sender, const char *account, const char *text);
void sparkles_diag_route(const char *command);
void sparkles_diag_command(const char *command, const char *owner,
                         stnlabz_module_result_t result, int replied);
void sparkles_diag_service(const char *service, const char *owner,
                         stnlabz_module_result_t result);
int sparkles_diag_render(const char *sender, const char *account,
                       char *output, size_t output_size);

#endif
