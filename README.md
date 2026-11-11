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

The source tree includes an Ezra-derived Core and Sparkles-specific modules. The independent Sparkles runtime has been operator-observed through terminal and IRC, but full qualification and acceptance remain outstanding. Ezra is the designated Core source. Sparkles' independent mission must not inherit spacecraft-specific modules or agent identity; imported modules remain outside this Core migration.

Preserve the shared STN-Labz ABI contract. Do not introduce canned conversational responses. Ezra's source repository remains independent.

## Engineering status

**Operator-observed (2026-11-10):** Core startup, Identity routing, local Ollama inference, and an IRC greeting exchange with Digit. Executive v0.3.1 LoRA training completed on a separate training machine; the best checkpoint was merged to a GGUF and imported on the local machine as a **separate** Ollama test model, `sparkles-executive:v0.3.1`.

Standalone manual scheduling tests showed correct interval calculations but also **intermittently wrong YES/NO authorization decisions** for a mandatory meeting break, even when the explanation identified the conflict. This trained test model is **not confirmed active in SparklesR**, and no calendar actions, MariaDB context, Second Life transport, or executive scheduling authority are accepted. The source/installation/runtime/test distinctions and outstanding qualification are documented in [docs/ROADMAP.md](docs/ROADMAP.md).
