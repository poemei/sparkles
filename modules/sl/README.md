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
