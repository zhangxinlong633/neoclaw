# Capability Directory Design

Date: 2026-09-05  
Status: approved (option A — propose only)

## Goal

Organize Capability Matrix rows as **one capability per file** under a configurable directory, with a small **manifest** for load organization. Allow the LLM to **propose** new capabilities when missing; proposals do **not** hot-load.

## Config

```json5
capability_matrix: {
  enabled: true,
  root: ".",
  directory: "capabilities",  // optional
  commands: [ /* inline still supported */ ],
}
```

## Layout

```
capabilities/
  manifest.json5       # organization
  commands/*.json5     # loaded command capabilities
  proposed/*.json5     # LLM drafts — NOT loaded until moved to commands/
```

### manifest.json5

```json5
{
  version: 1,
  load: ["commands"],   // subdirs of *.json5 to load (default if missing)
  proposed: "proposed", // write target for propose_capability
}
```

### Per-capability file

Same fields as inline `commands[]` (`name`, `description`, `argv`, `parameters`, …). Filename should match `name` when possible.

## Loader

On config load: for each `load` subdir, parse `*.json5` / `*.json` into `tools.commands` (same validation as inline). Skip `proposed/`. MCP and C builtins unchanged. Duplicate names fail.

## propose_capability (builtin)

- Registered when `directory` is set and matrix enabled.
- Writes `directory/<proposed>/<name>.json5`.
- Does **not** append to the live matrix this process.
- Returns path + instruction to move into a loaded subdir and restart `neo`.

## Non-goals (this slice)

- Hot-reload / approve CLI
- LLM-authored arbitrary exec taking effect same turn
- Loading `proposed/` automatically
