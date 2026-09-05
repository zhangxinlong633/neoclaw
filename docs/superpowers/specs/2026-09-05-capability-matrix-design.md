# Capability Matrix（能力矩阵）Design

Date: 2026-09-05  
Status: approved for spec (A skeleton + B paths + C policy; name: 能力矩阵)

## Goal

Give Neo a first-class **Capability Matrix** alongside DAG workflows: a standard, discoverable table of what the agent can do (contracts + bindings), so reactive tool loops and frozen DAGs share one vocabulary. MCP is a **loader** into the matrix, not a parallel tool system.

One-liner: **DAG orchestrates how work flows; the Capability Matrix defines what can be invoked; Policy decides what is allowed and how expensive it may be.**

## Product narrative (vs DAG)

| | DAG (existing) | Capability Matrix (this design) |
|--|----------------|----------------------------------|
| Role | Frozen topology, deterministic orchestration | Discoverable capabilities + contracted execution |
| Model | Worker that does not replan | Worker that selects blades, fills args, reads receipts |
| Config surface | `workflows` / steps | Matrix entries (builtins, commands, MCP, …) |
| Metaphor | Handle orchestrates | Blade catalog as a **matrix** (rows = capabilities, columns = contract dimensions) |

This is the same depth of product concept as DAG—not “add a few more OpenAI tools.”

## Architecture

### A — Skeleton: Capability Matrix

Each **row** is one capability. **Columns** (contract dimensions) include at least:

| Column | Meaning |
|--------|---------|
| `name` | Stable tool id exposed to the model and to DAG `type: tool` |
| `source` | `builtin` \| `command` \| `mcp:<server>` |
| `description` | Human/model-facing summary |
| `parameters` | JSON Schema object (OpenAI `function.parameters`) |
| `effect` | Side-effect class: `read` \| `write` \| `exec` \| `network` |
| `timeout_sec` / `max_output_bytes` | Runtime budgets |
| `root` | Sandbox root (default `tools.root`) where applicable |
| `binding` | How to run (C builtin, allowlisted argv, MCP `tools/call`) |
| `enabled` | Effective availability after policy |

**Consumers (same matrix snapshot):**

- `./neo "…"` and daemon tool loop  
- Workflow `type: tool`  
- Workflow `llm` with `tools: "on"`  
- `neo plan` — planner may only reference names present in the matrix (including MCP-derived names)

### B — Usage paths

| Path | When | Behavior |
|------|------|----------|
| Reactive loop | Plain `./neo`, daemon, `llm`+`tools:on` | Model picks tools from the matrix snapshot via OpenAI-style `tools` |
| Declarative DAG | `workflow run` / `neo plan`→`run` | `type: tool` / planner output must use matrix `name`s |
| Upgrade (non-goal for v1) | Optional later | Materialize a successful tool trace into workflows JSON |

DAG steps gain power because they bind to the **same** matrix rows as the agent loop—including MCP tools once loaded.

### C — Policy layer

Sits between matrix materialization and dispatch:

- **Effect gates**: `write` / `exec` / `network` require explicit config flags (existing `http_fetch_enabled`; new `shell_enabled`; per-MCP-server enable)
- **Budgets**: `max_rounds`, per-call output caps, optional per-session call count
- **Sandbox**: path tools stay under `tools.root`; MCP trusts process isolation + server allowlist
- **Observability**: `-v` logs `capability=<name> source=… status=…`; silent truncation of >16 tool_calls per turn becomes an explicit stderr warning

## MCP as a matrix loader

- Config: `tools.mcp_servers[]` with `name`, `command`, `args`, optional `env` (**stdio** in v1)
- Reserve `url` (or transport) field in schema **without implementing** HTTP/SSE in v1
- Lifecycle: start server → `initialize` → `tools/list` → insert rows (`name` = `mcp_<server>_<tool>` to avoid collisions)
- Invoke: `tools/call` → normalize content to the same tool-result string shape as builtins
- Failure: mark that server’s rows unavailable; other rows keep working; DAG step naming a missing capability fails loudly

## Builtins / call-quality (v1 delivery themes)

Not the conceptual core, but required to make the matrix useful immediately:

- Commands: optional `parameters` JSON Schema on `tools.commands[]`
- System prompt: list matrix names + short descriptions (builtins and configured commands/MCP)
- New builtin row: `grep` (read-only, under `tools.root`, match/line caps)
- Optional later row: `run_command` behind `shell_enabled` (default off)

## Phasing

| Phase | Deliverable |
|-------|-------------|
| **P0** | In-process Capability Matrix abstraction over existing builtins + commands; command `parameters` schema; system listing; observability fixes; `grep` builtin |
| **P1** | MCP stdio loader → matrix rows; DAG `type: tool` and planner can use MCP names |
| **P2** | Controllable `run_command` + richer policy knobs; docs elevate Capability Matrix beside DAG |

## Non-goals (this initiative)

- Full MCP resources/prompts surfaces as first-class Neo features  
- MCP HTTP/SSE runtime (config may reserve fields only)  
- Auto-compiling tool loops into DAGs  
- Cursor-class IDE agent (multi-file apply_patch UX, repo index, etc.)

## Success criteria

- Docs/README treat **Capability Matrix（能力矩阵）** as peer to DAG  
- One config → loop and planned DAG tool steps share the same capability names  
- Adding a stdio MCP server exposes tools to both agent loop and DAG without new C per-tool code  
- Custom commands with schemas are called more reliably; over-limit tool_calls are visible on stderr  

## Testing

- Unit: matrix build from fixtures (builtin + command with schema; name collision rules)
- Unit: policy disables `network`/`exec` rows when flags off
- Integration (optional smoke): mock or tiny stdio MCP that lists one tool and echoes `tools/call`
- Plan prompt / extract tests: planner allowlist text includes matrix names when present

## Open implementation notes (non-blocking for spec)

- Code module naming: prefer `capability_matrix` / `cap_matrix` over “registry”
- Exact MCP JSON-RPC framing and process supervision details belong in the implementation plan
