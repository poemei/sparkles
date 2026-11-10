# Ms. Sparkles

**STN-Labz Executive Assistant AI Agent**

Sparkles is a distinct executive-assistant agent, not Rictus renamed. The intended development foundation is STN-Labz's established modular Core with IRC and local Ollama, followed by authorized MariaDB context and Second Life transport.

## Architecture

```
IRC / future transports -> Core router -> registered Sparkles module -> reply/action transport
                                                 |
                                            Ollama inference
                                                 |
                                       authorized MariaDB context
```

Core is responsible for the generic runtime, routing, logging and module lifecycle. Transports handle their own protocols. Model inference interprets conversation and generates responses. Sparkles' executive capabilities belong outside generic Core.

## Adaptation boundary

The current source tree was derived from Rictus and **has not yet been fully adapted or qualified as Sparkles**. Ezra is the designated Core source. Sparkles' independent mission must not inherit spacecraft-specific modules or agent identity; imported modules remain outside this Core migration.

Preserve the shared STN-Labz ABI contract. Do not introduce canned conversational responses. Ezra's source repository remains independent.

## Engineering status

**Ezra-derived Core source migration committed; runtime not verified.** There is no claim yet that the Sparkles code builds, installs, loads modules, or functions end-to-end. Consult [docs/ROADMAP.md](docs/ROADMAP.md) for the scoped adaptation stages.
