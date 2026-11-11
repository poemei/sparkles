#include <stdio.h>
#include "mariadb_internal.h"

int main(void)
{
    unsigned int executed = 0U, passed = 0U;
    int negative_executed = 0, negative_passed = 0;
    int result = sparkles_mariadb_selftests(
        &executed, &passed, &negative_executed, &negative_passed);
    printf("mariadb tests: executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d\n",
           executed, passed, executed - passed,
           negative_executed, negative_passed);
    return result ? 0 : 1;
}
