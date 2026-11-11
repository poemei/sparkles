# Sparkles MariaDB module — v1.0.0

This is the **initial MariaDB connection and schema-verification foundation** for Sparkles. It follows the Core ABI 1.5 lifecycle and registers one named service, `sparkles.mariadb.status`. It does **not** access contact, appointment, task, interaction or permission records, does not grant permissions, and does not modify Core/chat routing, IRC or Ollama. No model inference is treated as authority over database records.

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

Use the **actual service account** in the install variables. The make test suite is offline: it validates credential parsing and rejects malformed inputs; it does not claim a database was connected. Only install after tests pass. Restart the Sparkles process when authorized and verify `mariadb v1.0.0: ACTIVE` plus startup log `[MARIADB] local database connected; seven tables verified`.

## Service contract

Header: `include/sparkles_mariadb.h`. A trusted Core module can invoke `sparkles.mariadb.status` with NULL request and zero request size; a successful response is `sparkles_mariadb_status_t` with `connected=1` and `tables_found=7`. The service rejects other request shapes and returns INVALID_STATE when the connection or schema check fails. The service exposes no confidential record data.

## Scope and acceptance

This increment is an installation/connection foundation, **not** a context retrieval feature or autonomous database assistant. Per-contact identity verification and authorization enforcement for reads/writes require separate design because ABI 1.5 service calls do not supply the caller identity. No automatic conversation logging, record updates, or calendar actions are authorized or implemented.

Source commit, automated qualification, runtime activation, end-to-end operation, and human acceptance are separate gates.
