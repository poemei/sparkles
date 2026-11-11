#include <stdio.h>
#include "mariadb_internal.h"

int main(void)
{
    unsigned int executed = 0U, passed = 0U;
    int negative_executed = 0, negative_passed = 0;
    int result = sparkles_mariadb_selftests(
        &executed, &passed, &negative_executed, &negative_passed);
    unsigned int date_executed = 0U, date_passed = 0U;\n    int dates_ok = sparkles_mariadb_date_selftests(&date_executed, &date_passed);\n    executed += date_executed;\n    passed += date_passed;\n    printf("mariadb tests: executed=%u passed=%u failed=%u negative_executed=%d negative_passed=%d\n",
           executed, passed, executed - passed,
           negative_executed, negative_passed);
    return result && dates_ok ? 0 : 1;
}
