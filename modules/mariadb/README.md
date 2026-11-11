# Sparkles MariaDB module — v1.2.0

This module uses the Core ABI 1.5 lifecycle, registers an authenticated explicit `/schedule` command and two named services: `sparkles.mariadb.status` and **read-only** `sparkles.mariadb.appointments.read`. The latter retrieves scheduled appointments by an explicit date range using a prepared SQL statement. It does not insert, update, delete or authorize appointments; nor does it modify Core/chat routing, IRC or Ollama. No model inference is treated as authority over database records.

## Database prerequisite

Operator-created local MariaDB database `sparkles` and restricted account `sparkles@localhost` with SELECT/INSERT/UPDATE/DELETE on `sparkles.*`. Required existing tables:

- contacts
- appointments
- tasks
- appointment_participants
- interactions
- contact_identities
- contact_permissions

The module only connects to `localhost` and database `sparkles` as account `sparkles`. There is **no embedded password**. It verifies all seven table names at activation and returns START_FAILED on missing credentials, database connection failure, or missing tables. It neither creates nor migrates schema.

## Credentials (on the VPS)

Create `/opt/sparkles/auth/mariadb/mariadb.env` owned by the account running Sparkles, mode `0600`. The file has exactly one plain key/value line:

```ini
SPARKLES_DB_PASSWORD=YOUR_EXISTING_SPARKLES_DATABASE_PASSWORD
```

Replace the placeholder locally. Do not commit, paste, or log the password. No `export`, quotes, or other keys. The module requires the credential file owner to match the running process effective UID and forbids group/other permissions. The parent directory should be `0700`, accessible to the service user. If the service runs as `stnchain`, set ownership accordingly.

## Build and qualification

On Ubuntu with `libmariadb-dev` and `pkg-config` installed:

```sh
make -C modules/mariadb clean all test
sudo make -C modules/mariadb install INSTALL_USER=stnchain INSTALL_GROUP=stnchain
```

Use the **actual service account** in the install variables. The make test suite is offline: it validates credential parsing and rejects malformed inputs; it does not claim a database was connected. Only install after tests pass. Restart the Sparkles process when authorized and verify `mariadb v1.2.0: ACTIVE` plus startup log `[MARIADB] local database connected; seven tables verified`.

## Service contract

Header: `include/sparkles_mariadb.h`. A trusted Core module can invoke `sparkles.mariadb.status` with NULL request and zero request size; a successful response is `sparkles_mariadb_status_t` with `connected=1` and `tables_found=7`. The service rejects other request shapes and returns INVALID_STATE when the connection or schema check fails. The status service exposes no confidential record data.

### Appointment lookup service (not exposed to chat)

Header: `include/sparkles_mariadb.h`. The private service `sparkles.mariadb.appointments.read` accepts a `sparkles_mariadb_appointments_request_t` with `from_date` (inclusive) and `until_date` (exclusive), both strict `YYYY-MM-DD` dates. The service replies with `sparkles_mariadb_appointments_reply_t`: up to eight scheduled appointments in start-time order, each with ID, title, start and end times; `has_more=1` indicates the result was capped. Empty results mean **no matching stored appointments in that range**, not that the user has no external commitments. Cancelled records are omitted. Stored `DATETIME` values have no timezone; the calling application must establish timezone semantics before relying on times.

This service uses bound SQL parameters; no SQL is constructed from natural-language inputs. It reads only the `appointments` table. It does not verify whether DJ names or venue details are stored in other systems.

**Privacy/authority limitation:** the shared Core ABI does not supply the identity of the *module* invoking a service, and the current IRC transport does not populate a verified account in the route request. Consequently, **no IRC, conversational or terminal schedule-retrieval handler is registered in this increment**. Do not expose this service through chat or a public slash command without a separately designed authenticated caller/authorization boundary. Only locally installed trusted modules can invoke the internal service currently; do not treat that as a user permission check.

## Authenticated IRC schedule retrieval (v1.2.0)

IRC transport v1.1.3 copies the **server-authenticated IRCv3 `account` tag**, received over verified TLS with `account-tag` capability negotiated, into `sparkles_route_request_t.account`. A visible IRC nickname does **not** establish identity. Messages without account tags, with anonymous `account=*`, or malformed tags have an empty account and are denied schedule access. This code does not alter the shared ABI or Core router.

Create the private allowlist on the VPS (the account running `sparkles.service` is shown as `stnchain` in current operator logs):

```sh
sudo install -d -o stnchain -g stnchain -m 0700 /opt/sparkles/auth/mariadb
sudo -u stnchain nano /opt/sparkles/auth/mariadb/irc-accounts.conf
sudo chmod 0600 /opt/sparkles/auth/mariadb/irc-accounts.conf
```

Put **one verified IRC account name per line**, without `account=` or nick prefixes. Use the *actual* server account name confirmed by the IRC operator, which may differ from the displayed nickname. Do not place the bot's SASL account or passwords in this file. No account is implicitly approved. A missing, incorrectly permissioned or malformed file causes fail-closed denial. It is checked on every schedule request for immediate revocation.

Explicit usage, either as an addressed channel message or an IRC private message:

```text
SparklesR: /schedule 2026-11-11 2026-11-12
```

The first date is inclusive; the second is **exclusive**. These are calendar dates in the application's stored `DATETIME` convention, not timezone-aware timestamps. On success, Sparkles sends appointment rows **privately** to the requesting nickname, and posts only a generic confirmation at the origin. Public channel disclosure of record contents is prohibited. Lookup is read-only. It does not confirm newly supplied commitments, create records, identify DJs, or verify external schedules.

**Important:** This is an explicitly invoked command, **not yet automatic natural-language schedule understanding**. Ordinary chat continues to Ollama. The internal named appointment service remains in-process and must not be surfaced as a general public API. The server-verified identity gate applies to this `/schedule` command; arbitrary in-process modules are trusted by Core ABI 1.5 and have no independent module-caller authorization metadata.

Install/qualify the updated IRC and MariaDB modules together; verify version `irc v1.1.3` and `mariadb v1.2.0` and then exercise one authorized and one unauthorized account. These are not yet native-build or operator-acceptance claims.

## Scope and acceptance

This increment adds a private **read-only appointment retrieval service** to the existing connection foundation, **not** a user-facing schedule assistant. Per-contact identity verification and authorization enforcement for reads/writes require separate design because ABI 1.5 service calls do not supply the caller identity. No automatic conversation logging, record updates, or calendar actions are authorized or implemented.

Source commit, automated qualification, runtime activation, end-to-end operation, and human acceptance are separate gates.
