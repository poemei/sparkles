# Sparkles Marketing HUD — manual Post integration (source stage)

This folder contains `Sparkles_Marketing_HUD.lsl`, a Corrade-free owner HUD.
The previous PHP/Discord/Corrade dispatcher is **not** used.

## HUD inventory

Create a notecard named `sparkles-hud.cfg` in the HUD inventory:

```text
endpoint|https://<operator-controlled-HTTPS-host>/notice
token|<independent-HUD-token>
```

Do **not** commit a real token, reverse-proxy credentials, or a private
notecard to GitHub. The token must match the local Sparkles
`/opt/sparkles/auth/sl/notice.conf` configuration, whose existing secure
file ownership and permissions checks remain in force. The matching
`operator_uuid` and `object_uuid` must be those of the actual HUD owner and
HUD object. The `object_uuid` can change if the object is recreated; an
unmatched object fails closed.

The HTTPS host must be operated and configured by the human operator. The
Sparkles native notice listener binds only to `127.0.0.1:17777` and accepts
`POST /notice` with `X-Sparkles-Object-Token`; it is **not** directly
reachable from an in-world HUD. This source increment does not install
a public reverse proxy or set an HTTPS endpoint.

Keep `venues.cfg` (same original five-column syntax):

```text
# alias|venue name|slurl|landmark reference|enabled
```

The fourth column is carried to Sparkles as the exact landmark inventory
name. **It is not yet looked up or attached**. The River entry should retain
`The River ~ Rock Music at its BEST` exactly. An SLURL is notice text,
not an attached inventory landmark.

Keep `approved_groups.cfg` in the HUD inventory:

```text
# set|group name|enabled|delay seconds
events|Sparkles Entertainment|1|10
events|Poe's Peeps|1|10
events|Team Sparkles|1|10
events|The Abyss Club VIPs|0|10
```

The HUD also accepts equivalent `all` records. The `Set` menu selects
`events` (default) or `all`. Only enabled records for the selected set
are submitted, in file order, separated by their configured delays
(measured after each local queue acknowledgement). MariaDB **still
authorizes each exact destination**, and the native server rejects a
group that does not belong to the operator's active, notice-enabled
business relationships. This notecard is not an authorization bypass.

## Manual workflow

1. Add the LSL source to your HUD and supply `sparkles-hud.cfg`, `venues.cfg`, and `approved_groups.cfg`.
2. Set Title, Message, and Venue (or Use Here).
3. Preview shows the complete message with SLURL and signature.
4. Post submits one group at a time with a unique request ID and waits
   for local queue acknowledgement before applying that group's delay.
5. Sparkles verifies the selected group against MariaDB and queues the
   native Second Life notice attempt, without inference or rewriting.
   On rejection, the HUD stops rather than proceeding silently.

Backend content limit: subject <= 64 bytes, notice body <= 448 bytes, and
subject + separator + body <= 510 bytes. Body includes the original message,
SLURL line and the exact Sparkles Entertainment signature. Oversized content
is rejected rather than truncated. An endpoint that returns `queued` proves
only local acceptance, not simulator delivery or attachment.

**Limitations:** Native landmark inventory search/attachments, scheduled
initial/half-time show notices, publication receipts, operator HTTPS reverse proxy setup,
and real-grid verification remain separate. Queue capacity and per-group
permissions are enforced; incomplete fanout is reported as partial/failed,
not success. Do not replace an existing production HUD until source builds,
local HTTP authorization, simulator dispatch, and in-world notice receipts
have been verified.
