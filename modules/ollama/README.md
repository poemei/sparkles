**v1.2.3 (GitHub source only):** Second Life session chat is dispatched under an isolated `slg-<session-uuid>` namespace. This path invokes conversational generation only, without contact identification, private memory retrieval, scheduling reads, or state-changing tools. Session speech must be addressed to Sparkles before the SL transport dispatches a reply. Group-session invitation acceptance through capabilities remains unimplemented; delivery is not live-tested.

**v1.2.2 remaining-day request correction (2026-11-12, source only):** `Sparkles bish what is my shedule for the rest of the day?` now matches calendar-read intent and uses the contact-scoped MariaDB service with a same-day datetime lower bound. Free-form inference must not invent a schedule response for this phrasing. Local timezone semantics remain subject to operator configuration and testing. Neither native build nor live reply is verified.

**Second Life observation routing (v1.2.1 source, 2026-11-12):** Incoming SL private messages are now persisted by the SL ingress worker before optional Core/Ollama reply processing; Ollama's context exchange remains recall-only for SL and does not duplicate IM rows. Nearby chat is recorded passively and does not trigger a reply or automatically become model instructions. Previous IRC fallback code is not an installation prerequisite. No live model retraining or SL deployment was performed.

**Named guest scheduling (Ollama v1.2.0, GitHub source, 2026-11-12):** A new scheduling request may explicitly name one contact using `with <first name>`. The guest remains attached to the proposal across date/time clarification, is shown as a pending invitee in the human confirmation, and is sent to MariaDB for unique contact lookup and transactional participant creation. Requests specifying multiple guests are rejected rather than silently saving a creator-only meeting. This source change does not implement guest RSVP, multiple simultaneous guests, or legacy appointment ownership migration. Native compilation, client testing, installation and operator acceptance are unverified.

**Schedule read formatting (source, 2026-11-12):** Ollama v1.1.11 formats the existing one-IM calendar response with a date heading and each appointment on a separate, blank-line-separated bullet, showing HH:MM start/end and the unchanged title. It retains bounded output, the more-results note when space permits, and delivery via the originating Core reply callback. Appointment records, calendar authorization and write behavior are unchanged. GitHub source readback verified; native build and live SL rendering not yet tested.

**Second Life executive calendar reply routing (GitHub source, 2026-11-12):** In Ollama v1.1.10, calendar read responses, scheduling prompts, summaries, confirmations, cancellation and rescheduling messages use the incoming Core reply callback. The previous IRC `send_private_message` delivery path was removed from executive scheduling, preventing SL avatar names from being sent as IRC DM destinations. Bounded schedule-read results are returned in a single IM, with an overflow/has-more indicator when space permits. A callback qualification probe was added; native compilation, SL end-to-end delivery, and operator acceptance remain unverified. The operator removed IRC from the running installation; this source correction does not install or activate it.

# Sparkles Ollama inference module

The Ollama module implements the established STN-LABZ shared module ABI. Sparkles Core routes ordinary conversation to its registered `chat` controller. Identity and mission questions are answered by the registered `sparkles.identity.answer` service when recognized.

## Selected model

**Model:** `sparkles-executive:v0.3.1`

**Endpoint:** `http://127.0.0.1:11434/api/generate`

The selected model is defined by `SPARKLES_OLLAMA_MODEL` in `include/ollama_internal.h`, not an environment variable. **No exports or runtime configuration steps are required.** The model is already reported installed on the operator's Ollama server. Ollama performs model loading on the first generation request; the module does not pull or install models.

## SL contact persistence acknowledgement (v1.1.8 source)

For native Second Life IMs (identified by the transport-created
`sl:<avatar UUID>` principal), Ollama invokes MariaDB's
`sparkles.mariadb.contacts.identify` service. Explicit volunteered
introductions such as `my name is Poe` and `call me Poe` are written
as a self-reported first name, linked to the corresponding SL UUID.
A name is not considered saved until MariaDB commits the contact and
identity transaction. An unsuccessful schema match or SQL write
produces an explicit *not saved* response instead of a promise of
memory.

An SL requester asking `do you know me?` receives the name returned
by MariaDB's contact lookup, if found, not a guessed identity. Later
chat prompts may use the saved, self-reported first name as bounded
context. No inferred executive relationship or calendar authorization
is granted by this association. Existing IRC account verification,
calendar permission gates and Core/router ABI remain unchanged.

The implementation is intentionally limited to first-name discovery
and avatar-UUID linkage. Email/phone collection, richer demographics,
cross-transport identity proof, permission grants and task management
remain distinct unimplemented functions. The MariaDB contact service
performs conservative runtime schema inspection and may fail against
unrecognized existing column layouts. GitHub source changes alone
do not establish native build, actual database writes or operator
acceptance.

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

## Social conversation and DM regression correction (v1.0.17)

Observed operator dialogues with Laurie exposed duplicate "I've replied to you privately" acknowledgments, literal echoes, misreading questions about Laurie as peer-relay instructions, and unnecessary calendar extraction on ordinary social turns.

Ollama v1.0.17 sends a single DM when retrieved private memory affects its answer and does not also return an IRC route acknowledgment. This works for both channel-origin and already-private conversations without changing shared ABI. Model response text identical to the user's message is rejected rather than echoed; no scripted replacement text is generated. Calendar extraction is skipped for ordinary messages without a recognizable scheduling signal, preserving the established explicit-confirmation scheduling workflow. The normal-generation system instruction now emphasizes social fluency, playful but respectful tone, speaker intent, brevity, feedback, and non-intrusive behavior.

These are source-level changes. Actual model response quality, conversational initiative, and long-term memory recall require operator-observed end-to-end qualification. Do not treat a green module activation as proof of social competence.

## Short-term dialogue continuity (v1.1.0 source)

An operator-observed private conversation using **sparkles-executive:v0.3.1 exclusively** showed generic greetings and literal responses to playful feedback. Prior Ollama chat requests included the current message plus optional MariaDB observations, but did not reliably include the immediately preceding two-sided conversational turns. In particular, short replies such as `yayy` were deliberately excluded from durable observation storage; the model therefore lacked the immediate exchange needed to interpret follow-up teasing.

Ollama v1.1.0 now keeps **two completed human/Sparkles turns per verified account** in a bounded, in-process recent-dialogue buffer and supplies them as labeled context on subsequent ordinary conversational generation. Each side of a turn is capped at 160 bytes, with up to eight account slots and 15-minute idle expiry. A module restart clears it. The controller stores a turn only after successful reply delivery. Entries from one verified account are never used for another account. When prior private conversation informs an answer, the reply is delivered privately; no duplicate acknowledgment is generated.

This is immediate conversational context, **not** a replacement for MariaDB's long-term memories or for independent account verification. The source leaves the operator-trained model, Core, shared ABI, scheduling approval gates, MariaDB data, and IRC transport unchanged. Offline qualification includes five module-side checks for continuity, isolation, two-turn rollover, and clearing. They are source tests only; **native compilation, qualification, deployment and live social-quality improvement have not been verified**. The outstanding operator test is the original affectionate DM exchange, followed by an account-isolation test, using the actual installed model.

**Version numbering:** The pre-existing module source identified itself as v1.0.17, which exceeds STN-LABZ's permitted revision ceiling of 10. This increment uses the next compliant minor-series number v1.1.0 rather than extending the invalid revision sequence. This records a source-label correction, not an independently accepted release.

## Verification

Source changes have been committed to GitHub. Native compilation, module activation, and model inference remain subject to operator verification.

## Inference limits (v1.0.14)

The Ollama request uses `num_predict=512`, `CURLOPT_TIMEOUT_MS=120000L` (120 seconds), and the unchanged connection timeout `CURLOPT_CONNECTTIMEOUT_MS=1000L` (1 second). These are source constants, not environment variables.

**Known limitation:** `sparkles_llm_response_t.text` remains 512 bytes and Core/IRC replies are also limited to 512 bytes. A longer generated response can exceed the output buffer and cause response parsing to fail; increasing `num_predict` does not by itself permit 512 tokens to be delivered over IRC. The output-size contract and multi-message handling require a separate authorized change. Timeout and answer consistency require operator retesting.
