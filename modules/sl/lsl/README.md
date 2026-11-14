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

Optionally retain `venues.cfg` (same original five-column syntax):

```text
# alias|venue name|slurl|landmark reference|enabled
```

The `landmark reference` column is retained for compatibility but is **not**
resolved or attached in this increment. An SLURL is part of the notice text,
not an actual attached landmark.

## Manual workflow

1. Add the LSL source to your HUD and supply the two inventory notecards.
2. Set Title, Message, and Venue (or Use Here).
3. Preview shows the complete message with SLURL and signature.
4. Post sends one authenticated HTTP request with a unique request ID.
5. Sparkles selects authorized active notice-enabled groups **from MariaDB**
   and queues official Second Life group-notice attempts, without inference
   or rewriting.

Backend content limit: subject <= 64 bytes, notice body <= 448 bytes, and
subject + separator + body <= 510 bytes. Body includes the original message,
SLURL line and the exact Sparkles Entertainment signature. Oversized content
is rejected rather than truncated. An endpoint that returns `queued` proves
only local acceptance, not simulator delivery or attachment.

**Limitations:** Native landmark inventory search/attachments, timer/notecard
show scheduling, publication receipts, operator HTTPS reverse proxy setup,
and real-grid verification remain separate. Queue capacity and per-group
permissions are enforced; incomplete fanout is reported as partial/failed,
not success. Do not replace an existing production HUD until source builds,
local HTTP authorization, simulator dispatch, and in-world notice receipts
have been verified.
