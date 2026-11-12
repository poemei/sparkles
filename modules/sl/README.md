# Sparkles Second Life module — native C staging source (v0.1.1)

**Architecture requirement:** Sparkles is native C (C11). The operator did
NOT approve C++, C#, a managed runtime, Corrade, or a separate SL service.
No such runtime or client is part of this module.

## What is present

`src/sl_module.c` is a native C, STN-Labz ABI-compatible module. Its
existing implementation is a **local Unix-domain IPC ingress scaffold**,
not a Second Life grid client. It invokes the existing Core router for
framed private messages, and has bounded input, Unix peer UID checks,
and a private socket. It does **not** log the avatar in, negotiate the
SL protocol, authenticate a grid session, receive real SL IMs, manage
group sessions, or deliver anything to the Second Life grid itself.

**Crucial trust limitation:** the current IPC frame contains a claimed
sender UUID, not a UUID verified by the grid. Same-UID IPC authorization
does not prove grid identity. This interface therefore MUST NOT be
used as an authoritative SL identity source or granted calendar
privileges. It is a local testing scaffold only, not a deployable SL
transport. The previous C# avatar project and LibreMetaverse dependency
were removed because the human operator requires a native C module.

## What the requested native C SL transport still requires

A native C implementation of the Second Life login/session protocol and
message transport must be implemented and qualified before the module
can represent MsSparkles as an in-world avatar. That work includes:

- SL authentication, persistent simulator/session connection and
  reconnect recovery without placing account credentials in source;
- IM reception and UUID verification from authenticated SL traffic;
- outbound IM transport with delivery outcomes;
- group/conference sessions and local chat with correct provenance,
  privacy and routing boundaries;
- robust network/main-loop separation from blocking Core inference;
- strict rate limits, permission boundaries and operator acceptance.

Do not claim these capabilities based on the existence of an IPC socket,
Core response, or module ACTIVE status. Do not copy or embed a C++/C#
client, add a second runtime, or modify Core/shared ABI as a workaround.
The required native C grid-protocol implementation remains incomplete.

## Developer build (staging scaffold only)

```sh
cd modules/sl
make
```

This builds `build/sl.so` with the repository ABI headers and Linux
libcrypto/pthread. It is a source/build smoke check **only**, not a Second
Life connection check. No .NET tooling is required.

The module currently requires `SPARKLES_SL_SOCKET` pointing into an
already-existing, runtime-UID-owned directory with mode 0700. It creates
a socket with mode 0600. **Do not configure or activate this incomplete
ingress module in a production Sparkles runtime.** No account credentials
or avatar login configuration are provided by this source.

## Qualification and evidence

- Native C source and module manifest: committed.
- C compile, tests and qualification: not performed by this source edit.
- Native C SL login and grid communication: **NOT IMPLEMENTED**.
- MsSparkles avatar login, IM, group/local chat: **NOT TESTED**.
- Installation, activation, deployment and operator acceptance: absent.

This document supersedes the earlier unapproved C# implementation
instructions. The current artifact is a **C-only foundation**, not
a completed Second Life module.

**AWAITING OPERATOR END-TO-END TESTING AND ACCEPTANCE** after a complete
operator-authorized native C implementation exists.
