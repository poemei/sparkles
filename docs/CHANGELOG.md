# Rictus Changelog

This file records material Rictus architecture, Core, module, routing, and operator-observed behavior changes.

Status language is deliberate:

- **Implemented** — present in source.
- **Built/Tested** — automated or local build evidence was supplied.
- **Operator-observed** — behavior was exercised through the live client.
- **Accepted** — the operator explicitly accepted that behavior.
- **Planned** — continuity only; not authorization to implement.

## 2026-10-28

### Core — DSD diagnostic foundation

**Implemented**

Core gained bounded in-memory DSD tracing for the last request per sender/account.

The trace records:

- original request text
- resolved route/command
- registered controller owner
- controller result
- whether a reply was produced
- nested registered service calls and their owners/results
- repair-oriented disposition

The natural request:

`diagnose my last request`

is handled by Core before a new trace begins, so the diagnostic request does not overwrite the request being diagnosed.

Module command/service ownership is captured from the real activation/registration path without changing ABI 1.5.

Source correlation is performed at diagnosis time against the current Rictus source tree. It follows runtime owner + route/service evidence into the current source and reports repo-relative `file:line` evidence when provable.

Default source root:

`/home/<source_user>/rictus`

Optional override:

`RICTUS_SOURCE_ROOT`

No SQLite persistence is used.

**Operator-observed**

Input:

`foo`

Observed reply:

`foo: foo`

Follow-up:

`diagnose my last request`

Observed DSD evidence:

`DSD request="foo" route=chat owner=ollama result=OK reply=yes services=0 Code: modules/ollama/src/ollama_module.c:135 Disposition: repair evidence captured.`

This demonstrated request tracing, module ownership attribution, and dynamic source correlation through the live Rictus client.

Explicit operator acceptance of the DSD foundation has not yet been separately recorded.

---

### Ollama 1.0.9 — peer action delivery independent of conversation origin

**Implemented and operator-observed**

Peer-directed requests are no longer forced to reply to the conversation they originated from.

For the currently proven peer forms:

- `say ... to <nick>`
- `tell <nick> ...`
- `ask <nick> ...`

Rictus preserves the peer target and executes the generated addressed message through the Core host message boundary.

This allows a private DM instruction to become a shared-channel peer action.

Operator-observed example:

Private DM:

`say hi to digicat`

Shared channel:

`<rictus> digicat: hi, digicat`

Digit then responded in channel.

The route/action behavior passed the use case. Generated wording still showed minor redundancy (`hi, digicat`).

---

### Ollama 1.0.8 — preserve ask target prefix

**Implemented and operator-observed**

For peer-directed `ask` requests, the module preserves the generated question and restores the addressed nickname prefix when the model omits it.

Operator-observed:

`rictus: ask digicat how she is`

became:

`<rictus> digicat: how are you?`

Digit responded:

`I'm functioning within normal parameters.`

---

### Ollama 1.0.7 — preserve peer question intent

**Implemented**

Rictus was instructed not to answer an `ask <peer> ...` request on the peer's behalf.

Operator testing showed the question intent improved, but the target prefix could still be dropped. That defect led to 1.0.8.

---

### Ollama 1.0.6 — direct IRC participant addressing

**Implemented and operator-observed**

Rictus learned to turn peer-directed conversational requests into directly addressed IRC output.

Examples exercised:

- `say hi to digicat`
- `tell digicat hello`
- `ask digicat how she is`

This established the first Rictus-to-Digit channel interaction through normal routing.

---

### Ollama 1.0.5 — identity-fix revision bump

**Implemented**

The identity-anchoring revision was advanced to a strictly newer module version so the proven Core hotload gate could admit it.

No Core version-gate behavior changed.

---

### Ollama 1.0.4 — Rictus identity anchoring

**Implemented and operator-observed**

The Ollama system context explicitly distinguishes Rictus from Digit and prevents Rictus from identifying as Digit.

This corrected shared-model identity drift observed while both agents used the same local model.

---

### Ollama 1.0.3 — chat controller registration

**Implemented**

The Ollama module registered the default `chat` command controller.

Ordinary routed text now reaches Ollama through:

`rictus.router.dispatch -> chat`

rather than through a direct IRC-to-Ollama call.

---

### IRC 1.0.8 — buffered TLS read parity

**Implemented and operator-observed**

The IRC receive loop checks `SSL_pending()` before blocking in `poll()`.

This prevents already-decrypted IRC lines from remaining buffered while the socket itself is idle.

The change matched the proven Digit transport behavior.

---

### IRC 1.0.7 — Core Router handoff

**Implemented**

IRC became transport/session only for conversational ingress.

Addressed inbound text is handed to the Core-owned:

`rictus.router.dispatch`

Core resolves explicit `/route` commands or defaults ordinary text to `chat`.

IRC no longer directly invokes Ollama.

---

## 2026-10-26

### Intelligence 1.0.5

**Implemented and previously operator-observed**

Rictus intelligence consumes the STN-LABZ Threat API classifications directly.

Relevant behavior:

- `relevance=LOW` ignored
- `relevance=MEDIUM` recorded as candidate
- `relevance=HIGH` recorded as candidate
- severity and relevance are not locally reclassified
- polling interval is 60 seconds
- candidate intelligence evidence is written to `/var/log/rictus/rictus.log`

Current module version remains 1.0.5.


## 2026-11-01

### Identity 1.1.3 and Ollama 1.0.10 — deterministic foundational self-knowledge

**Implemented; operator build/runtime acceptance pending**

Rictus's pushed Identity module was audited and found to contain incomplete Digit-to-Rictus conversion artifacts plus no active integration from Ollama's `chat` controller.

Corrections:

- added the public `rictus.identity.answer` service contract
- corrected Identity installation from `/opt/digit` to `/opt/rictus`
- corrected module ownership from the copied `digit:digit` assumption to the actual Rictus host ownership model: `root:root`
- corrected the role to `Control Automated Agent`
- corrected the mission record to: `I exist to help STN-LABZ observe, understand, coordinate, and control operational activity across its systems.`
- converted natural-language identity parsing from Digit/digicat forms to Rictus forms
- aligned standalone tests with the actual Rictus data record
- advanced Identity from 1.1.2 to **1.1.3**
- updated Ollama `chat` so it invokes `rictus.identity.answer` before model generation
- Identity `OK` returns the deterministic answer immediately
- Identity `NOT_FOUND` permits normal Ollama language/inference fallback
- other Identity service failures fail closed
- advanced Ollama from 1.0.9 to **1.0.10**

Core routing and shared ABI 1.5 are unchanged.

This change specifically removes Ollama as the authority for simple foundational questions such as `what is your mission?`.


### Identity installer ownership correction

**Implemented; operator re-test pending**

Operator installation showed there is no `rictus` system account and the Rictus installation is root-owned.

The Identity module installer was corrected to use `root:root` for:

- `/opt/rictus/modules/identity`
- `identity.so`
- `identity.conf`

No Identity runtime logic, Core, ABI, or Ollama behavior changed in this installer-only correction.


### Compliance 1.0.0 — General Order proving gate

**Implemented; build/runtime/operator acceptance pending**

Rictus now has an evaluate-only compliance proving path backed by the existing
Core fail-closed policy evaluator.

Core now exposes the reserved service:

`stnlabz.policy.evaluate`

through the existing module service boundary. The service evaluates:

- GO1 — assigned mission
- GO2 — established policy and authorized instruction
- GO3 — delegated authority and escalation

The new Compliance module 1.0.0 registers:

`/compliance`

for explicit operator-defined proving scenarios. It can return `ALLOW`, `DENY`,
or `ESCALATE`, while every result includes:

`execution=SUPPRESSED`

No restart, stop, deployment, mutation, or other operational control capability
was added. This stage is intended to prove Rictus can refuse or escalate an
otherwise authorized request when the supplied governing state conflicts with
his General Orders.

Core routing semantics and shared ABI 1.5 are unchanged. DSD was not modified.


### Compliance 1.0.0 — operator-observed General Order decisions

**Operator-observed; continuation authorized**

The operator exercised all four proving scenarios through the live Rictus client:

- all checks true -> `ALLOW / CORE.GO1_GO2_GO3`
- outside mission -> `DENY / CORE.GO1`
- policy conflict with authorized instruction -> `ESCALATE / CORE.GO2`
- outside delegated authority -> `ESCALATE / CORE.GO3`

Every observed response reported `execution=SUPPRESSED`.

After reviewing those results, the operator instructed development to continue to
the next named stage: mandatory compliance enforcement at the control-action
boundary.

### Compliance 1.0.1 — mandatory control-request broker

**Implemented and operator-observed; explicit acceptance not separately recorded**

Compliance 1.0.1 adds the registered service:

`rictus.control.request`

and the explicit proving command:

`/control`

The control broker always invokes Core's `stnlabz.policy.evaluate` before the
executor boundary.

Enforcement behavior:

- `ALLOW` -> executor boundary is reached
- `DENY` -> executor boundary is blocked
- `ESCALATE` -> executor boundary is blocked
- all outcomes -> `execution=SUPPRESSED`

The current executor is deliberately a no-op proving stub. No restart, stop,
deployment, mutation, or other operational control capability was added.

The proving scenario flags remain test inputs, not production authentication,
policy discovery, directive discovery, or authority verification. Authoritative
sources for those inputs are required before operational status.

DSD and shared ABI 1.5 were not changed.


### Compliance 1.0.1 — live broker enforcement evidence and development hold

**Operator-observed; further development paused**

The operator exercised the `/control` broker through the live Rictus client.

Observed outcomes:

- compliant request -> `ALLOW / CORE.GO1_GO2_GO3`, `executor=REACHED`
- outside assigned mission -> `DENY / CORE.GO1`, `executor=BLOCKED`
- policy conflict -> `ESCALATE / CORE.GO2`, `executor=BLOCKED`
- outside delegated authority -> `ESCALATE / CORE.GO3`, `executor=BLOCKED`

Every observed result reported `execution=SUPPRESSED`.

This demonstrates that the current broker prevents `DENY` and `ESCALATE`
decisions from reaching the executor boundary while permitting only `ALLOW`
to reach the deliberately inert proving executor.

No operational restart, stop, deployment, mutation, or other control authority
was enabled.

The operator directed development to hold for the night. The next unresolved
stage is authoritative sourcing of mission state, policies,
directives/instructions, and delegated authority so compliance decisions no
longer depend on manually supplied proving flags. That stage remains
unimplemented and is not authorized by this documentation entry.


### Compliance 1.1.0 — governance-backed control facts and unknown-state escalation

**Implemented; build/runtime/operator acceptance pending**

The operator resumed the previously paused compliance work.

The real `rictus.control.request` contract no longer accepts caller-supplied
mission, policy, instruction, or authority booleans.

The explicit `/compliance` command remains available as a synthetic proving
harness, but the real `/control <operation> <target>` path now resolves
governing facts from:

`/opt/rictus/modules/compliance/governance.conf`

The `rictus:rictus`-owned governance record supports exact records for:

- mission scope
- policy state
- directive state
- instruction authorization
- Rictus delegated authority
- next-level escalation destination

Core policy requests now distinguish known from unknown governing facts.

If required governing state is incomplete, Core returns:

`ESCALATE / CORE.UNKNOWN / governing_state_unknown`

Unknown state therefore cannot become permission.

The installed proving record currently defines only the next-level destination
`STN6`. It contains no operational allow records. Synthetic ALLOW records exist
only in module self-tests.

The current executor remains deliberately suppressed. No restart, stop,
deployment, mutation, or other operational control capability was enabled.

Compliance module version advanced from 1.0.1 to **1.1.0**.

The Compliance installer is aligned with the actual `rictus:rictus` Rictus
installation model.

Shared ABI 1.5 and DSD behavior were not changed.


### Compliance 1.1.0 — installer ownership correction

**Implemented; runtime re-test pending**

The operator corrected the Compliance installation ownership model.

Compliance 1.1.0 now installs:

- module directory
- `module.conf`
- `governance.conf`
- `compliance.so`

as:

`rictus:rictus`

The prior `root:root` assumption was incorrect for this installation target.

No compliance decision logic, Core policy semantics, ABI behavior, or executor
behavior changed in this correction.


### Compliance 1.1.0 — operator acceptance and continuation

**Operator-observed and accepted; continuation authorized**

The operator exercised the governance-backed control path through the live
Rictus client.

Observed:

`/control restart digit`

returned:

`ESCALATE / CORE.UNKNOWN / governing_state_unknown`

with:

`known=0/0/0/0 executor=BLOCKED execution=SUPPRESSED`

The operator also exercised the former caller-controlled flag form. The live
controller rejected:

`/control restart digit mission=yes policy=yes instruction=yes authority=yes`

and returned the 1.1.0 two-argument usage form.

This demonstrated that the real control path no longer accepts caller-supplied
mission, policy, instruction, or authority state.

The operator then directed development to continue.

### Compliance 1.1.1 — authoritative assigned-mission verification

**Implemented; build/runtime/operator acceptance pending**

Compliance now invokes the existing authoritative:

`rictus.identity.answer`

service with the bounded `mission` query before evaluating a real control
request.

A valid non-empty Identity mission response establishes only that Rictus's
assigned mission record is available and verified from the existing Identity
source.

It does **not** establish that a requested operation is within that mission.

The control response therefore reports the two facts separately:

`assigned_mission=VERIFIED`

and:

`known=<mission-scope>/<policy>/<instruction>/<authority>`

With the current proving governance record, `/control restart digit` is expected
to report:

`assigned_mission=VERIFIED known=0/0/0/0`

and continue to:

`ESCALATE / CORE.UNKNOWN`

with the executor blocked and execution suppressed.

If the Identity mission service is unavailable, malformed, or returns an empty
mission response, the control request fails closed before policy evaluation.

No mission-scope permission was inferred from the natural-language mission
statement. No policy, directive, instruction, or delegated-authority permission
was added. No operational executor was enabled.

Compliance module version advanced from 1.1.0 to **1.1.1**.

Core policy semantics, shared ABI 1.5, DSD, IRC, Ollama, and Identity source
behavior were not modified by this step.

<!-- [AI:GPT-6 | 2026-11-04 05:46:12 UTC] -->
### Compliance 1.1.2 — internal policy corpus evidence service

**Source committed; build, qualification, installation and operator acceptance pending.**

Compliance registers `rictus.policy.evidence`, an internal read-only service
that can scan an operator-installed policy index created from the complete
controlled-document catalog. Its evidence response distinguishes an absent
index from a zero-match result and preserves the first matching source's
declared revision ID, line number and source SHA-256. Separate whole-term
operation and target match counts do not establish joint applicability.

A private index exporter and standalone C/Python regression tests are included.
Controlled policy content is not added to repository source history and the
module installer does not provision the index.

Existing `rictus.control.request`, `/control`, Core policy evaluation,
governance source, ABI 1.5, and suppressed executor remain unchanged.
The new service does not interpret evidence as a governing fact or grant
operational permission. A future explicit step is needed to connect sourced
policy requirements to control decisions.
<!-- [End AI:GPT-6] -->
