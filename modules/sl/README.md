# Sparkles Second Life transport — v0.1.0

This is a **native Sparkles ABI module** plus a **headless Second Life
avatar process**. It is not Corrade and it is not an LSL object. The avatar
process uses LibreMetaverse as a library, inside the process, for login and
protocol handling. Core remains responsible for routing and inference.

## Provenance and current scope

- A dedicated in-world MsSparkles account has been created by the operator.
- Source presently implements grid login, reconnect attempts, bounded private
  IM reception, avatar UUID attribution, an owner-only local Unix socket,
  Core dispatch, and UUID-targeted private replies.
- The C module starts an IPC listener; **its ACTIVE status does NOT mean
  the avatar process has logged in**. They are distinct processes.
- No claim is made that the source has been compiled, installed or tested
  with Second Life. No credentials, payments, or accounts were changed.
- Group and conference IMs, local chat, notices, teleport, inventory,
  automated reminders, and in-world payments are **not enabled**.
  These require typed, session-aware Core interfaces and separately
  authorized policies. Silent group interception would break discretion.

## Protocol boundary

The C module accepts a single Unix peer with the same effective user ID.
Its socket directory must already exist, be owned by the runtime user, and
must have no group/other permissions. Socket itself is mode 0600. Never
expose the IPC socket over a network. No unverified external client can
inject an arbitrary `account` identity by using a public HTTP endpoint.

One tab-separated, base64-framed line per message:

- Avatar -> module: `I<TAB>avatar-uuid<TAB>base64(display-name)<TAB>base64(utf8-text)\n`
- Module -> avatar: `R<TAB>avatar-uuid<TAB>base64(utf8-reply)\n`

The module derives the Core account as `sl:<avatar-uuid>`.
Names are **display only**, not authorization. The local socket is an
authenticated trust boundary, not a substitute for grid authentication.

The existing IRC account allowlist is NOT automatically extended to
Second Life identities. Consequently, no claim is made that privileged
calendar operations will work for SL residents until identity/authorization
and outbound private notification interfaces are deliberately adapted.
IRC remains unchanged.

## Build (on an authorized development host)

From `modules/sl`:

```sh
make
dotnet build avatar/Sparkles.SLAvatar.csproj -c Release
```

The native module uses the repository's `include/stnlabz_abi` headers.
The avatar uses the NuGet `LibreMetaverse` package (pinned to 3.3.0).
Builds require a suitable .NET 8 SDK, restore connectivity, Linux,
libcrypto and pthread.

## Isolated installation and activation (operator directed only)

Example with Sparkles operating as the already configured `stnchain`
Linux account. Do not run both the SL viewer and this avatar client
concurrently on the same account.

```sh
sudo install -d -o stnchain -g stnchain -m 0700 /opt/sparkles/state/sl
sudo make install
```

Set `SPARKLES_SL_SOCKET=/opt/sparkles/state/sl/sparkles.sock` in the
**Sparkles Core runtime environment** before activating the sl module.
The directory must be mode 0700 and owned by the Core process UID.
The module will refuse startup if these conditions are not met.

For the headless avatar process, set the same socket path and:

- `SPARKLES_SL_FIRST` — account first name (typically MsSparkles)
- `SPARKLES_SL_LAST` — account last name (typically Resident)
- `SPARKLES_SL_PASSWORD` — from a private service credential store
- `SPARKLES_SL_SOCKET` — local IPC socket path

Do **not** commit, paste, log, or put passwords in shell history, argv,
repository files, or chat. Use private service environment injection.
Run the avatar under the **same UID** as the Sparkles Core module;
it will refuse IPC when the UIDs do not match. Supervise both processes
with your existing process manager and configure restart behavior.
Do not claim continuous SL availability merely because processes restart.

The avatar is a scripted agent and must comply with SL's applicable
scripted-agent, communication-rate, and estate policies.

## First operator qualification

1. Build and inspect warnings for both components; verify package API
   compatibility and no missing symbols.
2. Install/activate the native module; verify it reports only IPC
   listener activation, not SL login.
3. Start the avatar client as the same UID using private credentials;
   observe a successful grid login and authenticated IPC handshake.
4. Send MsSparkles a **private IM** from an unrelated SL account;
   confirm a real Core/Ollama reply is delivered to the original avatar.
5. Send a message from a second account; verify replies are not crossed.
6. Try group chat and local chat: neither must trigger an unintended
   conversational reply or leak private content.
7. Stop the avatar, disrupt IPC and reconnect; verify behavior and
   observe queue limitations, then inspect service logs.
8. Verify an unauthorized avatar cannot read/write privileged calendar
   data merely by knowing an avatar name, UUID, or group membership.

No SL payments, proactive notifications, group minutes, or calendar
mutations are qualified by this transport-only increment.

**AWAITING OPERATOR END-TO-END TESTING AND ACCEPTANCE.**
