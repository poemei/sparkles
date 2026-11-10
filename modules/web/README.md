# Rictus Web v1.0.3

Rictus uses the same outbound queue/reply pattern as Digit, not an inbound
listener. There is no token file, TLS certificate, or web.conf.

The Web worker sends requests to the local ChAoS MVC Chat transport:

`http://127.0.0.1/chat/transport`

- `{"type":"poll_rictus"}` fetches Rictus-only queued work.
- `{"type":"reply_rictus","id":"...","status":"ok","message":"..."}`
  returns the reply attributed to Rictus.
- The job is dispatched through the existing `rictus.router.dispatch`.
- Digit's `poll` and `reply` are not modified.

**Deployment dependency:** `127.0.0.1` refers to the VPS where Rictus runs.
The ChAoS MVC Chat transport must actually be served by that VPS at this
address, with its existing localhost access restriction. If not, requests will
fail. A 403 means the request is not reaching the trusted local transport.
Do not disable the Chat transport's access control.

Build and test:

```sh
make -C modules/web clean all test
sudo make -C modules/web install
```

Rictus hotloading must activate the installed module. A successful unit test
does not establish an end-to-end Chat round trip.
