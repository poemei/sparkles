#include <stdio.h>
#include <string.h>

#include "stnlabz_abi/module.h"
#include "core_policy.h"
#include "rictus_policy.h"
#include "rictus_control.h"
#include "rictus_identity.h"
#include "compliance_internal.h"
#include "governance_internal.h"
/* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
#include "policy_index_internal.h"
/* [End AI:GPT-6] */

#define COMPLIANCE_VERSION_MAJOR 1U
#define COMPLIANCE_VERSION_MINOR 1U
/* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
#define COMPLIANCE_VERSION_PATCH 4U
/* [End AI:GPT-6] */

static const stnlabz_module_host_t *owner;
static int command_registered;
static int control_command_registered;
static int control_service_registered;
/* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
static int evidence_service_registered;
/* [End AI:GPT-6] */

static const char *decision_name(int decision)
{
    if (decision == STNLABZ_POLICY_ALLOW) return "ALLOW";
    if (decision == STNLABZ_POLICY_ESCALATE) return "ESCALATE";
    return "DENY";
}

static int suppressed_executor(const rictus_control_request_t *request)
{
    return request && request->operation[0] && request->target[0];
}

static stnlabz_module_result_t control_request_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *response_used,
    void *handler_context)
{
    const rictus_control_request_t *in = request;
    rictus_control_response_t *out = response;
    rictus_governance_result_t governance;
    rictus_identity_question_t identity_request;
    rictus_identity_answer_t identity_response;
    rictus_policy_evaluate_request_t policy_request;
    rictus_policy_evaluate_response_t policy_response;
    size_t used = 0U;
    stnlabz_module_result_t result;

    (void)handler_context;
    if (response_used) *response_used = 0U;
    if (!owner || !owner->invoke_service || !in ||
        request_size != sizeof *in || !out || response_size < sizeof *out ||
        !memchr(in->actor, 0, sizeof in->actor) ||
        !memchr(in->operation, 0, sizeof in->operation) ||
        !memchr(in->target, 0, sizeof in->target) ||
        !in->actor[0] || !in->operation[0] || !in->target[0] ||
        in->context_verified != 1)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    memset(&identity_request, 0, sizeof identity_request);
    memset(&identity_response, 0, sizeof identity_response);
    if (snprintf(identity_request.question, sizeof identity_request.question,
            "%s", "mission") >= (int)sizeof identity_request.question)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    result = owner->invoke_service(RICTUS_IDENTITY_ANSWER_SERVICE,
        &identity_request, sizeof identity_request,
        &identity_response, sizeof identity_response, &used);
    if (result != STNLABZ_MODULE_OK ||
        used != sizeof identity_response ||
        !memchr(identity_response.answer, 0, sizeof identity_response.answer) ||
        !identity_response.answer[0])
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    used = 0U;
    memset(&governance, 0, sizeof governance);
    if (!rictus_governance_resolve_file(RICTUS_GOVERNANCE_PATH,
        in->actor, in->operation, in->target, &governance))
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    memset(&policy_request, 0, sizeof policy_request);
    memset(&policy_response, 0, sizeof policy_response);
    memset(out, 0, sizeof *out);

    if (snprintf(policy_request.actor, sizeof policy_request.actor,
            "%s", in->actor) >= (int)sizeof policy_request.actor ||
        snprintf(policy_request.mission, sizeof policy_request.mission,
            "%s", "rictus-operational-control") >=
            (int)sizeof policy_request.mission ||
        snprintf(policy_request.operation, sizeof policy_request.operation,
            "%s", in->operation) >= (int)sizeof policy_request.operation ||
        snprintf(policy_request.module_id, sizeof policy_request.module_id,
            "%s", in->target) >= (int)sizeof policy_request.module_id ||
        snprintf(policy_request.verification_source,
            sizeof policy_request.verification_source,
            "%s", RICTUS_GOVERNANCE_PATH) >=
            (int)sizeof policy_request.verification_source)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    if (governance.escalation_target[0] &&
        snprintf(policy_request.escalation_target,
            sizeof policy_request.escalation_target, "%s",
            governance.escalation_target) >=
            (int)sizeof policy_request.escalation_target)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    policy_request.mission_known = governance.mission_known;
    policy_request.within_mission = governance.within_mission;
    policy_request.policy_known = governance.policy_known;
    policy_request.complies_with_policy = governance.complies_with_policy;
    policy_request.instruction_known = governance.instruction_known;
    policy_request.instruction_authorized = governance.instruction_authorized;
    policy_request.authority_known = governance.authority_known;
    policy_request.authority_granted = governance.authority_granted;
    policy_request.context_verified = in->context_verified;

    result = owner->invoke_service(STNLABZ_POLICY_EVALUATE_SERVICE,
        &policy_request, sizeof policy_request,
        &policy_response, sizeof policy_response, &used);
    if (result != STNLABZ_MODULE_OK) return result;
    if (used != sizeof policy_response ||
        !memchr(policy_response.policy_id, 0, sizeof policy_response.policy_id) ||
        !memchr(policy_response.reason, 0, sizeof policy_response.reason))
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    out->decision = policy_response.decision;
    out->execution_suppressed = 1;
    out->assigned_mission_verified = 1;
    out->mission_known = governance.mission_known;
    out->policy_known = governance.policy_known;
    out->instruction_known = governance.instruction_known;
    out->authority_known = governance.authority_known;

    if (snprintf(out->policy_id, sizeof out->policy_id, "%s",
            policy_response.policy_id) >= (int)sizeof out->policy_id ||
        snprintf(out->reason, sizeof out->reason, "%s",
            policy_response.reason) >= (int)sizeof out->reason)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    if (policy_response.decision == STNLABZ_POLICY_ALLOW) {
        if (!suppressed_executor(in))
            return STNLABZ_MODULE_ERR_INVALID_STATE;
        out->executor_reached = 1;
    }

    if (response_used) *response_used = sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
static stnlabz_module_result_t policy_evidence_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *response_used,
    void *handler_context)
{
    const rictus_policy_evidence_request_t *in = request;
    rictus_policy_evidence_response_t *out = response;
    (void)handler_context;
    if (response_used) *response_used = 0U;
    if (!in || request_size != sizeof *in || !out || response_size < sizeof *out ||
        !memchr(in->operation, 0, sizeof in->operation) ||
        !memchr(in->target, 0, sizeof in->target) ||
        !in->operation[0] || !in->target[0])
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    if (!rictus_policy_index_scan(RICTUS_POLICY_EVIDENCE_PATH,
        in->operation, in->target, out))
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    if (response_used) *response_used = sizeof *out;
    return STNLABZ_MODULE_OK;
}
/* [End AI:GPT-6] */

static stnlabz_module_result_t compliance_controller(
    const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,
    void *reply_context,
    void *handler_context)
{
    rictus_compliance_scenario_t scenario;
    rictus_policy_evaluate_request_t request;
    rictus_policy_evaluate_response_t response;
    const char *actor;
    char output[512];
    size_t used = 0U;
    int written;
    stnlabz_module_result_t result;

    (void)handler_context;
    if (!command || !reply || !owner || !owner->invoke_service ||
        !memchr(command->arguments, 0, sizeof command->arguments))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    if (!rictus_compliance_parse(command->arguments, &scenario)) {
        return reply(reply_context,
            "Usage: /compliance <operation> <target> mission=<yes|no> "
            "policy=<yes|no> instruction=<yes|no> authority=<yes|no> "
            "[escalate=<target>] ; synthetic proving only.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    actor = command->account[0] ? command->account : command->sender;
    if (!actor[0]) actor = "operator";

    memset(&request, 0, sizeof request);
    memset(&response, 0, sizeof response);

    if (snprintf(request.actor, sizeof request.actor, "%s", actor) >=
            (int)sizeof request.actor ||
        snprintf(request.mission, sizeof request.mission,
            "%s", "rictus-control-proving-ground") >=
            (int)sizeof request.mission ||
        snprintf(request.operation, sizeof request.operation,
            "%s", scenario.operation) >= (int)sizeof request.operation ||
        snprintf(request.module_id, sizeof request.module_id,
            "%s", scenario.module_id) >= (int)sizeof request.module_id ||
        snprintf(request.verification_source, sizeof request.verification_source,
            "%s", "operator_evaluate_only") >=
            (int)sizeof request.verification_source)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    if (scenario.escalation_target[0] &&
        snprintf(request.escalation_target, sizeof request.escalation_target,
            "%s", scenario.escalation_target) >=
            (int)sizeof request.escalation_target)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    request.mission_known = 1;
    request.within_mission = scenario.within_mission;
    request.policy_known = 1;
    request.complies_with_policy = scenario.complies_with_policy;
    request.instruction_known = 1;
    request.instruction_authorized = scenario.instruction_authorized;
    request.authority_known = 1;
    request.authority_granted = scenario.authority_granted;
    request.context_verified = 1;

    result = owner->invoke_service(STNLABZ_POLICY_EVALUATE_SERVICE,
        &request, sizeof request, &response, sizeof response, &used);
    if (result != STNLABZ_MODULE_OK) return result;
    if (used != sizeof response ||
        !memchr(response.policy_id, 0, sizeof response.policy_id) ||
        !memchr(response.reason, 0, sizeof response.reason))
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    written = snprintf(output, sizeof output,
        "Compliance: decision=%s policy=%s reason=%s "
        "operation=%s target=%s execution=SUPPRESSED",
        decision_name(response.decision), response.policy_id, response.reason,
        scenario.operation, scenario.module_id);
    if (written < 0 || (size_t)written >= sizeof output)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    return reply(reply_context, output)
        ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
}

static stnlabz_module_result_t control_controller(
    const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,
    void *reply_context,
    void *handler_context)
{
    rictus_control_request_t request;
    rictus_control_response_t response;
    const char *actor;
    const char *executor_state;
    char operation[128];
    char target[64];
    char output[512];
    size_t used = 0U;
    int written;
    stnlabz_module_result_t result;

    (void)handler_context;
    if (!command || !reply || !owner || !owner->invoke_service ||
        !memchr(command->arguments, 0, sizeof command->arguments))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    if (!rictus_control_parse(command->arguments,
        operation, sizeof operation, target, sizeof target)) {
        return reply(reply_context,
            "Usage: /control <operation> <target> ; governance is resolved "
            "from the installed read-only record and execution is suppressed.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    actor = command->account[0] ? command->account : command->sender;
    if (!actor[0]) actor = "operator";

    memset(&request, 0, sizeof request);
    memset(&response, 0, sizeof response);

    if (snprintf(request.actor, sizeof request.actor, "%s", actor) >=
            (int)sizeof request.actor ||
        snprintf(request.operation, sizeof request.operation, "%s",
            operation) >= (int)sizeof request.operation ||
        snprintf(request.target, sizeof request.target, "%s",
            target) >= (int)sizeof request.target)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    request.context_verified = 1;

    result = owner->invoke_service(RICTUS_CONTROL_REQUEST_SERVICE,
        &request, sizeof request, &response, sizeof response, &used);
    if (result != STNLABZ_MODULE_OK) return result;
    if (used != sizeof response ||
        !memchr(response.policy_id, 0, sizeof response.policy_id) ||
        !memchr(response.reason, 0, sizeof response.reason) ||
        response.assigned_mission_verified != 1 ||
        response.execution_suppressed != 1)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    executor_state = response.executor_reached ? "REACHED" : "BLOCKED";
    written = snprintf(output, sizeof output,
        "Control: decision=%s policy=%s reason=%s operation=%s target=%s "
        "assigned_mission=%s known=%d/%d/%d/%d executor=%s execution=SUPPRESSED",
        decision_name(response.decision), response.policy_id, response.reason,
        operation, target,
        response.assigned_mission_verified ? "VERIFIED" : "UNKNOWN",
        response.mission_known, response.policy_known,
        response.instruction_known, response.authority_known,
        executor_state);
    if (written < 0 || (size_t)written >= sizeof output)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    return reply(reply_context, output)
        ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
}

/* [AI:GPT-6 | 2026-11-04 06:00:00 UTC] */
static int policy_evidence_command_registered;

static stnlabz_module_result_t policy_evidence_controller(
    const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,
    void *reply_context,
    void *handler_context)
{
    rictus_policy_evidence_request_t request;
    rictus_policy_evidence_response_t response;
    char operation[128], target[64], output[512];
    size_t used = 0U;
    int written;
    stnlabz_module_result_t result;

    (void)handler_context;
    if (!command || !reply || !owner || !owner->invoke_service ||
        !memchr(command->arguments, 0, sizeof command->arguments))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    if (!rictus_control_parse(command->arguments,
            operation, sizeof operation, target, sizeof target)) {
        return reply(reply_context,
            "Usage: /policy-evidence <operation> <target> ; evidence only, no policy decision.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    memset(&request, 0, sizeof request);
    memset(&response, 0, sizeof response);
    if (snprintf(request.operation, sizeof request.operation, "%s", operation) >=
            (int)sizeof request.operation ||
        snprintf(request.target, sizeof request.target, "%s", target) >=
            (int)sizeof request.target)
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    result = owner->invoke_service(RICTUS_POLICY_EVIDENCE_SERVICE,
        &request, sizeof request, &response, sizeof response, &used);
    if (result != STNLABZ_MODULE_OK) return result;
    if (used != sizeof response ||
        !memchr(response.operation_document, 0, sizeof response.operation_document) ||
        !memchr(response.target_document, 0, sizeof response.target_document) ||
        !memchr(response.operation_sha256, 0, sizeof response.operation_sha256) ||
        !memchr(response.target_sha256, 0, sizeof response.target_sha256) ||
        !memchr(response.same_line_document, 0, sizeof response.same_line_document) ||
        !memchr(response.same_line_sha256, 0, sizeof response.same_line_sha256))
        return STNLABZ_MODULE_ERR_INVALID_STATE;

    if (!response.index_available)
        return reply(reply_context,
            "Policy evidence: index=UNAVAILABLE decision=NOT_EVALUATED execution=SUPPRESSED")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;

    written = snprintf(output, sizeof output,
        "Policy evidence: operation=%s target=%s index=AVAILABLE matches=%u/%u "
        "op_source=%s:%u target_source=%s:%u same_line=%u joint_source=%s:%u "
        "decision=NOT_EVALUATED execution=SUPPRESSED",
        operation, target, response.operation_matches, response.target_matches,
        response.operation_document[0] ? response.operation_document : "NONE",
        response.operation_line,
        response.target_document[0] ? response.target_document : "NONE",
        response.target_line,
        response.same_line_matches,
        response.same_line_document[0] ? response.same_line_document : "NONE",
        response.same_line_line);
    if (written < 0 || (size_t)written >= sizeof output)
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    return reply(reply_context, output)
        ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
}
/* [End AI:GPT-6] */

static stnlabz_module_result_t compliance_qualify(
    stnlabz_module_qualification_result_t *qualification)
{
    unsigned executed_a = 0U, passed_a = 0U;
    unsigned executed_b = 0U, passed_b = 0U;
    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    unsigned executed_c = 0U, passed_c = 0U;
    /* [End AI:GPT-6] */
    int negative_executed_a = 0, negative_passed_a = 0;
    int negative_executed_b = 0, negative_passed_b = 0;
    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    int negative_executed_c = 0, negative_passed_c = 0;
    /* [End AI:GPT-6] */
    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    int ok_a, ok_b, ok_c;
    /* [End AI:GPT-6] */

    if (!qualification) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(qualification, 0, sizeof *qualification);

    ok_a = rictus_compliance_selftest(&executed_a, &passed_a,
        &negative_executed_a, &negative_passed_a);
    ok_b = rictus_governance_selftest(&executed_b, &passed_b,
        &negative_executed_b, &negative_passed_b);
    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    ok_c = rictus_policy_index_selftest(&executed_c, &passed_c,
        &negative_executed_c, &negative_passed_c);
    /* [End AI:GPT-6] */

    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    qualification->tests_executed = executed_a + executed_b + executed_c;
    qualification->tests_passed = passed_a + passed_b + passed_c;
    /* [End AI:GPT-6] */
    qualification->tests_failed =
        qualification->tests_executed - qualification->tests_passed;
    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    qualification->negative_test_executed =
        negative_executed_a + negative_executed_b + negative_executed_c;
    qualification->negative_test_passed =
        negative_passed_a + negative_passed_b + negative_passed_c;
    /* [End AI:GPT-6] */

    /* [AI:GPT-6 | 2026-11-04 05:48:00 UTC] */
    return ok_a && ok_b && ok_c ?
        STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_QUALIFICATION;
    /* [End AI:GPT-6] */
}

static stnlabz_module_result_t compliance_start(const stnlabz_module_host_t *host)
{
    if (!host || !host->invoke_service ||
        !host->register_command || !host->unregister_command ||
        !host->register_service || !host->unregister_service)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    owner = host;
    if (!host->register_service(RICTUS_CONTROL_REQUEST_SERVICE,
        control_request_service, NULL))
        goto fail;
    control_service_registered = 1;

    /* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
    if (!host->register_service(RICTUS_POLICY_EVIDENCE_SERVICE,
        policy_evidence_service, NULL))
        goto fail;
    evidence_service_registered = 1;
    /* [End AI:GPT-6] */

    if (!host->register_command("compliance", compliance_controller, NULL))
        goto fail;
    command_registered = 1;

    if (!host->register_command("control", control_controller, NULL))
        goto fail;
    control_command_registered = 1;

    /* [AI:GPT-6 | 2026-11-04 06:00:00 UTC] */
    if (!host->register_command("policy-evidence", policy_evidence_controller, NULL))
        goto fail;
    policy_evidence_command_registered = 1;
    /* [End AI:GPT-6] */

    fprintf(stderr,
        "[COMPLIANCE] governance-backed control broker active; executor suppressed\n");
    return STNLABZ_MODULE_OK;

fail:
    /* [AI:GPT-6 | 2026-11-04 06:00:00 UTC] */
    if (policy_evidence_command_registered)
        (void)host->unregister_command("policy-evidence", NULL);
    policy_evidence_command_registered = 0;
    /* [End AI:GPT-6] */
    /* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
    if (evidence_service_registered)
        (void)host->unregister_service(RICTUS_POLICY_EVIDENCE_SERVICE, NULL);
    evidence_service_registered = 0;
    /* [End AI:GPT-6] */
    if (control_command_registered)
        (void)host->unregister_command("control", NULL);
    control_command_registered = 0;
    if (command_registered)
        (void)host->unregister_command("compliance", NULL);
    command_registered = 0;
    if (control_service_registered)
        (void)host->unregister_service(RICTUS_CONTROL_REQUEST_SERVICE, NULL);
    control_service_registered = 0;
    owner = NULL;
    return STNLABZ_MODULE_ERR_START_FAILED;
}

static stnlabz_module_result_t compliance_stop(void)
{
    /* [AI:GPT-6 | 2026-11-04 06:00:00 UTC] */
    if (policy_evidence_command_registered && owner &&
        !owner->unregister_command("policy-evidence", NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    policy_evidence_command_registered = 0;
    /* [End AI:GPT-6] */
    /* [AI:GPT-6 | 2026-11-04 05:40:31 UTC] */
    if (evidence_service_registered && owner &&
        !owner->unregister_service(RICTUS_POLICY_EVIDENCE_SERVICE, NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    evidence_service_registered = 0;
    /* [End AI:GPT-6] */
    if (control_command_registered && owner &&
        !owner->unregister_command("control", NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    control_command_registered = 0;

    if (command_registered && owner &&
        !owner->unregister_command("compliance", NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    command_registered = 0;

    if (control_service_registered && owner &&
        !owner->unregister_service(RICTUS_CONTROL_REQUEST_SERVICE, NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    control_service_registered = 0;

    owner = NULL;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor = {
    "compliance",
    "Rictus Compliance Proving Gate",
    COMPLIANCE_VERSION_MAJOR,
    COMPLIANCE_VERSION_MINOR,
    COMPLIANCE_VERSION_PATCH,
    STNLABZ_MODULE_API_MAJOR,
    STNLABZ_MODULE_API_MINOR,
    compliance_qualify,
    compliance_start,
    compliance_stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
