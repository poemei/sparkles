# Sparkles Second Life transport — native C v0.2.1

This module is in-process native C11, loaded by the existing STN-Labz
Core module ABI. It does NOT use Corrade, C#, C++, .NET, or another
runtime. Corrade and public Second Life protocol documents informed the
protocol design; Corrade code is not included.

## Current protocol implementation

On module start, a native C worker calls the SL XML-RPC login code,
then uses the login response's simulator IPv4 address, UDP port, agent
UUID, session UUID, and circuit code to establish a connected simulator
socket. It sends UseCircuitCode and CompleteAgentMovement, replies to
simulator pings, acknowledges reliable messages, retries reliable
handshake packets, and checks that AgentMovementComplete contains the
login-issued agent and session UUIDs before logging observed presence.
It sends an idle AgentUpdate and a RegionHandshakeReply when requested.
It checks for simulator silence and attempts fresh login on disconnect.

**Actual grid login, avatar presence, and duration of presence remain
UNVERIFIED until installed and tested in Second Life.** In particular,
the current code does not handle capabilities EventQueueGet, IMs,
group/conference chat, local chat, inventory, teleport, reliable
region-reply retransmission, or mature session recovery. The narrow
presence handshake is NOT a fully operational resident/avatar client.

Module activation only means that the worker thread started.
The log line `[SL] simulator confirmed AgentMovementComplete; avatar
presence observed` is the stronger protocol observation, but even
that requires operator confirmation in-world.

## Credentials

The single account configuration path is:

`/opt/sparkles/auth/sl/sl.conf`

It is a private 0600 owner-only regular file, owned by the Sparkles
process UID (normally stnchain). No shell evaluation occurs. Use only:

```ini
first=MsSparkles
last=Resident
password=YOUR_PRIVATE_SECOND_LIFE_PASSWORD
start=home
```

Use the **account's actual login name values**. Do not commit or
paste credentials into chat. The native C parser rejects unknown,
missing, duplicate, or oversized keys, symlinks, and group/other
readable credentials. Password data is wiped after the login attempt.

The legacy XML-RPC parameters `mac` and `id0` are **not operator
configuration fields**. The implementation sends neutral values
internally (all-zero MAC and UUID); whether the grid currently accepts
these values and the XML-RPC endpoint is UNVERIFIED. No actual device
identifiers are collected from the server.

## ABI qualification (v0.2.1)

Core requires at least `STNLABZ_MODULE_MIN_TESTS` (10) qualification
checks, even when all earlier checks pass. SL v0.2.0 executed only 3
and was denied hotload by Core as qualification result 8. Version
0.2.1 adds 18 concrete offline checks covering UUID validation and
round trip, correct circuit packet layout and byte order, movement
packet layout, decoding and malformed/bounded packet rejection.
The module returns qualification success only when at least 10
checks execute and all pass; it does not loosen Core requirements.

**The 2026-11-12 operator hotload failure was observed; the v0.2.1
fix is source committed only until compiled and hotload-tested.**

## Build and offline regression

Install distribution C development dependencies for libcurl, libxml2,
OpenSSL, and pkg-config. Then:

```sh
make -C modules/sl clean all test
```

Build/test does not log into Second Life. A successful
`build/sl.so` does not prove simulator traffic or avatar presence.
Install only under operator control, with credentials present and
known-good source, then inspect SL lifecycle messages in system logs.

The previous staging-only IPC listener is removed in v0.2.0.
Its unverified claimed-avatar UUID was not a safe production identity.
The current module does not grant IRC calendar authorization to SL IDs,
or register/override IRC send services.

## Outstanding integration

Next: implement authenticated incoming/outgoing private IM, group and
conference chat, local chat, capabilities EventQueueGet, SL session
recovery, friends, teleport, inventory, and executive orchestration via
the existing Core/router boundary. No missing functionality is
represented as complete or accepted.
