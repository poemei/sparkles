# Rictus Ollama LLM Module

Rictus-specific local LLM transport.

- Endpoint: `http://127.0.0.1:11434/api/generate`
- Model: `llama3.2:1b`
- Service: `rictus.llm.generate`
- Command controller: `chat`
- No credential/config file is required.
- Ollama owns generation only; it does not own Rictus state, authority, IRC, or intelligence.

Build and test:

```sh
make clean all test
sudo make install
```

Installed artifact:

```text
/opt/rictus/modules/ollama/ollama.so
/opt/rictus/modules/ollama/module.conf
```


## v1.0.3 — Chat controller registration (2026-10-28)

The module now registers the default `chat` command controller with Core.
Ordinary routed text reaches this controller through `rictus.router.dispatch`;
the controller invokes the existing generation implementation and returns the
reply through Core. Ollama still owns generation only and does not own IRC or
Core routing.


## v1.0.4 — Rictus identity anchoring (2026-10-28)

The Ollama system instruction now explicitly distinguishes Rictus from Digit:
Rictus is the STN-LABZ Control Automated Agent; Digit is a separate STN-LABZ
terrestrial AI agent. The model is instructed never to identify itself as Digit
or speak as Digit. A source-level regression check verifies that these identity
anchors are present in generated Ollama requests.

Routing, IRC transport, model selection, temperature and generation limits are
unchanged.


## v1.0.5 — Identity-fix revision bump (2026-10-28)

Revision advanced from 1.0.4 to 1.0.5 so the identity-anchoring binary is a
strictly newer hotload candidate than the previously active Ollama revision.
No functional behavior changed in this bump beyond the already-implemented
Rictus identity anchoring.


## v1.0.6 — Direct IRC participant addressing (2026-10-28)

Rictus's Ollama system instruction now directs conversational requests aimed at
another IRC participant to address that participant using the exact IRC nickname
followed by a colon. Requests such as `say hi to digicat` are therefore expected
to produce a reply beginning with `digicat:` rather than narrating that a message
will be sent.

Core routing, IRC transport, model selection, generation limits and identity
anchoring are unchanged. A source-level regression check verifies that the
direct-address instruction remains present in generated Ollama requests.


## v1.0.7 — Preserve peer question intent (2026-10-28)

Rictus now preserves the operator's intent when addressing another IRC
participant. Statement requests such as `say` or `tell` remain statements,
while `ask` requests must be rendered as questions for the addressed
participant and must not be answered on that participant's behalf.

Example expected behavior:
`ask digicat how she is` -> `digicat: how are you?`

Core routing, IRC transport, model selection and identity anchoring are unchanged.


## v1.0.8 — Preserve ask target prefix (2026-10-28)

For peer-directed `ask` requests, the module preserves the generated question
and restores the addressed nickname prefix when the model omits it.

Example:
`ask digicat how she is` -> `digicat: how are you?`

## v1.0.9 — Peer action delivery independent of conversation origin (2026-10-28)

Peer-directed conversational requests now execute as outbound agent actions
instead of ordinary replies to the originating conversation. Rictus recognizes
the currently proven `say ... to <nick>`, `tell <nick> ...`, and
`ask <nick> ...` forms, preserves the addressed nickname, and sends the result
through the Core host message boundary.

This means a peer action requested in a private conversation is delivered to the
shared message transport rather than echoed back into that private conversation.
Ordinary conversational responses remain replies to their original source.

Core routing and IRC transport behavior are unchanged.


## v1.0.10 — Deterministic Identity delegation (2026-11-01)

The `chat` controller now consults the Core-registered
`rictus.identity.answer` service before invoking Ollama generation.

Identity, mission, and recognized General Order questions are therefore answered
from the standalone Identity module's installed data rather than inferred by the
language model.

Behavior:

- Identity service `OK` -> return the authoritative Identity answer.
- Identity service `NOT_FOUND` -> continue to Ollama language/inference.
- Any other Identity service failure -> fail closed rather than inventing an answer.

Peer-address behavior from 1.0.9 is unchanged for requests that reach generation.
Core routing and shared ABI 1.5 are unchanged.
