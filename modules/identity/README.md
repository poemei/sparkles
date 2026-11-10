# Rictus Identity & Mission — 1.1.3

Rictus mission, identity, and General Order responses are owned by the standalone Identity module's installed data file:

`/opt/rictus/modules/identity/identity.conf`

- `mission`, `name`, `organization`, `role`, and `order1`/`order2`/`order3` are required; invalid or incomplete data causes startup failure.
- `/identity mission`, `/identity who`, `/identity orders`, and individual General Order requests are data-backed and deterministic.
- Natural-language questions such as `what is your mission?`, `rictus, what is your mission?`, and `rictus: who are you?` are recognized by `rictus.identity.answer`.
- Rictus Ollama consults the Identity service before model generation. Matching identity/mission questions never reach Ollama generation.
- Unmatched conversation returns `NOT_FOUND` from Identity and continues to Ollama for language/inference.
- Core and shared ABI 1.5 are unchanged. No direct module-to-module binding is used.
- The module record is loaded at startup. Installing only the binary without the matching `identity.conf` is insufficient.
- Rictus is root-owned on the current host; the installer therefore installs the module directory, binary, and identity record as `root:root`.

Current record:

- Name: Rictus
- Organization: STN-LABZ
- Role: Control Automated Agent
- Mission: `I exist to help STN-LABZ observe, understand, coordinate, and control operational activity across its systems.`

## Operator checks

```sh
make -C modules/identity clean all test
sudo make -C modules/identity install

make -C modules/ollama clean all test
sudo make -C modules/ollama install
```

Then verify through the normal Rictus client:

```text
rictus: what is your mission?
rictus: who are you?
rictus: what is your first general order?
rictus: how are you?
```

The first three should resolve deterministically through Identity. Unrelated chat such as `how are you?` should fall through to Ollama.

Source version 1.1.3. Build, qualification, activation, and operator acceptance remain separate evidence gates.
