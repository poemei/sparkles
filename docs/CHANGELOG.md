# Sparkles — Changelog

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
