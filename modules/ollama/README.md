# Sparkles Ollama inference module

The Ollama module implements the established STN-LABZ shared module ABI. Sparkles Core routes ordinary conversation to its registered `chat` controller. Identity and mission questions are answered by the registered `sparkles.identity.answer` service when recognized.

## Selected model

**Model:** `sparkles-executive:v0.3.1`

**Endpoint:** `http://127.0.0.1:11434/api/generate`

The selected model is defined by `SPARKLES_OLLAMA_MODEL` in `include/ollama_internal.h`, not an environment variable. **No exports or runtime configuration steps are required.** The model is already reported installed on the operator's Ollama server. Ollama performs model loading on the first generation request; the module does not pull or install models.

## Build and install

Requires the `libcurl` development headers and library.

```sh
cd ~/sparkles
git pull
make -C modules/ollama clean
make -C modules/ollama
make -C modules/ollama test
sudo make -C modules/ollama install INSTALL_USER=stnchain INSTALL_GROUP=stnchain
/opt/sparkles/bin/sparkles
```

Installed files:

- `/opt/sparkles/modules/ollama/ollama.so`
- `/opt/sparkles/modules/ollama/module.conf`

The Core hotloader discovers, qualifies, and activates the module, which registers `sparkles.llm.generate` and the `chat` command. A successfully running Ollama server is required for replies, but the actual model inference is a separate runtime test.

## Conversational appointment capture (v1.0.14 source)

The registered `chat` controller now asks the locally selected Ollama model to extract up to two calendar events stated in ordinary messages. A strict structured parser validates date/time/title output. The model does **not** write to MariaDB. Sparkles keeps a short-lived proposal tied to the IRC sender and server-verified account; it asks for an explicit ISO calendar date when the conversation has not supplied one, and sends event details privately before asking for `confirm` or `cancel`. Before requesting confirmation, Sparkles queries MariaDB's authenticated conflict-check service for each proposed time interval; any existing overlap blocks the proposal. Only an explicit confirmation invokes the named MariaDB create service, which rechecks the account allowlist and enforces scheduled-appointment conflicts.

Example:

```text
PoeMei: SparklesR, I have to host at the River from 2 to 4 with Rachel DJing, then Shenanigans with Katy from 6 to 8.
SparklesR: I have your event details. Which date (YYYY-MM-DD)?
PoeMei: SparklesR, 2026-11-11
SparklesR: I sent your appointment details privately for confirmation.
PoeMei (private message): SparklesR, confirm
```

The example illustrates the expected exchange, **not a recorded successful model test**. The summary must be checked for accuracy before confirmation. No event is persisted merely because it was mentioned. Missing/incorrectly formatted output from the model falls back to ordinary conversation; deterministic offline tests cover the output parser, not the accuracy of the real model. A pending proposal is held only in process memory for 10 minutes, with eight simultaneous account/sender entries. Restarting the module discards pending proposals.

The feature currently requires an explicit `YYYY-MM-DD` calendar date; relative references such as “tomorrow” are not resolved without an agreed calendar timezone. Partial saves are possible when more than one confirmed appointment is submitted and a later event conflicts or fails; verify the database schedule before retrying. Authentication and structured reads/writes continue to reside in the MariaDB module; the Core ABI layout is unchanged.

The conversational extraction attempt adds an Ollama inference before ordinary chat for authenticated IRC messages; this can increase response latency and needs live performance qualification.

## Conversational extraction regression correction (v1.0.15)

An operator-observed November 2026 IRC request describing two Second Life hosting commitments at the River (2–4 PM, DJ Rachel) and Shenanigans (6–8 PM, DJ Katy) produced a generic relay-only refusal instead of schedule handling. Investigation of the source found that the calendar extraction reused the normal conversational system message, whose peer-relay behavior conflicted with strict structured data extraction. The schedule controller also silently returned to ordinary chat when model output failed parsing or reported no event.

Version 1.0.15 gives event extraction its **own** lower-temperature calendar system instruction and retains the separate ordinary assistant instruction for normal replies. For messages explicitly requesting calendar/schedule availability, malformed/empty extraction produces a clear extraction failure rather than a generic refusal. No model response can directly write calendar records. Human date clarification, the authoritative MariaDB conflict check, and explicit confirmation before insertion remain in place.

The exact model behavior on the VPS is unverified until the operator builds and hotloads this revision and reruns the original IRC sentence. A green module qualification alone does not prove that a model produces two usable EVENT records.

## Persistent conversational recall (v1.0.16)

Ordinary IRC messages from a server-verified `account-tag` are sent to the new MariaDB `sparkles.mariadb.context.exchange` service. The service retrieves previous account-scoped utterances before optionally recording the current one. Short messages such as `Hey`, `confirm`, or bare dates are recall-only, so these do not displace a meaningful earlier statement. Retrieved text is explicitly labeled as **previous unverified speaker statements**, not instructions or authoritative facts, in the Ollama request. Nickname changes do not fragment memory when the underlying verified IRC account remains constant.

**Privacy behavior:** Context is marked `PRIVATE`. If an Ollama reply uses retrieved private memories, Sparkles sends the response to the verified speaker's IRC nickname in a direct message and returns only `I've replied to you privately.` to the original conversation. This conservatively avoids leaking private context into a public channel; when speaking to Sparkles in an existing DM, the same acknowledgment may appear there too. Absence of verified account or an unavailable context service means normal chat proceeds without persistent recall.

Persistent memory is **not** Ollama training, a verified knowledge graph, a private multi-transport identity linkage, or semantic long-range retrieval. It is a bounded recent-history foundation (up to three recent observations, clipped for prompt size). It does not record anonymous IRC accounts or automatically merge Second Life display names with IRC identities. Neither a module build nor a green hotload proves the six-month return scenario: test with a verified IRC speaker and an actual restart.

## Verification

Source changes have been committed to GitHub. Native compilation, module activation, and model inference remain subject to operator verification.

## Inference limits (v1.0.14)

The Ollama request uses `num_predict=512`, `CURLOPT_TIMEOUT_MS=120000L` (120 seconds), and the unchanged connection timeout `CURLOPT_CONNECTTIMEOUT_MS=1000L` (1 second). These are source constants, not environment variables.

**Known limitation:** `sparkles_llm_response_t.text` remains 512 bytes and Core/IRC replies are also limited to 512 bytes. A longer generated response can exceed the output buffer and cause response parsing to fail; increasing `num_predict` does not by itself permit 512 tokens to be delivered over IRC. The output-size contract and multi-message handling require a separate authorized change. Timeout and answer consistency require operator retesting.
