# Sparkles Core and Module Interface Map

## Core foundation

Sparkles' Core source is adapted from `poemei/ezra`. The external STN-Labz ABI is shared with Digit. Agent-specific names, log paths, service namespaces and compiled Root identity are Sparkles-owned.

### Module lifecycle

Core owns discovery, verification, qualification, activation, hotload, withdrawal, inventory and mandatory logging. The implementation uses the shared ABI registry and loader contract.

### Registration and routing

- Commands are registered by modules using the shared `stnlabz_module_host_t` callbacks.
- Services are registered and withdrawn through that same host interface.
- During module activation, Core records registration ownership through `sparkles_module_host_begin_registration` / `sparkles_module_host_end_registration`, matching Digit's module ownership contract.
- Incoming messages use `sparkles.router.dispatch` and dispatch to registered commands. Ordinary text goes to registered `chat`; Core does not implement model intelligence.
- Transport outbound operations use registered channel/private services.

### Shared ABI

The external sibling `../ABI` source is required to compile. The ABI header and registry/loader contracts are not independently redefined for Sparkles.

## Module directory

The copied module directory is deliberately **outside the present Core migration**. Its Rictus-specific sources, Ollama model selection, and other module implementation details have not been adapted or qualified. No module runtime claim is made.

## Evidence status

Source migration committed. Core build, module qualification, installation, runtime and operator acceptance remain to be demonstrated.
