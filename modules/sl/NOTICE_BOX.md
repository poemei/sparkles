# Sparkles Notice Test Box — operator setup (source revision 2026-11-13)

**Status:** Source committed only. No build, hotload, proxy deployment, or Second Life end-to-end publication is claimed.

## Architecture

Second Life owner-clicked prim -> operator-controlled HTTPS reverse proxy -> 
`127.0.0.1:17777/notice` inside the native `sl.so` module ->
MariaDB authorized group resolver -> registered Ollama generation service ->
prim draft preview -> explicit **Publish** click -> MariaDB resolver again ->
existing Second Life official group-notice queue (IM dialog 32).

The listener **binds only to loopback**. It is disabled unless a strict, same-UID, owner-only `0600` file exists. Do not expose TCP port 17777 directly to the internet and do not use plain HTTP from LSL. The proxy must serve HTTPS with a valid certificate and should impose request size, rate and connection limits.

This is a controlled test box, not a general-public notice API. The first increment admits exactly one configured owner avatar and one configured object UUID. Re-rezzing a prim may change the object UUID and requires updating the operator's configuration. Group names remain dynamically resolved through the existing database; they are not hardcoded into the native backend.

## Host configuration

Create `/opt/sparkles/auth/sl/notice.conf` with the same effective UID that runs Sparkles; mode **0600**, regular file (no symlinks). Do not commit or paste this credential file:

```ini
token=<random token of at least 32 characters>
operator_uuid=<lowercase Second Life owner/avatar UUID>
object_uuid=<lowercase current object UUID of rezzed test box>
```

Use a cryptographically random token, e.g. `openssl rand -hex 32`, generated locally on the server. The `object_uuid` is the UUID from the actual rezzed prim, not the script inventory item. Both the configured token and object UUID must be placed in the prim script. The script is intended to remain **owner-only / no-modify for other users**. A token is a bearer credential: anyone who obtains it and knows the configured IDs may submit requests. Rotate it if the object is distributed or copied.

The operator controls the public HTTPS hostname and proxy configuration. An example for an **existing** nginx HTTPS virtual host is:

```nginx
location = /sparkles/notice {
    limit_except POST { deny all; }
    client_max_body_size 3k;
    proxy_connect_timeout 5s;
    proxy_read_timeout 15s;
    proxy_pass http://127.0.0.1:17777/notice;
}
```

Set `HTTPS_ENDPOINT` in `modules/sl/lsl/Sparkles_Notice_Test_Box.lsl` to `https://YOUR-EXISTING-HOST/sparkles/notice`; set `OBJECT_TOKEN` to the private token. The example does not install or activate nginx for you. Ensure the Sparkles process has bound 127.0.0.1:17777 before enabling the route.

The native module is built with:

```sh
make -C modules/sl clean all test
```

After a successful operator build, install/hotload using the already established Sparkles procedure. Do not reboot merely to activate this source increment.

## In-world test

1. Rez the prim, inspect its object UUID, and populate the restricted host configuration. Set the two variables at the top of the LSL script.
2. Click the box as the configured owner; select **Team Sparkles** or **Sparkles Ent.**; enter details. Check that the location URL is populated from the prim position.
3. Click **Draft**. The server must respond `{"status":"drafting"}`. The prim polls until the Ollama model returns a valid subject/body or drafting fails.
4. Inspect the exact proposed subject, body and destination in owner chat. **No notice should have been published yet.**
5. Click **Publish**. The server validates the group again, consumes the approval identifier **before** enqueueing, and returns `queued` only if the native outbound queue accepted it.
6. Independently check whether the notice actually appears in the group. An outbound queue receipt and simulator UDP ACK do **not** prove publication.
7. Negative checks: nonowner touch, wrong token, mismatched object UUID, invalid group, expired draft, double-click Publish, and missing Ollama/MariaDB availability must not publish.

## Request contract

`POST /notice`, content-type `application/x-www-form-urlencoded`, with header `X-Sparkles-Object-Token`. For `op=draft`, supply `request_id`, `group`, `message`, `location`, `operator_uuid`, `object_uuid`. For `op=status`, `op=publish`, or `op=cancel`, supply `request_id` and the same authenticated token. Draft generation happens on a separate worker to avoid blocking the HTTP request; only one pending draft/approval is supported in this test increment.

This service deliberately has no unrestricted endpoint for ordinary Sparkles IMs, group chat, or arbitrary Core commands. Existing auth and notice transport remain separate.

## Unverified / current limits

- No live TLS reverse proxy, credential file, MariaDB group resolution, model inference, LSL HTTP response or simulator publication has been tested by this source change.
- The source has not been compiled in this session; any actual compile errors or protocol mismatches require correction before activation.
- The credential file currently supports one operator and one rezzed object. Multiple HUDs would require an explicitly authorized extension.
- An abandoned draft expires after 10 minutes. A publication with an uncertain acknowledgment is **not automatically repeated**.
- The model must follow the bounded output format and include the supplied location. If not, drafting fails safely rather than sending an unchecked notice.
