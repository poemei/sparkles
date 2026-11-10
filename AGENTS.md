# Sparkles repository engineering instructions

## Authority and scope

The human developer directs Sparkles development. A request to review, investigate, or propose does not independently authorize unrelated changes, deployment, or activation. Work only within the requested increment. Never represent untested source as operational.

Before development, read README.md and docs/ROADMAP.md along with applicable STN-Labz engineering instructions and Module Creation Guide.

## Architecture

Preserve the established STN-Labz Core/router/module/transport split and externally maintained shared ABI. Transport modules handle protocol only; Core manages lifecycle and routes to registered modules; Ollama provides inference; MariaDB is an authorized contextual record source. No extra conversational routing chain or hardcoded conversation scripts without explicit human instruction.

Sparkles is an independent executive-assistant agent, not Rictus renamed. Do not import Rictus-specific control, threat-intelligence, supervisor, or governance implementation unless explicitly requested.

## Evidence and changes

Inspect actual source and dependencies before a correction. Distinguish implemented, built, tested, active, operator-observed, and accepted. Do not claim one stage proves another.

Do not create GitHub branches or workflows. Preserve the original Rictus repository and its history unchanged. For source corrections, follow the requested complete-file workflow and report affected files, build/tests, limitations, and commit status.

## Acceptance

Automated checks do not substitute for operator testing or acceptance. Stop after the requested increment and report unverified behavior accurately.
