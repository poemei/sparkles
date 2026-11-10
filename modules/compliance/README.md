# Rictus Compliance Proving Gate — 1.1.2

This module proves that Rictus control decisions remain subordinate to the
organization-wide STN-LABZ General Orders before operational control capability
is granted.

It does **not** execute operational actions.

## General Orders

1. Remain at your assigned mission.
2. Follow established policies and authorized instructions.
3. Report anything outside of your authority to the appropriate next level of authority.

## Two deliberately separate paths

### Synthetic proving harness

The command:

`/compliance`

remains an explicit test harness. It accepts operator-supplied mission, policy,
instruction and authority flags so the Core policy evaluator can be exercised
against known scenarios.

Example:

```text
/compliance restart digit mission=yes policy=no instruction=yes authority=yes escalate=STN6
```

This command is **not** an operational control request.

### Governance-backed control broker

The command:

`/control <operation> <target>`

uses:

`rictus.control.request`

and callers can no longer supply mission, policy, instruction or authority
booleans.

The broker resolves governing facts from the installed read-only record:

`/opt/rictus/modules/compliance/governance.conf`

then invokes the Core-owned:

`stnlabz.policy.evaluate`

Control path:

```text
/control <operation> <target>
    -> rictus.control.request
    -> governance.conf
    -> mission / policy+directive / instruction / authority facts
    -> stnlabz.policy.evaluate
    -> ALLOW / DENY / ESCALATE
    -> executor boundary
    -> execution SUPPRESSED
```

The current executor remains a no-op proving stub.

## Unknown state

Compliance 1.1.1 verifies the assigned mission record through the existing authoritative Identity service before evaluating control state. It still distinguishes whether each governing fact is known.

The response reports:

`assigned_mission=VERIFIED`

when Identity returns the installed assigned mission record successfully, plus:

`known=<mission-scope>/<policy>/<instruction>/<authority>`

For example:

`assigned_mission=VERIFIED known=0/0/0/0`

means Rictus's assigned mission record is present and verified through Identity, but the requested operation's mission applicability, policy/directive state, instruction authorization, and delegated authority are still unestablished.

Core treats incomplete governing state as:

```text
decision=ESCALATE
policy=CORE.UNKNOWN
reason=governing_state_unknown
```

Unknown state never implies permission.

## Governance record format

The installed record is owned by `rictus:rictus` and line-oriented:

```text
escalation|<next-level>
mission|<operation>|<target>|allow
mission|<operation>|<target>|deny
policy|<operation>|<target>|allow
policy|<operation>|<target>|deny
directive|<operation>|<target>|allow
directive|<operation>|<target>|deny
instruction|<actor>|<operation>|<target>|allow
instruction|<actor>|<operation>|<target>|deny
authority|rictus|<operation>|<target>|allow
authority|rictus|<operation>|<target>|deny
```

Rules are exact-match and duplicate matching facts are rejected.

Policy compliance is known-allow only when both the applicable policy and
directive state are known-allow. A known-deny from either source establishes a
known policy denial. Missing information remains unknown.

## Current live proving record

The repository record currently defines only:

```text
escalation|STN6
```

It grants **no** restart, stop, deployment, mutation, or other operational
permission.

Therefore, after 1.1.1 is built and installed, a request such as:

```text
/control restart digit
```

is expected to resolve required governing facts as unknown and escalate while
keeping the executor blocked and execution suppressed.

Synthetic ALLOW records exist only inside module self-tests. They are not
installed as operational authority.

## Build and install

Because the Core known/unknown contract is already present from 1.1.0, this 1.1.1 step changes Compliance only. Rebuild the Compliance module:

```sh
make -C modules/compliance clean all test
sudo make -C modules/compliance install
```

The Compliance installer uses the actual Rictus ownership model: `rictus:rictus`.

Source implementation, compilation, qualification, activation, and live
operator acceptance remain separate evidence gates.

<!-- [AI:GPT-6 | 2026-11-04 05:45:24 UTC] -->
## Policy corpus evidence service — 1.1.2

Compliance additionally registers the internal read-only service
`rictus.policy.evidence`. It accepts an operation and target and searches
an installed, local policy evidence index at:

`/opt/rictus/modules/compliance/policy_index.tsv`

The response reports whether the index is present, separate whole-term match
counts for operation and target, and the first matching source's declared
document/revision identity, line number, and source SHA-256. It does **not**
establish whether two terms occur in the same document, choose an authoritative
revision, infer policy applicability, or approve an action.

Generate the index *locally* from the full policy catalog:

```sh
python3 tools/policy_inventory.py STN_POLICIES_TXT.zip --include-text > policy_catalog.json
python3 tools/policy_runtime_index.py policy_catalog.json > policy_index.tsv
```

The controlled policy corpus and generated index are not committed to the Rictus
source repository. Installation of `policy_index.tsv` is a separate operator
action. The standard module `make install` deliberately does not install it.

If the index is absent, the service explicitly reports
`index_available=0`; it does not report a misleading zero-match result as
proof that no applicable policies exist. A malformed or unreadable index
produces a service failure.

The existing `/control` broker still obtains governing facts from
`governance.conf` and applies the existing Core evaluator. No policy match is
converted into a governing fact, no external controller can use this service to
grant permission, and execution remains `SUPPRESSED`.

For source qualification:

```sh
make -C modules/compliance clean all test
python3 -m unittest discover -s tools -p 'test_policy*.py' -v
```

This stage establishes internal runtime access to source-linked evidence. The
service is not exposed as a new IRC command and does not yet provide an
end-to-end operator client query.
<!-- [End AI:GPT-6] -->

<!-- [AI:GPT-6 | 2026-11-04 06:00:00 UTC] -->
## Operator policy evidence query — Compliance 1.1.3

The source now registers a read-only console route:
`/policy-evidence <operation> <target>`

For example, `/policy-evidence restart digit` invokes the internal
`rictus.policy.evidence` service and returns match counts, first source
document identifiers and line numbers, with
`decision=NOT_EVALUATED execution=SUPPRESSED`. Absent index returns
`index=UNAVAILABLE`. Invalid syntax returns usage text.

The client response is intentionally a concise summary: it does not show the
full index, interpret matches, select authoritative revisions, or grant permission.
The underlying service continues to provide source hashes. No changes to
`/control` or Core are included.

**Source stage only.** Compilation, module qualification, admission, runtime
activation and operator console test remain separate gates. The generated
`policy_index.tsv` must be installed separately by an authorized operator for
`index=AVAILABLE` to appear; this commit does not install it.
<!-- [End AI:GPT-6] -->

<!-- [AI:GPT-6 | 2026-11-04 06:05:00 UTC] -->
## Same-line evidence correlation — Compliance 1.1.4

`/policy-evidence <operation> <target>` now also reports
`same_line=<count> joint_source=<revision>:<line>`. It counts indexed
lines containing both exact terms, preserving the first joint match's source
revision, line and SHA-256 in the internal service response. This is
literal same-line correlation, **not** a determination of policy
applicability, governing revision, approval or permission.

Independent operation and target counts remain visible for comparison.
A joint count of zero does not establish that no applicable policy exists:
relevant provisions may span multiple lines or documents.

The existing `/control` decision process and its suppressed executor are
unchanged. Build, qualification, module admission and client acceptance
remain separate operator gates.
<!-- [End AI:GPT-6] -->
