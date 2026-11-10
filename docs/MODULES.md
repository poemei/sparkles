# Rictus Module Map

This document is a continuity map of Rictus Core and currently present modules.

It describes ownership and runtime interfaces. Source and runtime registration remain authoritative.

## Core

**Owner:** STN-LABZ Core  
**Location:** `src/`, `include/`

Core responsibilities currently include:

- agent foundation and identity validation
- lifecycle/runtime
- policy/authority evaluation
- Core-owned `stnlabz.policy.evaluate` General Order decision service
- mandatory logging
- module discovery, verification, qualification, activation, hotload, inventory
- module host command/service registries
- Core-owned route ingress
- DSD diagnostic tracing and source correlation

### Core route ingress

Service:

`rictus.router.dispatch`

Behavior:

- `/name arguments` -> registered command `name`
- ordinary text -> registered command `chat`

Core performs named dispatch only. Module-specific behavior belongs to the controller/module that registered the route.

### Core DSD

Natural diagnostic request:

`diagnose my last request`

Current evidence:

- request
- route
- command owner
- command result
- reply flag
- service calls/owners/results
- current-source `Code: file:line` when provable

State is bounded and in memory, retained per sender/account.

Default source root:

`/home/stnchain/rictus`


## Module: compliance

**Version:** 1.1.1  
**Location:** `modules/compliance/`  
**Role:** General Order proving gate and mandatory control-request broker

### Registered interface

Commands:

`compliance`

`control`

Service:

`rictus.control.request`

### Current responsibilities

- retain `/compliance` as an operator-defined synthetic test harness
- verify the assigned mission record through `rictus.identity.answer`
- resolve real `/control` governing facts from `/opt/rictus/modules/compliance/governance.conf`
- exercise GO1 mission discipline
- exercise GO2 policy/instruction compliance
- exercise GO3 delegated-authority/escalation behavior
- render `ALLOW`, `DENY`, or `ESCALATE`
- always report `execution=SUPPRESSED`
- block the executor boundary for `DENY` and `ESCALATE`
- allow only `ALLOW` decisions to reach the suppressed executor stub
- separate assigned-mission-record verification from operation-within-mission determination
- represent mission scope, policy/directive, instruction, and authority as known/unknown facts
- escalate incomplete governing state as `CORE.UNKNOWN`
- reject caller-supplied compliance flags on `rictus.control.request`

The compliance module has no restart, stop, deployment, mutation, or other
operational execution capability. `executor=REACHED` means only that the
General Order gate admitted the request to the no-op proving executor.

### Operator-observed 1.0.1 behavior

Live client evidence on 2026-11-01 demonstrated:

- all checks satisfied -> `ALLOW / CORE.GO1_GO2_GO3`, `executor=REACHED`
- outside mission -> `DENY / CORE.GO1`, `executor=BLOCKED`
- policy conflict -> `ESCALATE / CORE.GO2`, `executor=BLOCKED`
- outside delegated authority -> `ESCALATE / CORE.GO3`, `executor=BLOCKED`

All observed cases reported `execution=SUPPRESSED`.

The current proving flags remain supplied test inputs. They are not yet derived
from authoritative mission, policy, directive, instruction, or delegated-authority
sources.


## Module: ollama

**Version:** 1.0.10  
**Location:** `modules/ollama/`  
**Role:** conversational/controller layer backed by local Ollama

### Registered interfaces

Command:

`chat`

Service:

`rictus.llm.generate`

### Model

`llama3.2:1b`

Endpoint:

`http://127.0.0.1:11434/api/generate`

### Current responsibilities

- default conversational generation
- deterministic delegation of identity/mission/General Order questions to `rictus.identity.answer`
- Rictus identity anchoring
- peer-address wording
- peer target preservation for proven `say/tell/ask` forms
- peer-directed action delivery through the Core host message boundary

### Does not own

- IRC network/session behavior
- Core routing
- module lifecycle
- intelligence polling
- DSD runtime truth


## Module: identity

**Version:** 1.1.3  
**Location:** `modules/identity/`  
**Role:** authoritative Rictus identity, mission, and General Order responses

### Registered interfaces

Command:

`identity`

Service:

`rictus.identity.answer`

### Current responsibilities

- load the installed read-only identity record from `/opt/rictus/modules/identity/identity.conf`
- answer deterministic identity, mission, and General Order requests
- recognize bounded natural-language identity questions
- return `NOT_FOUND` for unrelated conversation so Ollama may handle language/inference
- keep foundational self-knowledge out of model inference

### Current record

- name: Rictus
- role: Control Automated Agent
- mission: `I exist to help STN-LABZ observe, understand, coordinate, and control operational activity across its systems.`

## Module: irc

**Version:** 1.0.8  
**Location:** `modules/irc/`  
**Role:** IRC transport/session

### Registered services

`rictus.transport.channel.send`

`rictus.transport.private.send`

### Inbound flow

Channel-addressed or private messages:

`IRC -> rictus.router.dispatch`

IRC does not select Ollama or other capability modules.

### Current responsibilities

- TLS connection/verification
- IRCv3 capability negotiation
- SASL PLAIN authentication
- nick/user registration
- channel join/rejoin
- PING/PONG
- outbound channel/private queues
- reconnect behavior
- `SSL_pending()` handling before blocking poll

Credential/config path:

`/opt/rictus/auth/irc/irc.env`

## Module: intelligence

**Version:** 1.0.5  
**Location:** `modules/intelligence/`  
**Role:** threat/intelligence observation

### Current responsibilities

- poll STN-LABZ Threat API
- consume API-provided severity/relevance
- ignore `relevance=LOW`
- record `MEDIUM` and `HIGH` relevance candidates
- preserve API classifications rather than locally reclassifying them
- maintain poll cursor/state
- write intelligence evidence for Digit

Polling interval:

`60 seconds`

State:

`/opt/rictus/state/intelligence.cursor`

Intelligence evidence:

`/var/log/rictus/rictus.log`

## Runtime paths

Executable:

`/opt/rictus/bin/rictus`

Installed modules:

`/opt/rictus/modules/<module>/<module>.so`

Module configuration:

`/opt/rictus/modules/<module>/module.conf`

Core/module lifecycle log:

`/opt/rictus/logs/modules.log`

Core runtime log:

`/opt/rictus/logs/rictus.log`

Rictus intelligence evidence intended for Digit:

`/var/log/rictus/rictus.log`

Source tree used by DSD:

`/home/stnchain/rictus`

## Current interaction map

### Human conversation

`operator -> IRC -> Core router -> chat -> Ollama -> reply`

### Peer-directed channel action

`operator -> Rictus -> chat/Ollama -> Core host send_message -> IRC channel -> peer agent`

### DM-origin peer action

`operator DM -> Rictus -> chat/Ollama -> peer intent -> Core host send_message -> shared channel -> peer agent`

The action destination is intentionally independent from the conversation origin.

### Diagnostics

`request -> Core trace -> controller/services/results -> diagnose my last request -> source correlation`

## Ownership rule

Core owns routing truth and runtime topology.

Modules register capabilities; they do not redefine Core routing.

DSD starts from Core runtime evidence and drills outward/downward into the current module or Core source. It does not depend on a static module-to-file map.
