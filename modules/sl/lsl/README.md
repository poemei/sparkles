# Sparkles Marketing HUD (native Second Life integration stage)

The HUD source is [Sparkles_Marketing_HUD.lsl](Sparkles_Marketing_HUD.lsl).
It does **not** use Corrade, PHP, Discord, a public HTTPS endpoint,
an HTTP API, or a HUD token. No `sparkles-hud.cfg` is needed.

## Notecards in HUD inventory

Keep the existing `venues.cfg` and `approved_groups.cfg` files.

`venues.cfg` uses the original five-column format:

```text
# alias|venue name|slurl|landmark inventory name|enabled
river|The River|https://maps.secondlife.com/secondlife/The%20River/38/140/30|The River ~ Rock Music at its BEST|1
stntech|STN Tech Center|https://maps.secondlife.com/secondlife/Dongjak/214/230/52|STN Tech Center|1
#abyss|The Abyss|https://maps.secondlife.com/secondlife/Wasteland/30/158/1508|The Abyss|1
```

The fourth field is an **exact inventory name**, not an inventory UUID.
The HUD displays it in Preview but Sparkles cannot yet retrieve or attach
that landmark using the native SL transport.

`approved_groups.cfg` uses the existing four-column format:

```text
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

Disabled and commented records are excluded from the UI. The HUD
supports selecting `all` or `events`; the default is `events`.
These names are **operator selections**, not notice authorization.
MariaDB remains the authoritative permission source for future sends.
The delay values are parsed and shown but are **not acted on** because
native HUD dispatch is not connected.

## Functional state

- HUD: Title, Message, Venue, Use Here, Alias/List, Preview, Set,
  Clear, Status, Reload.
- Preview: exact operator text, SLURL and the required Sparkles signature.
- Post: **deliberately fails closed** with a clear "NOT SENT" message.
  No packet, IM, HTTP request, or fallback is sent.
- Native SL: private avatar IM handling and independent notice-service
  code remain. There is currently **no authenticated object-origin HUD
  command ingress**. A HUD object IM cannot simply impersonate an avatar.
- The retired local HTTP notice listener and its source/header are
  removed from the SL build.
- Inventory retrieval/landmark attachment, native HUD command intake,
  first/halftime show timers, dispatch pacing and publication receipts
  remain unimplemented or unverified. Existing inventory packet encoder
  source does not establish successful attachment.

A successful GitHub commit is source history, not an LSL compilation,
native build, live avatar test, or operator acceptance.
