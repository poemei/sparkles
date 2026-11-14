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

Touch the HUD and choose **Agent**. Enter Sparkles' actual legacy
Second Life avatar username, e.g. `First Resident` or `first.last`.
The HUD uses `llRequestUserKey` to resolve that name via Second Life;
it refuses Post if resolution fails. No hardcoded avatar UUID is used.
Use the Title, Message, Venue and Preview buttons as before. The
`Set` button chooses `events` or `all`, default `events`.
The HUD sends **one bounded object IM per enabled group**, respecting the
configured delay between outgoing submissions. An IM sent by an object
does not provide an application-level acknowledgment.

## Native server-side operator pinning (no endpoint or token)

On the machine **running the Sparkles Second Life module**, the human
operator must securely provision the following file, *not commit it*:

`/opt/sparkles/auth/sl/hud.conf`

```text
operator_uuid=<actual HUD owner's Second Life avatar UUID>
object_uuid=<actual HUD object UUID from llGetKey()>
```

This is a server-local **two-UUID allowlist**, not a URL, token, or
web service. The module requires the regular file to be owned by its
effective service user and to be inaccessible to other users (mode
`0600`, with no symlink). If absent or mismatched, native HUD commands
are rejected. An object recreated/re-rezzed may have a different UUID;
the operator must check the actual in-world object ID. The server-side
operator UUID is never trusted merely because a HUD text field says so.

The native grid receiver separately decodes simulator-origin dialog 19
object IMs, verifies the pinned sender object UUID, validates strict
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
