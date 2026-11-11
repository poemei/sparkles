# Sparkles IRC Transport Module

Ported from the established Digit IRC module, with Sparkles-specific naming, configuration, service contracts, and installation paths. This is transport/session code, not a conversation controller.

## Configuration

The module reads **/opt/sparkles/auth/irc/irc.env** directly as a strict key=value file. It does **not** read `irc.conf`, use exported environment variables, or retrieve credentials from GitHub. Use `irc.env.example` as a template. Required exact keys:

```ini
SPARKLES_IRC_SERVER=irc.example.net
SPARKLES_IRC_PORT=6697
SPARKLES_IRC_NICK=Sparkles
SPARKLES_IRC_CHANNEL=#stn-labz
SPARKLES_IRC_ACCOUNT=Sparkles
SPARKLES_IRC_PASSWORD=REPLACE_WITH_SASL_PASSWORD
```

All six keys are mandatory. Unknown/duplicate/missing keys, invalid ports or invalid account/nick/channel values cause config loading to fail. The port must be in 1..65535, with a TLS-enabled server. Set `SPARKLES_IRC_CHANNEL` to one channel or two different comma-separated channels, without whitespace. Don't commit real passwords. Account is the SASL account, which may differ from the nickname.

Create the credential file on the host using operator-approved privileged access and restrict access to the runtime account, e.g.:

```sh
sudo install -d -o stnlabz -g stnlabz -m 0700 /opt/sparkles/auth/irc
sudo install -o stnlabz -g stnlabz -m 0600 modules/irc/irc.env.example /opt/sparkles/auth/irc/irc.env
sudoedit /opt/sparkles/auth/irc/irc.env
```

Replace example server, channel, account, and SASL password with operator values. File content must be raw `KEY=value` lines, without `export` or shell quoting. An absent or invalid file makes IRC startup fail rather than connecting anonymously.

## Behavior

- TLS with peer certificate and hostname verification
- IRCv3 CAP negotiation, SASL PLAIN over TLS, and account-tag capability requirement
- Nick/user registration, one or two channel joins, KICK recovery, PING/PONG, and bounded reconnection
- Core-only conversational handoff through `sparkles.router.dispatch`; ordinary addressed text is not interpreted by IRC
- Core reply is returned to its originating private sender or channel
- IRCv3 `account` tag, when asserted by the authenticated server, is copied into Core route account identity (v1.1.3); nickname alone remains unverified. This supports the explicit, restricted MariaDB `/schedule` command, which delivers appointment details privately.
- Outbound transport services: `sparkles.transport.channel.send` and `sparkles.transport.private.send`
- Safe diagnostic categories, no raw credential logging

## Build, qualify, install

```sh
cd ~/sparkles
git pull
# Rebuild and install Core: router service names were corrected to sparkles.*
make && make test
sudo make install RUNTIME_USER=stnlabz RUNTIME_GROUP=stnlabz
make -C modules/irc clean
make -C modules/irc
make -C modules/irc test
sudo make -C modules/irc install MODULE_USER=stnlabz MODULE_GROUP=stnlabz
sha256sum modules/irc/build/irc.so /opt/sparkles/modules/irc/irc.so
```

The updated Core binary must be installed before the IRC module is tested: an older Core binary may still use the legacy `digit.router.dispatch` service identifier and will not match the new IRC module's `sparkles.router.dispatch` endpoint. Restart Sparkles after updating the Core binary.

The installed module is `/opt/sparkles/modules/irc/irc.so`, with `/opt/sparkles/modules/irc/module.conf` containing `id=irc`. Hotload qualification runs protocol self-tests. IRC activation additionally needs a valid credential file and live authenticated channel joins. Source migration alone does not establish a successful connection or accepted operational behavior.


## Configured-channel ambient memory (v1.1.4)

IRC provides verified `account-tag` identities to a restricted in-process memory exchange service for regular messages visible in Sparkles' *already configured* channels, even when the sender does not address Sparkles. These ambient messages are persisted as unverified private speaker observations without producing replies. This does not monitor arbitrary IRC channels or third-party direct messages. Only messages bearing server-verified account identities and fitting the fixed memory limit are stored. IRC remains a temporary development transport; this is a source-level test of platform-independent contextual intake, not a Second Life implementation. A busy channel can increase synchronous database traffic, and the policy for broader audience consent/retention needs review before wider deployments.
