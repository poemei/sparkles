#include <stdio.h>
#include "stnlabz_abi/module.h"

/* Module entrypoint is exported by web_module.c, not declared by the ABI header. */
extern const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void);

int main(void)
{
    stnlabz_module_qualification_result_t result = {0};
    const stnlabz_module_descriptor_t *module = stnlabz_module_get_descriptor();
    stnlabz_module_result_t status;
    if (!module || !module->qualify) return 1;
    status = module->qualify(&result);
    printf("Rictus Web: %u/%u passed, negative=%d/%d\n",
           result.tests_passed, result.tests_executed,
           result.negative_test_passed, result.negative_test_executed);
    return status == STNLABZ_MODULE_OK &&
           result.tests_executed >= STNLABZ_MODULE_MIN_TESTS &&
           result.tests_failed == 0U &&
           result.negative_test_executed && result.negative_test_passed
           ? 0 : 1;
}
