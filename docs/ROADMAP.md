# Sparkles — Engineering Roadmap

**Project:** Ms. Sparkles, STN-Labz Executive Assistant  
**Status:** Rictus-derived source adaptation incomplete  
**Authority:** Human-directed development only

A roadmap is not permission to implement future stages.

## Stage 0 — Independent Core baseline

- Verify the copied source tree is complete, including required `src/`, `include/`, module files, Makefiles and external ABI dependency.
- Change Rictus-specific naming, executable, install prefix, paths, service identifiers and module interfaces consistently, preserving the ABI.
- Replace Rictus identity and mission configuration with Sparkles' separate executive-assistant identity.
- Exclude Rictus-only compliance/control broker, governance records, operational intelligence and supervision from Sparkles' installation and activation.
- Preserve generic Core lifecycle, routing, module management and logging.
- Verify a native WSL2 build and installation, module qualification and initial runtime in operator environment.

**Status:** Not qualified; incomplete source adaptation.

## Stage 1 — IRC and inference

- Bring up IRC transport through existing Core routing.
- Use local Ollama for conversational inference, initially the Qwen3 4B Instruct base; integrate a trained model only after packaging and qualification.
- Preserve sender, reply destination and independently authorized peer-action destination.
- Avoid scripted/canned conversational replies.

**Status:** Not qualified.

## Stage 2 — MariaDB context

- Implement authorized persistent context for resident relationships, permissions, interactions and schedules.
- Separate model inferences from authoritative records and confirmed edits.
- Validate record permissions and positive/negative conversational scenarios.

**Status:** Not implemented.

## Stage 3 — Executive assistance

- Scheduling and conflict resolution, natural language intent discovery, multi-party coordination and authorized proactive follow-through.
- Verify decisions using human-directed qualification scenarios.

**Status:** Not implemented.

## Stage 4 — Second Life transport

- Integrate the intended avatar with the existing transport boundary.
- Qualify in-world operation through operator-observed end-to-end tests.

**Status:** Future.

## Acceptance

Source edit, build, module qualification, hotload, client behavior and operator acceptance are separate evidence gates. No source file is accepted solely because it was committed.
