# Rictus Web integration — v1.0.3

No `web.conf` and no `RICTUS_WEB_TOKEN`.

The worker connects to `http://127.0.0.1/chat/transport`, using the
existing ChAoS MVC Chat route, and sends `poll_rictus` / `reply_rictus`.
Digit's existing `poll` / `reply` remain unchanged.

This requires the Chat transport to run on Rictus's VPS at localhost port
80, or to be already mapped there by the host's existing configuration.
If the endpoint is absent, **do not claim integration succeeds**.
Check the HTTP status before changing any further components.
