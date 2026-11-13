## v0.3.9 — Show confirmation and official Second Life group notices (GitHub source only)

Sparkles' dedicated show timer checks the MariaDB `sparkles.mariadb.shows.claim`
service every three seconds **only after simulator-confirmed avatar presence**.
The MariaDB scheduler selects registered public shows at **T−60 minutes**
(for a private owner check-in) and **T−30 minutes** (for **real** group notices,
not group-chat IMs). It uses one-minute due windows; if Sparkles is offline
for an entire window, the task is missed rather than sent misleadingly late.

The owner check-in is sent as a private dialog-0 IM. Reply exactly
`show yes` to record confirmation or `show cancel` to suppress further
automatic notices for this show. Cancellation in this workflow does not
cancel the original calendar appointment. If no answer arrives, a scheduled
show's notices proceed unless the show is explicitly cancelled.

Notices use `ImprovedInstantMessage` dialog 32, the group UUID as the
recipient, a null IM conversation ID, the `subject|message` format, and
a one-byte empty attachment bucket. The encoder enforces the combined
510-byte limit and rejects unescaped `|` in notice input. Only groups
selected through **active** business/group links, `entertainment_enabled`,
`notices_enabled`, and the matching SL owner UUID are targeted.
Sparkles still needs group membership and the Send Notices role permission.

A unique MariaDB claim records *one outbound attempt* per registered show,
stage and group. The attempt state `queued` does **not** establish delivery.
UDP ACKs do not establish publication, group notice archival, or reception.
Failed or unverified sends are not silently re-sent because notices cannot
be deleted. Operators must reconcile uncertain delivery manually.

The show scheduler is **inactive until the operator runs** the migration
`modules/mariadb/sql/20261112_show_coordination.sql` and explicitly maps
public shows into `show_events`. Ordinary appointment records are never
assumed to be public shows. Existing group chat, private IM and normal
calendar appointments remain separate. Database timezone must match the
intended Second Life show timezone. No migration, install, live grid
qualification or delivery has been performed by this source commit.

## v0.3.8 — Database-driven Second Life groups (GitHub source)

Group UUIDs, names and operator identity are no longer read from
`/opt/sparkles/auth/sl/sl.conf`. This secure file accepts only
`first`, `last`, `password` and `start`; legacy group/operator keys
are rejected. Leave your existing login credentials unchanged.

For **DM-directed group initiation**, the SL dispatcher forwards the
simulator-verified private-IM account and the human-mentioned group name
to the new MariaDB `sparkles.mariadb.sl_groups.resolve` service.
MariaDB resolves the group dynamically through the operator-populated
`sl_groups`, `business_groups` and `businesses` tables. There is no
two-group restriction and no hardcoded destination. The request is authorized
only if the group is active with `chat_enabled=1`, linked to an active
business with `sl_owner_uuid` equal to the verified requesting avatar UUID.
If `sl_owner_uuid` is NULL or wrong, the group is unknown, duplicated,
unlinked or inactive, the group operation fails closed.

**Existing-session behavior:** Incoming Second Life group-chat invitations
continue to be accepted and conversation replies remain available; the
DM-directed group start and session-confirmed outgoing message flow is
unchanged. The old `sl.conf`-driven automatic primary-group startup was
removed; group chat is now started on demand by an authorized DM or by an
incoming invitation, **not** proactively for every active database group.

The MariaDB business owner UUID must be populated for the business linked
to each operator-directed group. To inspect (read only):

```sql
SELECT b.id, b.business_name, b.sl_owner_uuid,
       g.id AS group_id, g.group_name, g.group_uuid,
       g.chat_enabled, g.is_active, b.is_active AS business_active
FROM businesses b
JOIN business_groups bg ON bg.business_id=b.id
JOIN sl_groups g ON g.id=bg.group_id
ORDER BY b.id,g.id;
```

This increment does **not** implement show schedules or group notice publishing.
GitHub source changes have not been installed or live-grid tested.

## v0.3.7 — DM-initiated named group conversation (GitHub source, 2026-11-12)

This increment registers the typed `sparkles.sl.group.start` service without altering Core's shared ABI. Only the SL dispatcher **while processing an authenticated one-to-one IM from the explicitly configured operator UUID** may enqueue a group opening. Group chat, nearby chat, IRC and accounts merely resembling an SL identity cannot invoke the operation. The service resolves a human-readable group alias to an explicitly configured UUID; the LLM never supplies arbitrary UUIDs.

Add the optional entries below to the existing UID-owned, mode-0600 `/opt/sparkles/auth/sl/sl.conf`, in addition to its existing first/last/password/start fields (use real avatar and group UUIDs; **do not commit or post credentials**):

```ini
operator_uuid=<authorized-human-avatar-uuid>
group_chat=<primary-group-uuid>
group_name=My Group
group_chat_2=<stn-labz-group-uuid>
group_name_2=STN-Labz
```

The second group mapping is optional. An existing `group_chat` without `group_name` continues to work for auto-start but is not a named DM-action destination. The optional operator UUID is required to enable DM-directed group operations; omission leaves this new action disabled.

Example private IM: `Sparkles, open the STN-Labz group chat and tell everyone we're having a meeting at 7 PM.` Ollama proposes a typed alias and opening text; the transport verifies the verified sender and configured mapping, queues a group-start packet, waits up to 30 seconds for a matching simulator `ChatterBoxSessionStartReply` success event, then posts the opening through the group session using dialog 17. If that session is already confirmed joined through the capability worker, it queues the opening directly without restarting the session. On rejection/timeout the opening is **not** sent. The initial private acknowledgment says only that a request was queued; it does not claim the group saw the post. Only one pending group-start action is supported at a time; overlap can be rejected by the transport. Existing private/group reply capability remains unchanged.

The expected SL outcome still requires native compilation/tests, installation under operator control, and a real group-chat test. A simulator UDP ACK confirms only transport receipt, not display in the chat window.

## Group-chat delivery correction — v0.3.6 (2026-11-12, GitHub source only)

- The native SessionSend (dialog 17) and group-start (dialog 15) encoder now includes the viewer-compatible **one-NUL-byte binary bucket** rather than a zero-length bucket; private IM dialog 0 retains its previously tested framing.
- Outgoing group messages log their dialog and socket-send attempt separately from private IM. A reliable simulator UDP ACK is reported as **transport acknowledgment only**, never as proof of delivery to another resident's group window. Retries, encoding failures and queue drops are group-specific.
- Simulator `ChatterBoxSessionEventReply` denial events are now surfaced with bounded event/reason diagnostics instead of silently ignored. Group-context observation success is logged as `channel=session` rather than incorrectly as `nearby`.
- Native codec regressions now inspect the session/group-start bucket lengths and bytes. LLSD event regression includes a rejected session event.
- **Status:** source-only; native build, acceptance by Linden Lab simulator, and message appearance in both SL groups remain to be confirmed. An HTTP 500 from EventQueueGet is not itself proof of outgoing message failure.

## Group chat capabilities — 0.3.5 (GitHub source, 2026-11-12)

- After simulator-confirmed avatar presence, a dedicated native C worker negotiates `EventQueueGet` and `ChatSessionRequest` from the login-issued HTTPS seed capability. It polls LLSD events independently of the UDP socket, acknowledges event IDs, retries the poll after expected 502 idle responses, and stops with the simulator session.
- Text `ChatterBoxInvitation` events now send `ChatSessionRequest` with `method=accept invitation` and the grid-issued session UUID. Successful HTTP responses mark an invited session as joined; unsuccessful invitations are not passed to Core as joined conversations. Voice and unrecognized event types are not joined.
- Group start confirmations and forced session closes are recognized. `ImprovedInstantMessage` dialog 17 continues to carry group/conference session chat and replies.
- **Operator-selected primary group:** Add optional `group_chat=<Second Life group UUID>` to the UID-owned, 0600 `/opt/sparkles/auth/sl/sl.conf` (alongside existing first/last/password/start fields). On simulator presence, Sparkles requests an ordinary group chat session using reliable `IM_SESSION_GROUP_START` (dialog 15) and waits for the server's start confirmation. If omitted, group sessions are entered only through received invitations. Group membership for house access is distinct from a joined chat session. The server is responsible for membership verification.
- Group speech remains attributed to simulator speaker UUID in `conversation_context`, addressed messages route into session-only Ollama inference, and private schedules, private memory, contacts and slash-command tools are not exposed in group chat. No automated group notice publishing is added.
- Validation includes offline LLSD queue parsing, group-start and force-close cases, optional group UUID configuration, and group-start packet encoding. These tests were added to `make -C modules/sl clean all test`.

**Status:** GitHub source committed, no native compile or simulator/group-chat operator verification performed in this session. Capability endpoints and group-session messages require real-grid acceptance testing. No credentials or session URLs are written to source.

**Group/conference session message increment (v0.3.4, 2026-11-12; GitHub source only):** Incoming simulator `ImprovedInstantMessage` dialog 17 can now be distinguished from direct IM dialog 0, including session UUID and speaker UUID. All accepted session speech is recorded as a `group_chat` observation through MariaDB, attached to the simulator-verified speaker identity; unaddressed session messages receive no automatic reply. Messages explicitly addressing Sparkles are dispatched under a synthetic **session-only** account, never a participant's private account, so private contact and calendar tools cannot run. Reply encoding uses dialog 17 with the session UUID as destination. Other dialogs (invitation controls, notices, inventory) are not interpreted as conversational text. The inbound queue capacity was raised to 128 to tolerate more multi-person traffic; overflows still log. **Important limitation:** the native SL transport does not yet poll `EventQueueGet` nor POST `ChatSessionRequest` to accept a newly invited ad-hoc conference. Therefore an invitation to Sparkles may not become an active session; actual joined-session reception, group delivery, group notices, and live operator testing are not qualified. No source compilation or installation has been verified.

**v0.3.3 context persistence diagnostics (2026-11-12, source only):** Every SL observation attempt logs whether MariaDB confirmed a saved row, along with the source channel (private/nearby). Failure logs contain service return code and response size but not message text. These logs can distinguish inactive/mismatched module versions from rejected storage after live installation; GitHub source alone cannot prove capture. No deployment or acceptance performed.

**Second Life conversation observation ingress (v0.3.2, 2026-11-12, GitHub source only):** The simulator UDP `ChatFromSimulator` Low 139 decoder accepts fully audible whisper/normal/shout from other avatars, and excludes own speech, objects, system messages, typing, debug and malformed content. Nearby chat is queued as an observation, never passed to the conversational reply router. Every accepted direct private IM is recorded through the MariaDB context exchange service *before* it is dispatched to Core, including short messages and requests handled without model generation. An unavailable context service or saturated queue produces a content-free diagnostic, not a false assertion that memory was saved. Source change is not native-built or SL live-qualified.

# Sparkles Second Life transport — native C v0.3.1

This native C11 STN-Labz module runs inside Sparkles Core, preserving the
shared ABI. No C++, C#, .NET, Corrade executable, external bot service,
or changes to IRC/MariaDB/Ollama/Core are introduced.

## Implemented source: grid and direct IM

The earlier v0.2.2 grid worker is retained: secure private credential
loading, legacy XML-RPC login, simulator UDP circuit handshake, reliable
packet acknowledgment/retransmission, region handshake/pings,
AgentMovementComplete presence marker, idle AgentUpdate and reconnect.

Version v0.3.0 adds one-to-one **ImprovedInstantMessage** transport
(message template Low 254, Dialog 0):

- The SL grid network thread decodes and validates bounded inbound IM
  payloads on its connected simulator socket. Only personal dialogs
  with FromGroup=false, a nonzero third-party sender UUID and
  ToAgentID equal to the login-issued avatar UUID are considered.
  Object, group, conference, inventory and control dialogs are ignored.
- Messages pass to a bounded, dedicated Core-dispatch worker through
  the existing `sparkles.router.dispatch` ABI 1.5 service. The request
  account is `sl:<sender UUID>`; it cannot be substituted with an IRC
  nick or caller-supplied account string.
- Valid nonempty Core replies are queued to the SL network thread,
  which sends a reliable outbound ImprovedInstantMessage to **the
  same UUID** and original conversation ID, with MsSparkles'
  authenticated account name supplied by the login configuration.
- Outbound messages have bounded packet size, an eight-message
  outstanding window, retry handling and simulator ACK tracking.
  Logs distinguish accepted ingress, Core no-reply, outbound send,
  and exhaustion of simulator acknowledgments.
- Queues are cleared and in-flight Core responses invalidated at each
  grid reconnect, preventing a prior session's pending reply from
  being delivered on a later circuit. Core inference never blocks
  simulator UDP receive/ping/ACK processing.

**Limitations:** A simulator ACK is not proof that the recipient viewer
displayed a reply. The v0.3.0 packet path is untested against a live
viewer and might require further protocol interoperability fixes.
Second Life may deliver some IM traffic through `EventQueueGet` or
other capabilities; no CAPS EventQueueGet implementation exists yet.
Group/conference/local chat, friends, teleport, inventory and
full executive SL capabilities are not implemented. Do not claim a
complete SL chat client from module ACTIVE status.

## v0.3.1 message template compatibility fix

The operator's 2026-11-12 two-message direct-IM test established that
avatar presence continued but no private IM reached Core. Inspection
against the Second Life viewer `ImprovedInstantMessage Low 254`
message template showed an important omitted trailer: current messages
carry a fixed 4-byte `EstateBlock.EstateID` and a variable
`MetaData` block (count byte and bounded 2-byte-sized data entries)
**after** the `BinaryBucket`. The v0.3.0 parser insisted that the
payload end directly after `BinaryBucket`, silently rejecting modern
packets. v0.3.1 accepts both legacy packets and strictly bounds and
validates the contemporary trailer. Outbound private IMs now include
the estate/metadata fields as prescribed by the current template.

Content-free diagnostics distinguish an `ImprovedInstantMessage UDP
packet received` from `IM packet rejected: reason=...`, from
`private IM accepted into Core dispatch queue` and the outbound
send/ACK milestones. No IM text, sender name or credentials are
logged. New offline regression cases cover optional trailer presence,
metadata block contents, bad lengths, wrong recipients, and excluded
IM dialogs.

**Limit:** Not every live Second Life IM is guaranteed to arrive as
UDP Low 254; some routes depend on CAPS/EventQueueGet, which remains
unimplemented. This change is SOURCE COMMITTED but has not been
compiled, hotloaded or confirmed in the live simulator by the operator.

## Account configuration

The sole operator-owned credential file is:

`/opt/sparkles/auth/sl/sl.conf`

Expected exactly four `key=value` entries (operator supplies real values):

```ini
first=MsSparkles
last=Resident
password=YOUR_PRIVATE_PASSWORD
start=home
```

The file must be a regular 0600 file owned by the running module's
UID. No secrets should be entered into chat or committed to GitHub.
Additional keys (including obsolete `mac` and `id0`) are rejected.
The login code supplies legacy hardware-identifier fields internally
as neutral values; compatibility with SL's current login endpoint is
not assured. Existing login failure diagnostics deliberately redact
secrets and raw server responses.

## Build and qualification

```sh
git pull --ff-only
make -C modules/sl clean all test
```

The `test` target runs secure configuration, SL wire, and private IM
codec regressions. Hotload qualification retains the Core ABI minimum
of 10 tests and now includes private IM outbound construction checks.
These are **offline** checks; they do not prove live IM delivery.

Operator-controlled installation:

```sh
sudo make -C modules/sl install
```

Use the existing Core module hotload mechanism to activate the new
binary; do not reboot the server. After operator activation, inspect:

```sh
journalctl -u sparkles -b --no-pager | grep -F '[SL]'
```

Useful milestone messages include `private IM accepted into Core dispatch
queue`, `private IM reply sent; awaiting simulator acknowledgment`,
and `private IM reply unacknowledged; retry limit reached`.

**Evidence:** v0.2.2 grid presence was visually observed by the
operator. v0.3.0 private IM source is committed to GitHub only; its
build, tests, installation, live simulator IM reception, Core routing,
outbound delivery and operator acceptance remain **unverified**.

## Next increments (not covered by v0.3.0)

SL CAPS/EventQueueGet, group and conference messaging, local chat,
friend/presence integration, teleport and inventory are separate
features. Core retains all executive decisions and service authority.
