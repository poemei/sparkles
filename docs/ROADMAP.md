# Sparkles — Engineering Roadmap

**Project:** Ms. Sparkles, STN-Labz Executive Assistant  
**Status:** Core and local Ollama terminal integration operator-observed; broader agent capabilities remain unqualified  
**Evidence updated:** 2026-11-10  
**Authority:** Human-directed development only

A roadmap is not permission to implement future stages. Source implementation, build, module qualification, activation, observed behavior, and operator acceptance are separate gates.

## Stage 0 — Independent Core baseline

- Verify the copied source tree is complete, including required `src/`, `include/`, module files, Makefiles and external ABI dependency.
- Change Rictus-specific naming, executable, install prefix, paths, service identifiers and module interfaces consistently, preserving the ABI.
- Replace Rictus identity and mission configuration with Sparkles' separate executive-assistant identity.
- Exclude Rictus-only compliance/control broker, governance records, operational intelligence and supervision from Sparkles' installation and activation.
- Preserve generic Core lifecycle, routing, module management and logging.
- Verify a native WSL2 build and installation, module qualification and initial runtime in operator environment.

**Observed on 2026-11-10:**
- Sparkles launched interactively from `/opt/sparkles/bin/sparkles` and displayed module registry status, welcome text, and the `You:` prompt.
- Identity module `v1.1.3` reported `ACTIVE`. The operator verified `/identity who`, `/identity mission`, name-addressed identity questions, and correctly spelled General Order questions. A misspelled `yur` query was not recognized.
- Existing terminal input and Core routing successfully returned Identity responses. Core also displayed real registry-derived module status.
- Ollama `v1.0.11` was detected and qualification-green but failed activation when its startup required an unset environment variable; Core denied the failed candidate. Source was subsequently corrected to select the already installed model without requiring exports, and the module revision became `v1.0.12`.
- Later operator terminal tests demonstrated successful Ollama generation and returned an inference reply through Sparkles Core, confirming an operational local inference path. The transcript did not include a subsequent full startup-status printout for `v1.0.12`, so its displayed registration/version is not independently evidenced here.

**Status:** Core startup, registry status display, Identity module interaction, and terminal-to-Ollama inference are operator-observed. Full Core/module/ABI qualification and broader installation acceptance remain separate.

## Stage 1 — IRC and inference

- Bring up IRC transport through existing Core routing.
- Use local Ollama for conversational inference, initially the Qwen3 4B Instruct base; integrate a trained model only after packaging and qualification.
- Preserve sender, reply destination and independently authorized peer-action destination.
- Avoid scripted/canned conversational replies.

**Observed on 2026-11-10:**
- Operator-reported local Ollama models included `qwen3:4b-instruct` and `qwen2.5-coder:3b`. The Ollama module's source selects `qwen3:4b-instruct` with no environment-variable export, and uses the local Ollama HTTP API.
- The first terminal request to explain RAM versus storage returned `curl=28 http=0` (timeout, no HTTP status). A later request about executive-assistant capabilities completed successfully, followed by a successful retry of the RAM-versus-storage explanation. A cold-model-start delay is plausible but not confirmed.
- The observed RAM explanation ended mid-sentence; the existing `num_predict=96` generation limit is a possible contributor, but truncation cause has not been isolated.
- The model's fluent description of scheduling, email, meetings, tasks and follow-up is generated text, not proof of implemented executive integrations.
- A training progress snapshot reported `321/364` steps, loss `1.87003 ± 0.11944`, accuracy `42.19 ± 0.76%`, and ETA `02:20:08`. This snapshot does not establish completed training, model identity, validation quality, packaging, or deployment.

**Outstanding:** Qualify cold-start and repeated-request reliability; investigate incomplete output; verify final training and validation results; package and qualify any trained model before integration; bring up and test IRC transport; verify routing and destination-preservation behavior across transports.

**Status:** Local terminal inference operator-observed with latency and output-length issues. IRC, trained-model integration, and full Stage 1 qualification remain outstanding.

## Stage 2 — MariaDB context

- Implement authorized persistent context for resident relationships, permissions, interactions and schedules.
- Separate model inferences from authoritative records and confirmed edits.
- Validate record permissions and positive/negative conversational scenarios.

**Status:** Not implemented or verified in this record.

## Stage 3 — Executive assistance

- Scheduling and conflict resolution, natural language intent discovery, multi-party coordination and authorized proactive follow-through.
- Verify decisions using human-directed qualification scenarios.

**Status:** Not implemented or verified in this record. A model-generated statement about these capabilities is not acceptance evidence.

## Stage 4 — Second Life transport

- Integrate the intended avatar with the existing transport boundary.
- Qualify in-world operation through operator-observed end-to-end tests.

**Status:** Future.

## Acceptance

Source edit, build, module qualification, hotload, client behavior and operator acceptance are separate evidence gates. No source file is accepted solely because it was committed. The operator-observed terminal behavior above does not imply IRC, MariaDB, executive actions, Second Life, or a trained model have passed their respective acceptance tests.
