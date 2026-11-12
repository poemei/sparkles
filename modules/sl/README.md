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

## Account configuration — operator-specified path

Native C credential loading is implemented in `src/sl_config.c`.
The **single** account configuration file is:

```text
/opt/sparkles/auth/sl/sl.conf
```

Expected `key=value` fields (sample placeholders only — do not commit
a populated file):

```ini
first=MsSparkles
last=Resident
password=REPLACE_LOCALLY
mac=REPLACE_WITH_OPERATOR_SELECTED_CLIENT_ID
id0=REPLACE_WITH_OPERATOR_SELECTED_MACHINE_ID
start=home
```

This is parsed as data, **not sourced or evaluated by a shell**.
The file must be owned by the module process UID, be a regular file,
and must not grant any group/other permissions (use `0600`).
Symlinks and unknown/duplicate/missing keys are rejected. Do not
paste a real password into chat or a GitHub repository. The module does
not create this credential file or alter account details.

`sparkles_sl_login_from_file()` reads this path, invokes the native
C XML-RPC login implementation, and clears the in-memory credential
structure. The login response is only a simulator circuit proposal.
**This function is not yet wired into the module lifecycle or a live SL
session implementation**, so installing the module still does not log
MsSparkles into Second Life. Login endpoint compatibility and SL
authentication challenges remain unverified.

Native build of the login code additionally needs libcurl and libxml2
development headers and libraries (`pkg-config` for libxml2).
No C#, C++, .NET or Corrade executable is included.

## Developer build (staging scaffold only)

```sh
cd modules/sl
make
```

This builds `build/sl.so` with the repository ABI headers and Linux
libcrypto/pthread/libcurl/libxml2. It is a source/build smoke check **only**, not a Second
Life connection check. No .NET tooling is required.

The module currently requires `SPARKLES_SL_SOCKET` pointing into an
already-existing, runtime-UID-owned directory with mode 0700. It creates
a socket with mode 0600. **Do not configure or activate this incomplete
ingress module in a production Sparkles runtime.** Avatar configuration is loaded only from the operator-owned private file
above; the existing module lifecycle does not yet initiate login.

## Qualification and evidence

- Native C source and module manifest: committed.
- C compile, tests and qualification: not performed by this source edit.
- Native C XML-RPC credential exchange code: **SOURCE IMPLEMENTED,
  UNTESTED AND NOT INVOKED BY THE MODULE**. Simulator connection and
  actual SL grid communication: **NOT IMPLEMENTED**.
- MsSparkles avatar login, IM, group/local chat: **NOT TESTED**.
- Installation, activation, deployment and operator acceptance: absent.

This document supersedes the earlier unapproved C# implementation
instructions. The current artifact is a **C-only foundation**, not
a completed Second Life module.

**AWAITING OPERATOR END-TO-END TESTING AND ACCEPTANCE** after a complete
operator-authorized native C implementation exists.
