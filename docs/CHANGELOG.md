# Sparkles — Changelog

## 2026-11-26 — Executive communications, scheduling, private reminders (GitHub source only)

- **SL 0.4.6–0.4.8:** A simulator-authenticated private IM that receives an invalid or failed Core response now generates a bounded, noncommittal private failure acknowledgment instead of silently disappearing. The acknowledgment explicitly avoids asserting whether a database operation committed. Group and nearby chat are not sent this private failure reply.
- **Ollama 1.3.1–1.3.3:** A timed appointment request that fails structured calendar extraction now receives a scheduling-specific failure response rather than falling through to another generic inference. Hosting show metadata recognizes the operator-observed variants where clock/date precede the venue and where `with DJ NAME at VENUE` follows the time. Added model-module qualification cases for those structures. Contradictory explicit dates and relative `today`/`tomorrow` expressions are rejected for clarification before a calendar mutation. The HTTP request deadline is now a bounded 420 seconds to admit an unusually slow cold on-demand model load; no permanent model residency, model replacement, Core change, or automatic calendar write was introduced. A cold start may still delay other requests because the SL dispatcher is serial.
- **MariaDB 1.10.6 and SL 0.4.8:** Added separate private calendar-reminder claim/attempt services (without changing shared Core ABI). A verified Second Life contact must be an **accepted** appointment participant. An operator-run additive SQL migration creates `appointment_reminder_attempts`, with a unique appointment/avatar ledger to prevent duplicate automatic sends. The existing SL presence-gated timer attempts a private reminder in the **45–60 minutes before start**, interpreting the stored appointment as America/Los_Angeles wall time; registered public shows remain on their separate show reminder/notice path. A queued IM is not confirmed delivered. Missing reminder service or migration conditions now produce throttled SL diagnostics. Pending/declined guest associations are excluded. Database timezone tables must support `CONVERT_TZ`, and migration must be applied by the operator before the new service can claim reminders.
- Source also updates the two native module Makefiles to track the reminder header. No deployment, database migration execution, native build, automated tests, installation, hotload activation, end-to-end IM verification, or operator acceptance was performed by the assistant. Runtime behavior and the precise cold-load hardware bottleneck remain unverified. The new general reminder defaults are source policy pending human qualification, not a claim of approved operating parameters.


## 2026-11-22 — Second Life touch signs: executive welcome and entertainment group links

- **Executive welcome, native SL 0.4.3 (GitHub source):** repurposed the existing `STNLINK1|visitor_uuid|escaped_display_name` object-IM handler from entertainment links to the exact operator-supplied Sparkles Executive AI Assistant welcome. Preserved visitor cooldown, private outbound IM transport, and the established command; removed the old group-membership request branch from this sign workflow. Commits: `257e9cf36cd2badb49d35eedd6ddcc47b5c3df26`, formatting correction `3d6178192c2d819e12146cf3754bfbd3c6c64aaa`.
- **Hotload revision:** a supplied `MODULE HOTLOAD_DENIED ... version_not_incremented_or_regressed` line exposed an unchanged SL descriptor. Advanced the source version from 0.4.2 to 0.4.3 in `4ef7d0ee73f1e8b700edb2672257bf68db3b8b20`. This log alone did not identify the installed descriptor or prove a successful hotload.
- **Executive welcome formatting, native SL 0.4.4:** inserted paragraph breaks while preserving the operator-approved wording and `STNLINK1`. Commit: `f86a1b2d539cf6f330572a70d9d0cd4a9b603bba`.
- **Entertainment sign, native SL 0.4.5:** retained **one command** (`STNLINK1`) and added an optional trailing `|links` *intent field* to select a separate Sparkles Entertainment / Team Sparkles introduction with clickable group links; the original two-field request continues to select the executive welcome. The parser, request structure, native dispatch, and parser regression cases were updated. Source commits: `b4eb50099a823629e87f60909c38a58f0a05a5b0`, `c77fe605069363b282e1f900782fc09193835922`, `4c66594154a51bdffa4e8179edded87ce086abe0`, `4c86d4b6171fae91962def2d8706d93774f96657`.
- **Proprietary LSL (not committed):** Executive Welcome Sign v2.2 sends `STNLINK1|UUID|escaped_name`; Sparkles Entertainment / Team Sparkles Group Introduction Sign v2.3 sends `STNLINK1|UUID|escaped_name|links`. Both signs use object-to-Sparkles `llInstantMessage` and expect Sparkles' native module to send visitor-facing private IMs. The sign holds no login credentials.
- **Operator observations:** the operator supplied a live Sparkles IM transcript showing the exact executive welcome on 2026-11-22, then subsequently confirmed the revised group-introduction sign was fixed after an earlier test returned the executive welcome instead. These are operator-reported live functional successes for the two sign interactions, not blanket qualification of every SL service, transport behavior, group invitation, or future delivery.
- **Qualification limits:** GitHub source readback confirmed the 0.4.5 code paths; no native build/test log, deployed descriptor, final hotload success log, or explicit group-link IM transcript was supplied here. LSL remains outside GitHub. No Core, shared ABI, MariaDB, Ollama, or unrelated module changes were part of this increment.


## 2026-11-12 — Show timer and official SL notices (source only)

- Ollama 1.2.8 and MariaDB 1.10.1 additionally register new, explicitly
  identified public DJ-hosting shows atomically with the confirmed calendar
  appointment. The existing November 13 appointment requires a one-time
  operator-verified registry backfill. The private MariaDB typed create
  request structure changed; rebuild both modules together.



- MariaDB 1.10.0 adds explicitly registered public shows and a unique,
  persistent at-most-once attempt ledger. It claims due T−60 owner
  check-ins and T−30 eligible group notices, records check-in answers and
  whether an action was queued. Existing appointment and group tables
  remain authoritative.
- SL 0.3.9 adds a simulator-presence-gated timer, a private IM
  check-in response path (`show yes` / `show cancel`), and native
  dialog-32 group notices with bounded `subject|message` format. A notice
  is queued once, not claimed delivered or acknowledged as published.
- New migration must be applied and the specific public show linked to
  the saved calendar appointment; no generic calendar entry is public by
  default. Group membership, role rights, timezone, tests, deployment and
  live delivery remain operator-verification gates.


## 2026-11-12 — Show scheduling role correction (source only)

**v1.2.7 — Show scheduling guest-role correction (GitHub source only):** When a show description says `with Manon as my DJ` (or another explicit `as my/the/our DJ/host/performer/singer` role), the performer is preserved as event narrative, not treated as an invited appointment participant requiring a separate `contacts` row. An ordinary `meeting with Manon` still requests guest participation. The appointment checker now reports a denied identity separately from a conflict or an unavailable/failed service, and writes response codes (no contact names or message text) for diagnosis. This does not bypass verified contact/calendar authorization, save without a human confirmation, or implement event notices. Source-level regressions cover staff-vs-guest classification. Native build, SQL behavior and operator acceptance remain unverified.

## 2026-11-12 — DB-driven group registry for Second Life (source only)

- Ollama v1.2.6 updates group-start failure messaging to refer to the linked database business/group relationship rather than legacy `sl.conf` settings.


- SL v0.3.8 replaces its fixed configured group/operator records with a read-only MariaDB group resolution service. Secure SL config now accepts only its original four login fields; group slots and configuration-driven auto-start are removed.
- MariaDB v1.9.7 registers `sparkles.mariadb.sl_groups.resolve`: prepared name lookup through `sl_groups` → `business_groups` → `businesses` gated by the simulator-verified human's avatar UUID matching the linked business's `sl_owner_uuid`; group and business must both be active and the group chat-enabled.
- Existing inbound group session invitations, ordinary group response encoding and authorization of private IM dispatch remain. Startup auto-join for a single configured primary group is no longer automatic; group starts are on demand or invitation-based.
- Only repository source changed; no database row edits, installation, source compilation, real SQL query or live SL acceptance was executed.



## 2026-11-12 — Ollama 1.2.5 group opening speaker perspective correction

- After operator-confirmed successful group delivery, revise the group-action generation prompt to compose a natural group-facing opening in Sparkles' voice, attribute `from me` to the verified private IM speaker, and preserve explicitly named sources such as `STN 6`.
- Add an inference retry when a forwarded `from me` greeting remains mistakenly first-person; reject the action after the second ambiguous attempt rather than publishing an incorrectly attributed message.
- Add group-action selftests for reported live examples. No changes to group transport protocol, authorization, Core ABI, or deployment. Native and live model behavior unverified.



## 2026-11-12 — DM-directed Second Life group initiation (source only)

- SL 0.3.7: New typed `sparkles.sl.group.start` service, restricted to a simulator-authenticated private IM from an explicitly configured operator avatar UUID. Two bounded operator-defined group name/UUID mappings; no arbitrary model-supplied destination.
- Native SL grid sends to an already confirmed joined session or queues a group-start packet, waits for positive simulator session-start confirmation, then sends the requested opening message. Rejection and 30-second timeout do not post the message. A private acknowledgment indicates *queued*, not delivered.
- Ollama 1.2.4: Explicit natural-language group-chat launch intent and fail-closed strict extraction of named group and opening text before invoking the typed transport tool.
- Existing Second Life IM/group-chat and Core/module split retained. No local installation or deployment. No native tests or live delivery evidence in this increment.



## 2026-11-12 — SL v0.3.6 group-chat framing and delivery diagnostics

- Make session-send dialog 17 and group-session-start dialog 15 include the viewer-style one-byte empty binary bucket while leaving direct IM dialog 0 framing unchanged.
- Distinguish group-session UDP sends, reliable acknowledgments, retries and queue drops from private IM diagnostics; acknowledgments do not prove recipient display.
- Recognize `ChatterBoxSessionEventReply` rejected session operations through the EventQueueGet worker and report bounded event/error details.
- Correct group observation persistence logging to say `channel=session`.
- Extend group codec and LLSD event regressions; no native or live-grid validation completed here.



## 2026-11-12 — SL group event capability and primary-group start (source only)

- SL 0.3.5: Native C HTTPS LLSD seed negotiation for EventQueueGet and ChatSessionRequest, independent cancellable long-poll worker, invitation acceptance, session-start/forced-close lifecycle tracking and group session message ingress.
- Optional `group_chat` UUID setting in SL restricted credential configuration starts an operator-selected group chat (dialog 15) after simulator-confirmed presence; the grid still verifies membership.
- Extended native regressions for LLSD event queue, group start/close, group-start packet and optional configuration field.
- Existing SL group-chat speaker attribution, Core routing, inference-only session namespace and MariaDB observations remain in place.
- **Not yet established:** live simulator acceptance, complete group-member roster or history, all event variants, group notice posting, module installation, operator acceptance and native build on the target machine. No rollout performed.



## 2026-11-12 — SL session group/conference message increment (GitHub source only)

- SL v0.3.4: recognize dialog 17 group/conference session messages and preserve session ID, sender UUID, and destination; encode group replies to the session rather than private IM.
- SL records passive session speech and replies only when Sparkles is explicitly addressed; group slash commands cannot invoke Core commands; inbound buffer raised to 128.
- MariaDB v1.9.6: distinguish speaker-attributed `group_chat` from private IM and nearby public chat using the existing `conversation_context` table.
- Ollama v1.2.3: session namespace isolated from private contact, schedule, and memory tools.
- Extended native SL codec regression cases for dialog 17, session destination, and rejected controls.
- **Not implemented:** Second Life EventQueueGet / ChatSessionRequest conference invitation acceptance and group notice publishing. Thus newly invited conferences may still not work; live SL verification remains required.
- No native compilation, deployment, installation, database write verification or operator acceptance established.



## 2026-11-12 — Remainder-of-day calendar and SL context diagnostics (GitHub source)

- Ollama v1.2.2: recognize `shedule` and `rest of the day`; dispatch to contact-scoped MariaDB instead of free-form model inference; restrict remaining-day display to unfinished appointments.
- MariaDB v1.9.5: optional validated datetime lower bound in appointment-read request, applied in SQL before result limiting; conversation observations are stored independently of a successful history read, with content-free write error diagnostics.
- SL v0.3.3: content-free success/failure log for each private IM or nearby-chat observation persistence attempt.
- Calendar, context table, and UUID authorization boundaries otherwise unchanged. IRC installation not required.

**Verification:** GitHub source only. Native compilation, module qualification, installed version, SQL writes, and Second Life operator acceptance are not confirmed.



## 2026-11-12 — SL conversation_context ingress (source only)

- SL v0.3.2: capture authenticated simulator private IMs and audible nearby avatar chat; passive ambient observations never trigger model responses.
- MariaDB v1.9.3: distinguish SL private `conversation` and shared `local_chat` observations using existing `conversation_context` columns; preserve UUID attribution and unverified status.
- Ollama v1.2.1: avoid duplicate IM writes when fetching speaker memory.
- Added SL chat decode and memory-flag negative checks; did not remove historical IRC rows or require IRC installation.
- No SQL schema changes, deployment, automated native build, live capture test, or operator acceptance established.



## 2026-11-12 — Contact-linked shared appointments (source only)

- MariaDB v1.9.2: exact unique contact lookup for one explicitly named guest; personal conflict checks for creator and guest; atomically insert creator as accepted and guest as pending.
- Ollama v1.2.0: preserve explicit guest through conversational proposal and confirmation; reject multi-guest expressions instead of silently omitting participants; add parser qualification cases.
- Preserve contact-scoped reads, transport identity authentication, and existing unlinked appointments. No database changes or migration performed.

**Verification:** GitHub source changes and readback only. Native compilation, SQL integration, deployed behavior and operator acceptance remain unverified.


## Unreleased — 2026-11-10 — Adopt Ezra Core foundation

- Replaced the inherited Rictus Core source and headers with the corresponding Ezra Core implementation, adapted to Sparkles identity, namespace and paths.
- Imported Ezra's Core module host, activation, discovery, hotload, inventory, policy, mandatory logging, runtime, and legacy module management sources.
- Preserved Ezra's qualified module activation and its module registration ownership hooks, previously aligned with Digit's Core contract.
- Removed Rictus-only DSD Core source and header from the Sparkles executable source tree.
- Kept the separate external shared STN-Labz ABI dependency; no ABI interface change is intended.
- Left the `modules/` source directory unchanged during the Core migration. The copied Ollama model and Rictus-specific module content are not considered Sparkles capabilities.

**Verification:** Source commits were made directly to GitHub. Native Ubuntu build, tests, installation, module hotload, model connection and end-to-end operator acceptance have not been performed.

## Earlier

Earlier repository history includes initial Rictus-derived import and incremental changes before Ezra was designated as the Core foundation. Git history preserves those commits; this changelog records the new direction without claiming that earlier builds passed.
