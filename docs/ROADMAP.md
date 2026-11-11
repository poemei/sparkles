# Sparkles — Engineering Roadmap

**Project:** Ms. Sparkles, STN-Labz Executive Assistant  
**Status:** Core, Identity, IRC and local Ollama conversation operator-observed; Executive v0.3.1 trained and separately tested, not activated in Sparkles  
**Evidence updated:** 2026-11-10  
**Authority:** Human-directed development only

A roadmap is not permission to implement future stages. Source implementation, build, module qualification, activation, observed behavior, and operator acceptance are separate gates.

## Stage 0 — Independent Core baseline

- Verify the copied source tree is complete, including required `src/`, `include/`, module files, Makefiles and external ABI dependency.
- Preserve generic Core lifecycle, routing, module management and logging.
- Verify a native WSL2 build and installation, module qualification and initial runtime in operator environment.

**Observed on 2026-11-10:**
- Sparkles launched interactively from `/opt/sparkles/bin/sparkles` and displayed module registry status, welcome text, and the `You:` prompt.
- Identity module `v1.1.3` reported `ACTIVE`. The director verified `/identity who`, `/identity mission`, name-addressed identity questions, and correctly spelled General Order questions.
- Existing terminal input and Core routing successfully returned Identity responses. Core also displayed real registry-derived module status.
- Ollama `v1.0.11` was detected and qualification-green but failed activation when its startup required an unset environment variable; Core denied the failed candidate. Source was subsequently corrected to select the already installed model without requiring exports, and the module revision became `v1.0.12`.
- Later director terminal tests demonstrated successful Ollama generation and returned an inference reply through Sparkles Core, confirming an operational local inference path. The transcript did not include a subsequent full startup-status printout for `v1.0.12`, so its displayed registration/version is not independently evidenced here.

**Status:** Core startup, registry status display, Identity module interaction, and terminal-to-Ollama inference are operator-observed. Full Core/module/ABI qualification and broader installation acceptance remain separate.

## Stage 1 — IRC and inference

- Bring up IRC transport through existing Core routing.
- Use local Ollama for conversational inference, initially the Qwen3 4B Instruct base; integrate a trained model only after packaging and qualification.
- Preserve sender, reply destination and independently authorized peer-action destination.
- Avoid scripted/canned conversational replies.

**Operator observations on 2026-11-10:**
- SparklesR successfully responded on IRC to an Identity mission request; this exercised IRC -> Core -> Identity -> IRC.
- SparklesR also generated an IRC greeting addressed to digicat (`digicat: hi`), and digicat replied `Hi!`. This demonstrates one successful peer greeting through IRC, not autonomous coordination or authoritative executive action.
- Local Ollama models included `qwen3:4b-instruct` and `qwen2.5-coder:3b`; Sparkles' compiled default was `qwen3:4b-instruct` at the time. The existing Ollama integration had shown timeouts (`curl=28`), short output, and incorrect/conflicting scheduling answers. The source was revised to permit up to 512 generated tokens and a 120-second request timeout (Ollama module v1.0.13); its fixed-size 512-byte response and IRC buffers remain an output-length risk. These changes are separate from any claim that the trained model is active.
- An IRC startup failure (`startup did not reach authenticated channel state`) was also observed during a service run. A subsequent successful IRC exchange does not by itself establish cold-start or reconnect reliability.

**Executive v0.3.1 training and local qualification (operator-reported, November 2026):**
- Training completed on a separate machine with `llama-finetune` using the original Ollama GGUF blob `sha256-85e4a5b7b8ef0e48af0e8658f5aaab9c2324c76c1641493f4d1e25fce54b18b9`, dataset `stn_executive_train_v0.3.1.jsonl`, rank 4, alpha 8, one epoch, and a 10% validation split.
- Training reported best validation loss `1.42736`, saved `executive.lora.gguf.best` and a final `executive.lora.gguf`; 144 LoRA pairs were written. Validation loss is not equivalent to operational qualification.
- The best adapter was merged on the training machine using `llama-export-lora`: `144` adapted tensors merged and `398` tensors written to `executive-merged.gguf`. The operator transferred the merged GGUF to the local machine and imported it separately in Ollama as `sparkles-executive:v0.3.1` (listed at 3.8 GB). The separate test model does not replace SparklesR's active compiled default.
- Standalone Ollama scheduling checks correctly handled partial overlap, back-to-back endpoints, contained meetings, gaps, noon and midnight boundaries, individual 60-minute slots, contiguous free intervals, and a straightforward room-unavailability approval question.
- **Known qualification failure:** in two 15-minute mandatory-break scenarios the model answered `YES` to a prohibited 11:05 AM start after an 11:00 AM meeting, despite correctly explaining the break ran until 11:15 AM. The same or simplified constraints later produced correct `NO` or `Decline` answers. This is inconsistent decision output, not a proven inability to compare times, and the later successes do not erase the failures.
- Formatting was also inconsistent in some otherwise correct responses (e.g., an incorrect cross mark and an unfinished `Step` label). The observed tests were manual, limited, and unautomated; no statistical accuracy or reliability claim follows.

**Outstanding:** Resolve or explicitly guard against inconsistent scheduling authorization; assess repeated-response reliability and output-size behavior; qualify cold-start/reconnect and end-to-end trained-model behavior; obtain human approval before changing Sparkles' active model or introducing executive actions.

**Status:** IRC conversation and identity routing operator-observed; trained model completed, merged, imported and manually evaluated **as a separate Ollama test model**. Stage 1 full reliability/acceptance and trained-model activation are outstanding.

## Stage 2 — MariaDB context

- Implement authorized persistent context for resident relationships, permissions, interactions and schedules.
- Separate model inferences from authoritative records and confirmed edits.
- Validate record permissions and positive/negative conversational scenarios.

**Source increment (2026-11-11):** A standalone `modules/mariadb` v1.0.0 source module has been committed. It uses the existing ABI 1.5 lifecycle and exposes only `sparkles.mariadb.status`; the service verifies a local restricted account connection and all seven existing tables. Credentials are loaded from an operator-owned 0600 local file. Offline parser qualification tests are included. This increment does not implement record reads/writes, identity linking, permission adjudication, automatic logging, chat routing, or scheduling actions. Source exists on GitHub; native compilation/tests, installation, activation and operator acceptance remain unverified.

**Status:** Initial connection/schema module source committed; Stage 2 contextual data operations and acceptance outstanding.

**Read-only appointment lookup increment (source, 2026-11-11):** MariaDB module v1.1.0 adds `sparkles.mariadb.appointments.read`, a private ABI 1.5 service with explicit strict date bounds, prepared SQL, a maximum of eight scheduled records, and an overflow indicator. It is not an IRC/chat feature. Source was committed on GitHub, but native compilation/tests, live database queries, activation and acceptance are **not yet evidenced** for v1.1.0. Current IRC request routing does not carry a verified account identity, so exposing personal schedule data to an IRC user would require a separately authorized authenticated caller/permissions design. There are no record writes and no new Core routes.

**Authenticated IRC read gate (source, 2026-11-11):** IRC transport v1.1.3 now forwards only validated server-asserted IRCv3 `account` tags into the existing Core route account field. MariaDB v1.2.0 adds a fail-closed, locally owned 0600 account allowlist and an explicit `/schedule YYYY-MM-DD YYYY-MM-DD` command. It reads scheduled records in a bounded range and sends appointment details by IRC private message to the requesting nickname; the originating channel receives only a generic acknowledgement. Missing/anonymous accounts are rejected. No write operation or automatic interpretation of free-text schedule requests has been added; this is a direct command, not an LLM scheduling agent. **Source commits are not native build, qualification, activation or operator acceptance evidence.** The internal MariaDB service remains an in-process trusted-module interface without ABI-level module caller identity. No changes to Core/ABI or scheduling database schema.

**Explicit appointment creation increment (source, 2026-11-11):** MariaDB v1.3.0 adds authenticated `/appointment-add YYYY-MM-DD HH:MM HH:MM Title` for same-day event inserts. It validates input, uses a transactional overlap check and prepared INSERT, and returns private confirmation. Existing appointments remain retrievable through `/schedule`; conflicts and unauthorized requests do not insert. This increment does not infer a date or schedule from ordinary conversational text, and it does not implement update/delete, external calendar sync or multi-party authorization. All source changes are GitHub-only; build, activation, operator test and acceptance are outstanding for v1.3.0. The existing verified IRC account allowlist is reused, and no new table or Core/ABI change is needed.

**Conversational scheduling proposal increment (GitHub source, 2026-11-11):** Ollama v1.0.14 now attempts to extract one or two event statements from ordinary conversation, validates a strict structured result, preserves a short-lived proposal bound to the verified IRC account, requests any missing explicit calendar date, and sends a private proposed schedule for human confirmation. Before confirmation, the controller checks overlapping stored appointments through MariaDB's authenticated read-only conflict service; only a positive confirmation invokes MariaDB v1.4.0's typed create service, which rechecks the account allowlist, validates appointment data and performs the transactional conflict check/INSERT. The model is not allowed to write directly. These commits are not evidence of native build, real-model extraction accuracy, live operation, or human acceptance. Natural-language **schedule retrieval** and relative-date timezone resolution are still outstanding; manual date clarification is required, and multi-event writes are not atomic as a group. The conversational extra inference can increase response latency.

**Operator regression and source response (2026-11-11):** With Ollama v1.0.14 and MariaDB v1.4.0 both green and active, an ordinary IRC message naming River and Shenanigans hosting events and requesting a schedule check elicited `I can't check your schedule. I can only relay a message...`. The model extraction stage was not proven successful. Ollama v1.0.15 source separates strict event extraction system instructions from the ordinary peer-relay persona and stops silently falling back to generic chat on explicit schedule-check requests with failed extraction. Deployment and end-to-end acceptance for v1.0.15 remain outstanding.

## Stage 3 — Executive assistance

- Scheduling and conflict resolution, natural language intent discovery, multi-party coordination and authorized proactive follow-through.
- Verify decisions using human-directed qualification scenarios.

**Status:** Executive scheduling reasoning was exercised through a separate test model, with both correct answers and inconsistent authorization decisions observed. No authoritative calendar integration, scheduling action, or human acceptance is established.

## Stage 4 — Second Life transport

- Integrate the intended avatar with the existing transport boundary.
- Qualify in-world operation through director-observed end-to-end tests.

**Status:** Future.

## Acceptance

Source edit, build, module qualification, hotload, client behavior and operator acceptance are separate evidence gates. No source file is accepted solely because it was committed. Operator-observed IRC exchanges and separately imported trained-model responses do not establish full transport reliability, MariaDB context, executive actions, Second Life, live trained-model activation, or final acceptance.
