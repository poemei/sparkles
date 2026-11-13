# Sparkles — Changelog

## 2026-11-12 — DB-driven group registry for Second Life (source only)

- SL v0.3.8 replaces its fixed configured group/operator records with a read-only MariaDB group resolution service. Secure SL config now accepts only its original four login fields; group slots and configuration-driven auto-start are removed.
- MariaDB v1.9.7 registers \`sparkles.mariadb.sl_groups.resolve\`: prepared name lookup through \`sl_groups\` → \`business_groups\` → \`businesses\` gated by the simulator-verified human's avatar UUID matching the linked business's \`sl_owner_uuid\`; group and business must both be active and the group chat-enabled.
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
