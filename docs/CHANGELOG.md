# Sparkles — Changelog

## 2026-11-12 — Contact-linked shared appointments (source only)

- MariaDB v1.9.2: exact unique contact lookup for one explicitly named guest; personal conflict checks for creator and guest; atomically insert creator as accepted and guest as pending.
- Ollama v1.2.0: preserve explicit guest through conversational proposal and confirmation; reject multi-guest expressions instead of silently omitting participants; add parser qualification cases.
- Preserve contact-scoped reads, transport identity authentication, and existing unlinked appointments. No database changes or migration performed.

**Verification:** GitHub source changes and readback only. Native compilation, SQL integration, deployed behavior and operator acceptance remain unverified.


## Unreleased — 2026-11-10 — Adopt Ezra Core foundation

- Replaced the inherited Rictus Core source and headers with the corresponding Ezra Core implementation, adapted to Sparkles identity, namespace and paths.
- Imported Ezra's Core module host, activation, discovery, hotload, inventory, policy, mandatory logging, runtime, and legacy module management sources.
- Preserved Ezra's qualified module activation and its module registration ownership hooks, previously aligned with Digit's Core contract.
- Removed Rictus-only DSD Core source and header from the Sparkles executable source tree.
- Kept the separate external shared STN-Labz ABI dependency; no ABI interface change is intended.
- Left the `modules/` source directory unchanged during the Core migration. The copied Ollama model and Rictus-specific module content are not considered Sparkles capabilities.

**Verification:** Source commits were made directly to GitHub. Native Ubuntu build, tests, installation, module hotload, model connection and end-to-end operator acceptance have not been performed.

## Earlier

Earlier repository history includes initial Rictus-derived import and incremental changes before Ezra was designated as the Core foundation. Git history preserves those commits; this changelog records the new direction without claiming that earlier builds passed.
