#include <stdio.h>

#include "compliance_internal.h"
#include "governance_internal.h"

int main(void)
{
    unsigned compliance_executed = 0U, compliance_passed = 0U;
    unsigned governance_executed = 0U, governance_passed = 0U;
    int compliance_negative_executed = 0, compliance_negative_passed = 0;
    int governance_negative_executed = 0, governance_negative_passed = 0;
    int compliance_ok;
    int governance_ok;

    compliance_ok = rictus_compliance_selftest(
        &compliance_executed, &compliance_passed,
        &compliance_negative_executed, &compliance_negative_passed);
    governance_ok = rictus_governance_selftest(
        &governance_executed, &governance_passed,
        &governance_negative_executed, &governance_negative_passed);

    printf("Compliance qualification executed=%u passed=%u negative=%d/%d\n",
        compliance_executed + governance_executed,
        compliance_passed + governance_passed,
        compliance_negative_passed + governance_negative_passed,
        compliance_negative_executed + governance_negative_executed);

    puts(compliance_ok && governance_ok ?
        "Compliance governance PASS" : "Compliance governance FAIL");
    return compliance_ok && governance_ok ? 0 : 1;
}
