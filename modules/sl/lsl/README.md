# Sparkles Marketing HUD — native Second Life IM integration

`Sparkles_Marketing_HUD.lsl` v1.5.0 uses **Second Life object-to-avatar
instant messages**, not Corrade, PHP, Discord or an HTTPS endpoint.
`sparkles-hud.cfg` is not needed.

## HUD inventory and configuration

Retain the existing operator-maintained `venues.cfg` and
`approved_groups.cfg` files without changing their formats:

```text
# venues.cfg
# alias|venue name|slurl|landmark inventory name|enabled
river|The River|https://maps.secondlife.com/secondlife/The%20River/38/140/30|The River ~ Rock Music at its BEST|1
stntech|STN Tech Center|https://maps.secondlife.com/secondlife/Dongjak/214/230/52|STN Tech Center|1
#abyss|The Abyss|https://maps.secondlife.com/secondlife/Wasteland/30/158/1508|The Abyss|1
```

```text
# approved_groups.cfg
# set|group name|enabled|delay seconds
all|Sparkles Entertainment|1|10
all|Poe's Peeps|1|10
all|Team Sparkles|1|10
all|The Abyss Club VIPs|0|10
events|Sparkles Entertainment|1|10
events|Poe's Peeps|1|10
events|Team Sparkles|1|10
events|The Abyss Club VIPs|0|10
```

On startup, the HUD automatically calls `llRequestUserKey` for
`MsSparkles Resident`, the Sparkles avatar name already established
in the native SL source. No manual Agent menu or avatar UUID entry is
required. If the simulator lookup fails, Post fails closed.
Use the Title, Message, Venue and Preview buttons as before. The
`Set` button chooses `events` or `all`, default `events`.
The HUD sends **one bounded object IM per enabled group**, respecting the
configured delay between outgoing submissions. An IM sent by an object
does not provide an application-level acknowledgment.

## Native simulator identity and MariaDB authorization

No `hud.conf`, token, endpoint, object UUID entry, or operator UUID
entry is required. The HUD obtains its own owner and object identities
from Second Life, and the native receiver uses the simulator-delivered
object-IM owner identity rather than trusting UUIDs within the message.
The requested notice group must be authorized by the existing MariaDB
`businesses`, `business_groups` and `sl_groups` relationships for
that owner. No registry match means no notice is queued.

The native grid receiver separately decodes simulator-origin dialog 19
object IMs, validates simulator-attributed object identity and strict
`STNHUD1` escaped request fields, and then uses Sparkles' existing
single-group notice service and MariaDB business/group authorization.
Object IMs are **not sent to Core conversation or Ollama inference**.
Unsupported, oversized and unauthorized commands fail closed.

## Distinguish the evidence

- **HUD submitted an IM**: the LSL function was invoked. This is *not*
  proof Sparkles received it.
- **Sparkles queued a notice**: native service validated the request
  and queued a simulator packet. This is *not* proof of publication.
- **Actual group notice publication and attached landmark**: unverified.
  Landmark names from the venue notecard are carried by the object IM
  and parsed but **no inventory lookup or attachment has been wired**.
- **Schedule**: existing `approved_groups.cfg` inter-group delays are
  pacing only. The separate 7:30 PM / 9:00 PM show schedule has not been
  added here.
- **Runtime**: operator must compile, deploy and perform in-world tests.
  GitHub source commits alone do not validate a live simulator message.

### Source build and regression checks

```sh
make -C modules/sl clean all test
```

Look for logs prefixed `[SL HUD]` for authorization, parsing and
queueing decisions; do not interpret a normal UDP acknowledgment as
proof of group-notice publication.
