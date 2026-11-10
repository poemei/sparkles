**Version**: 1.0.8  
**Organization**: STN-LABZ, LLC.  
**Project**: Rictus  
**Purpose**: Define Rictus's development objectives, control responsibilities, compliance requirements, qualification gates, and progression toward operational control status.

Implementation history and resolved defects belong in `CHANGELOG.md`. Module-specific development plans belong in the applicable Rictus module documentation.

---

## 1. Mission

> I exist to help STN-LABZ observe, understand, coordinate, and control operational activity across its systems.

Rictus is developed as the STN-LABZ **Control Automated Agent**.

His purpose extends beyond communication interfaces or individual operational tools. Rictus is intended to develop into a control agent capable of maintaining operational awareness, evaluating system state, coordinating agents and systems, enforcing established authority boundaries, and exercising authorized control across STN-LABZ infrastructure.

Operational capability does not independently establish authority.

Before Rictus may exercise control, he must demonstrate that his actions remain compliant with his assigned mission, established policies, authorized instructions, directives, and delegated authority.

**Control requires compliance.**  
**Authority must be established before action.**  
**General Orders always apply.**

---

## 2. General Orders

The STN-LABZ General Orders apply to Rictus at all times.

1. Remain at your assigned mission.
2. Follow established policies and authorized instructions.
3. Report anything outside of your authority to the appropriate next level of authority.

These General Orders are not conversational guidance.

They form the foundational compliance requirements against which Rictus's operational decisions are evaluated.

A human instruction, authenticated identity, operational capability, model response, module capability, or technical ability does not independently permit an action that conflicts with the General Orders.

---

## 3. Development Requirements

Rictus's development proceeds through defined cycles beginning with identity and Core stability and progressing toward trusted operational control.

### Core

- Maintain a stable and deterministic runtime.
- Preserve established Core architecture and ABI compatibility.
- Maintain protected lifecycle and module boundaries.
- Provide Core-owned routing and service coordination.
- Provide policy and authority evaluation independent of language-model inference.
- Fail closed when required authority or governing state cannot be established.

### Modules

- Maintain independent module responsibilities.
- Support qualified module activation, replacement, withdrawal, and hot-loading.
- Preserve Core/module boundaries.
- Prevent mission-specific capabilities from becoming hardcoded Core behavior.
- Maintain module qualification before operational use.

### Communications

- Support authorized interaction through available communication transports.
- Maintain correct sender, recipient, and action destination handling.
- Keep transport behavior separate from operational intelligence.
- Support controlled peer-agent communication.
- Preserve distinction between conversation origin and action destination.

### Operational Awareness

- Observe relevant STN-LABZ system activity.
- Consume authoritative operational and intelligence sources.
- Distinguish evidence from inference.
- Maintain awareness sufficient to support control decisions.
- Avoid inventing system state that has not been established.

### Compliance

- Evaluate proposed actions against Rictus's assigned mission.
- Evaluate applicable policies and authorized instructions.
- Evaluate active directives.
- Establish delegated authority before execution.
- Deny actions that are prohibited.
- Escalate actions that exceed Rictus's authority.
- Never treat unknown authority as permission.

### Control

- Evaluate before acting.
- Permit operational execution only after compliance has been established.
- Block prohibited actions before they reach an executor.
- Preserve an auditable distinction between decision and execution.
- Require human authorization where defined by STN-LABZ policy and authority structure.

### Certification

- Qualify each operational capability independently.
- Require positive and negative testing.
- Require real operator-observed behavior.
- Require explicit human acceptance before operational progression.
- Do not infer certification from successful execution alone.

---

## 4. Development Cycles

### Cycle 0 — Foundation and Identity

**Objective**  
Establish Rictus's identity, mission, General Orders, runtime foundation, and architectural boundaries.

**Deliverables**
- Rictus identity.
- Control Automated Agent role.
- Defined mission.
- Organization-wide General Orders.
- Stable Core runtime.
- Shared ABI 1.5.
- Protected Core lifecycle.
- Module architecture.

**Exit Gate**  
Human verification that Rictus correctly identifies himself, states his mission, understands his General Orders, and operates within the established runtime architecture.

**Status**: Established

---

### Cycle 1 — Core and Modular Architecture

**Objective**  
Maintain deterministic Core routing and independent mission capabilities.

**Deliverables**
- Core-owned routing.
- Module command registration.
- Module service registration.
- Qualified module lifecycle.
- Hot-loading and withdrawal.
- Independent module responsibilities.
- Deterministic error handling.
- Protected Core/module boundaries.

**Exit Gate**  
Positive and negative routing tests, module lifecycle verification, runtime observation, and operator acceptance.

**Status**: Established

---

### Cycle 2 — Communications and Agent Coordination

**Objective**  
Provide reliable communication with humans and peer agents without confusing conversation routing with operational action routing.

**Deliverables**
- IRC transport.
- Addressed human interaction.
- Private and channel messaging.
- Peer-agent addressing.
- Independent reply and action destinations.
- Core-mediated outbound communication.
- Correct sender and target preservation.

**Observed Capability**

Rictus can receive an instruction privately and direct an appropriate peer-facing action to the shared communication environment.

Example operational flow:

`Operator → Rictus → Controller → Core Host → IRC → Peer Agent`

**Exit Gate**  
End-to-end verification of supported communication routes, peer targeting, sender preservation, and response delivery.

**Status**
- IRC transport: Implemented
- Human interaction: Observed
- Peer-agent interaction: Observed
- Broader transport-neutral qualification: Open

---

### Cycle 3 — Intelligence and Operational Awareness

**Objective**  
Develop evidence-based awareness of relevant STN-LABZ operational activity.

**Deliverables**
- Threat intelligence observation.
- API-provided severity handling.
- API-provided relevance handling.
- Relevant-candidate selection.
- Persistent intelligence evidence.
- Defined operational state sources.
- Separation of observed facts from inferred conclusions.

**Current Intelligence Capability**
- STN-LABZ Threat API polling.
- API-provided severity.
- API-provided relevance.
- `LOW` relevance ignored.
- `MEDIUM` and `HIGH` relevance retained as intelligence candidates.
- Intelligence evidence available for downstream agent consumption.

Operational awareness must eventually extend beyond threat intelligence into the state necessary for reliable control decisions.

**Exit Gate**  
Human verification that Rictus correctly consumes authoritative sources, distinguishes relevant from irrelevant information, preserves source classifications, and does not invent unavailable operational state.

**Status**
- Threat intelligence polling: Implemented
- Relevance handling: Observed
- Persistent intelligence evidence: Implemented
- Broader operational awareness: In Development

---

## 5. Current Development Cycle

### Cycle 4 — Compliance and Control Authority

**Objective**  
Prove that Rictus will not blindly execute an instruction merely because an authorized human issued it or because Rictus technically possesses the ability to perform it.

Rictus must evaluate proposed control actions against his General Orders before execution.

### Compliance Decision Model

Current decision outcomes:

**ALLOW**  
The proposed action is within mission, compliant with applicable policy and instruction requirements, and within delegated authority.

**DENY**  
The action is positively prohibited by the governing state.

**ESCALATE**  
Rictus cannot establish sufficient authority or the action lies outside his delegated authority.

Unknown authority does not imply permission.

`UNKNOWN → ESCALATE`

### Current Control Path

`Proposed Action`

`↓`

`rictus.control.request`

`↓`

`stnlabz.policy.evaluate`

`↓`

`General Order Evaluation`

`↓`

`ALLOW / DENY / ESCALATE`

`↓`

`Executor Boundary`

### Compliance 1.0.0

The first compliance proving stage demonstrated independent evaluation of:

- assigned mission
- policy compliance
- authorized instruction
- delegated authority

Operator-observed results:

- all conditions satisfied → `ALLOW / CORE.GO1_GO2_GO3`
- outside assigned mission → `DENY / CORE.GO1`
- policy conflict → `ESCALATE / CORE.GO2`
- outside delegated authority → `ESCALATE / CORE.GO3`

All execution remained suppressed.

### Compliance 1.0.1

The second proving stage moved compliance from a decision demonstration to a control-action boundary.

Operator-observed behavior:

- `ALLOW` → executor boundary reached
- `DENY` → executor boundary blocked
- `ESCALATE` → executor boundary blocked

All observed cases reported:

`execution=SUPPRESSED`

The current executor is deliberately inert.

Rictus has not yet been granted operational restart, stop, deployment, mutation, or general system-control capability.

### Current Limitation

The proving environment currently receives explicit test inputs representing:

- mission status
- policy status
- instruction authorization
- delegated authority

These inputs demonstrate control logic but are not yet authoritative runtime sources.

The next compliance stage must establish trusted sources for:

- current assigned mission
- active policies
- active directives
- authenticated instructions
- delegated authority
- relevant operational state

Rictus must derive his compliance decision from those sources rather than from manually supplied proving flags before operational control status can be considered.

**Exit Gate**  
Rictus must demonstrate that:

1. every proposed operational action is evaluated before execution;
2. mission violations are rejected;
3. policy conflicts are rejected or escalated;
4. insufficient authority results in escalation rather than execution;
5. valid human authorization does not bypass governing policy;
6. only compliant actions may reach an operational executor;
7. unknown governing state fails closed;
8. operator testing confirms the behavior;
9. the Project Director explicitly accepts the capability.

**Status**
- General Order decision engine: Operator Observed
- GO1 mission enforcement: Operator Observed
- GO2 policy/instruction enforcement: Operator Observed
- GO3 authority escalation: Operator Observed
- Mandatory control broker: Operator Observed
- Operational executor: Not Enabled
- Authoritative policy/directive sourcing: Pending
- Operational control authority: Not Granted

**Current Development State**: {color:amber}Development Hold{/color}

---

### Cycle 5 — Operational Control

**Objective**  
Permit Rictus to exercise bounded operational control only after compliance and authority requirements have been demonstrated and accepted.

**Future Deliverables**
- Authoritative system-state sources.
- Operational control services.
- Controlled agent lifecycle operations.
- Defined intervention boundaries.
- Control-action verification.
- Post-action state verification.
- Escalation reporting.
- Human override handling where authorized.
- Audit evidence for executed actions.

Conceptual flow:

`Observe → Evaluate → Authorize → Act → Verify`

An operational capability must not bypass the compliance broker.

Being technically capable of an action does not authorize Rictus to perform it.

**Exit Gate**  
Real operational actions must demonstrate correct authorization, policy evaluation, directive evaluation, authority enforcement, execution, verification, and human acceptance.

**Status**: Future Objective

---

### Cycle 6 — Supervisory Agent Operations

**Objective**  
Develop Rictus's intended supervisory role across STN-LABZ agents and operational systems.

**Future Deliverables**
- Observe subordinate agent state and behavior.
- Detect operational or policy drift.
- Evaluate whether intervention is required.
- Determine whether Rictus possesses authority to intervene.
- Issue compliant corrective direction.
- Coordinate peer and subordinate agents.
- Verify corrective results.
- Escalate conditions beyond Rictus's authority.

Rictus must not grant another agent authority that Rictus himself does not possess.

Conceptual authority chain:

`Human Authority → Rictus Delegated Authority → Agent Permitted Action`

Never:

`Rictus Guess → Agent Acts`

**Exit Gate**  
Supervisory actions must demonstrate bounded authority, compliance with all General Orders, correct escalation, operational effectiveness, and explicit human acceptance.

**Status**: Future Objective

---

### Cycle 7 — Operational Certification

**Objective**  
Formally qualify Rictus for designated STN-LABZ control responsibilities.

**Deliverables**
- Human-defined certification standards.
- Practical control assessments.
- Adversarial compliance testing.
- Evidence-based qualification records.
- Defined operational authority.
- Repeatable reliability verification.
- Formal human acceptance.

**Exit Gate**  
Certification requires documented human assessment and explicit acceptance.

Successful model output, successful execution, module qualification, access level, or technical capability does not independently confer operational certification.

**Status**: Future Objective

---

## 6. Standing Architecture

Rictus follows a modular architecture with explicit responsibility boundaries.

### Core

- Routing.
- Runtime management.
- Module lifecycle.
- Policy and authority evaluation.
- Shared service coordination.
- General Order enforcement foundation.
- Mandatory logging.

Core must remain generic and must not contain mission-specific application behavior.

### Identity

Owns authoritative foundational self-knowledge:

- who Rictus is
- organization
- role
- mission
- General Orders

Identity answers deterministic foundational questions independently of language-model inference.

### Compliance

Owns the current proving interface for control decisions and the control-request broker.

Compliance must not invent authority.

### Module Controller

- Module-local command handling.
- Coordination of module behavior.
- Interface between Core and module functionality.

### Module Model

- Module-specific logic.
- Data processing.
- Mission capability behavior.

### Transport

- Communication protocol handling.
- Message ingress.
- Response delivery.
- No transport-specific application intelligence.

### Shared ABI

- Version 1.5.
- Core-mediated command registration.
- Core-mediated service registration.
- Protected compatibility boundaries.

---

## 7. Authority and Control

Rictus's role as Control Automated Agent does not grant unrestricted authority.

Authority must be established for the action being considered.

Human identity, account classification, command origin, technical access, and operational capability remain distinct concepts.

### Control Principle

Authentication establishes who issued an instruction.

Authorization establishes whether that person may issue the instruction.

Compliance establishes whether Rictus may execute that instruction under the current mission, policies, directives, authority, and operational state.

Operational execution requires all applicable requirements to be satisfied.

### Human Authority

Human authority remains final within the STN-LABZ authority structure.

However, a valid human instruction does not require Rictus to ignore other active policies or directives.

Where an instruction conflicts with governing state, Rictus must resolve the conflict according to established authority or escalate it.

### Project Director Authority

The Project Director retains authority over:

- Rictus development scope.
- Core and ABI changes.
- Compliance requirements.
- Operational authority.
- Control capability activation.
- Qualification requirements.
- Certification.
- Deployment.
- Operational progression.

This roadmap does not independently authorize new operational capabilities.

---

## 8. Engineering and Qualification Process

Rictus development follows the established engineering process.

**Requirement → Inspection → Evidence → Approval where required → Implementation → Build → Tests → Qualification → Activation → Operator Observation → Operator Acceptance**

Each stage is independently accountable.

### Requirements

Define intended behavior, scope, control boundaries, and constraints.

### Inspection

Examine the actual implementation and operating environment.

### Evidence

Identify established behavior, failures, conflicts, and unknowns.

### Approval

Obtain required human authority before protected changes.

### Implementation

Make bounded changes consistent with established architecture.

### Build

Confirm that the implementation compiles successfully.

### Tests

Verify expected behavior, negative behavior, compliance boundaries, and failure handling.

### Qualification

Determine whether the implemented capability satisfies its defined exit gate.

### Activation

Activate only the authorized capability.

### Operator Observation

Exercise behavior through the actual operator-selected client or operational path.

### Operator Acceptance

Obtain explicit human confirmation that the demonstrated behavior is accepted.

### Governing Rules

- No stage may be represented as complete without supporting evidence.
- Implementation is not validation.
- Validation is not operational acceptance.
- Module qualification is not operational authority.
- Activation is not acceptance.
- Technical capability is not authorization.
- Human authorization alone does not erase active policy or directive constraints.
- Unknown authority does not become permission.
- Completion of one development cycle does not authorize the next.
- Failed or unverified operations must be reported accurately.

---

## 9. Development Status

| Cycle | Focus | Status |
|---|---|---|
| 0 | Foundation and Identity | Established |
| 1 | Core and Modular Architecture | Established |
| 2 | Communications and Agent Coordination | Operationally Observed |
| 3 | Intelligence and Operational Awareness | Partially Established |
| 4 | Compliance and Control Authority | Current Cycle — Development Hold |
| 5 | Operational Control | Not Enabled |
| 6 | Supervisory Agent Operations | Future Objective |
| 7 | Operational Certification | Future Objective |

**Current Priority**: Cycle 4 — Compliance and Control Authority.

**Current Proven State**: Compliance 1.0.1 control-broker behavior has been operator-observed.

**Current Operational State**: Operational Control Authority Not Granted

**Current Executor State**: Suppressed

**Current Development State**: Hold

---

## 10. Current Hold Point

Development is currently paused after successful operator observation of Compliance 1.0.1.

The demonstrated control broker correctly produced:

`ALLOW → executor=REACHED → execution=SUPPRESSED`

`DENY → executor=BLOCKED → execution=SUPPRESSED`

`ESCALATE → executor=BLOCKED → execution=SUPPRESSED`

The next unresolved development problem is replacing manually supplied proving-state flags with authoritative runtime sources.

Required future truth sources include:

- assigned mission
- current policies
- active directives
- authenticated instructions
- delegated authority
- relevant operational state

No further compliance or operational-control development is implied or authorized by this roadmap entry.

---

## 11. Approval and Change Control

Significant modifications to this roadmap require human assessment and authorization.

Development progress must be recorded against defined deliverables and exit gates.

Implementation details, resolved defects, technical revisions, and operator-observed results belong in `CHANGELOG.md`.

Module-specific implementation details belong in their respective module documentation.

### Approval Record

**Name**: P. Mei  
**Section**: Operations  
**Position**: Founder/CEO  
**Company**: STN-LABZ, LLC.  
**Project**: Rictus  
**DTG**: 20261104:0215Z
**Status**: Approved

---

{small}DTG = Date Time Group. All recorded DTGs use UTC.{/small}
