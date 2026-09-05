# Config JSON5 cutover + verbose workflow diagnostics

Date: 2026-09-05  
Status: approved for implementation

## Goal

Address the highest maintainability risk from the project review: **hand-written YAML parsing**. Replace config/workflow serialization with **JSON5 via yyjson 0.12.0** (`YYJSON_READ_JSON5`), hard-cut (no YAML dual-read). Add **medium-tier diagnostics**: path-aware parse/validate errors by default, plus `--verbose` structured step summaries on stderr.

## Decisions

| Topic | Choice |
|-------|--------|
| Scope | Config JSON5 cutover + perceptible polish (not parallel DAG / MCP / audit files) |
| Format | **JSON5** (JSON supersetted) — one-shot cutover; refuse `.yaml`/`.yml` with migration hint |
| Parser | yyjson 0.12.0 with `YYJSON_READ_JSON5` → fill existing `agent_config_t` (no IR layer) |
| Diagnostics | Default: path-aware failures; `-v/--verbose`: step summaries; `-d` unchanged (prompt dump) |
| Migration script | Optional / deferred — not required for this change; docs suffice |

## Architecture

```text
config.json ──yyjson YYJSON_READ_JSON5──► agent_config_t ──► workflow / tools / plan
neo plan LLM ──extract JSON──► validate ──► stdout JSON | materialize temp .json + run
```

- Delete line/indent YAML parser in `config.c`.
- Keep `agent_config_t` field layout stable so `workflow.c` / tools paths stay mostly untouched.
- Plan materialization uses a temporary `.json` file loaded by the same `config_load` (JSON5 reader accepts strict JSON).

## Config paths

| Role | New default | Reject |
|------|-------------|--------|
| Main config | `config/config.json`, else `config.json` | `*.yaml` / `*.yml` |
| Profile | `config/profiles/<name>/neo.json`, else `profiles/<name>/neo.json` | `neo.yaml` |
| `-c` / `NEO_CONFIG` | Must point at `.json` / `.json5` | YAML extension or parse failure → exit 1 + message |

Also accept `.json5` as an alias extension for the same JSON5 reader. Default filenames stay `*.json` (JSON5 is a superset; comments/trailing commas allowed inside).

Stderr on yaml path (example): `neo: config is JSON5-only; migrate to config.json (see docs)`.

## JSON5 shape

Top-level object keys match today’s sections: `model`, `skills`, `tools`, `workflows`, `plan`, `soul`, `rules`, `memory`, `bootstrap`, `session`, `workspace`.

- Lists (`depends_on`, `then`, `else`, `paths`, `argv`, …) are arrays only (no bare-string-as-list sugar).
- Step `tools` remains a step field (`"tools": "off"` / `"on"`), not confused with top-level `tools` object.
- Omit `type` when `prompt` is present → treat as `llm` (preserve current semantic).
- **JSON5 allowed on read:** comments (`//`, `/* */`), trailing commas, unquoted keys, single-quoted strings, and other features covered by yyjson’s `YYJSON_READ_JSON5` (0.12.0).
- No separate JSON Schema validator in-process.
- **Write path** (`neo plan` stdout / `-o` / materialize): emit **strict JSON** (pretty), which remains valid JSON5 input.

Example (fragment):

```json
{
  "model": { "base_url": "...", "name": "...", "api_key": "..." },
  "workflows": [
    {
      "name": "diamond",
      "steps": [
        { "id": "a", "type": "tool", "tool": "count_run" },
        { "id": "b", "type": "tool", "tool": "count_run", "depends_on": ["a"] }
      ]
    }
  ],
  "plan": { "target_steps": 10 }
}
```

## Plan / CLI

- `neo plan`: prompt asks for JSON with top-level `"workflows": [...]` (fenced ` ```json ` allowed).
- Replace `plan_extract_workflows_yaml` with `plan_extract_workflows_json`.
- `neo plan` stdout = that JSON document (pretty-printed for readability / `-o`).
- `neo run`: plan → validate → execute; stdout still **execution result only**.
- Rename quiet flag semantically (`quiet_plan` / equivalent); drop `quiet_yaml` naming.
- `-o FILE`: write plan JSON; prefer `.json` in docs (suffix not enforced).

## Diagnostics

### Default (always)

- JSON parse/validate errors include a JSON Pointer-style path when possible, e.g. `/workflows/0/steps/2/depends_on`.
- Step execution failure: stderr includes workflow name, step id, reason.
- Successful runs (no `-v`): stderr may include **one** final `neo: run done ...` summary line; no per-step chatter.

### `-v` / `--verbose`

Stable, greppable lines:

```text
neo: step start workflow=diamond id=a type=tool tool=count_run
neo: step end   workflow=diamond id=a status=ok ms=12
neo: step end   workflow=diamond id=x status=skip reason=route_else
neo: step end   workflow=diamond id=branch type=route hit=1 ms=0
neo: run done workflow=diamond status=ok steps=4 skipped=1
neo: plan ok steps=10
```

- `ms` = wall-clock milliseconds for the step.
- `status` ∈ `ok` | `skip` | `error`.
- Do not dump LLM content or full tool payloads on verbose (use `-d` for prompt-level debug).

### `-d` / `--debug`

Unchanged meaning (system prompt / request params). Orthogonal to `--verbose`; both may be on.

### Convergence

Replace unconditional `neo dag:` / noisy `neo tool:` success spam with the scheme above (default quiet / verbose detail).

## Docs & examples

- `config/config.yaml.example` → `config/config.json.example`
- Update README, `docs/tool.md`, `docs/workflow.md`, `docs/claw.md`, `example/`, relevant specs/plans paths and code fences to JSON
- Short migration note (README subsection or `docs/migrate-json.md`): same keys, arrays required, no comments

## Tests

- Convert all test fixtures to `.json`.
- Keep coverage for: nested step `tools`, omit `type`, route merge/skip, invalid JSON, wrong types (path-bearing errors).
- Assert `.yaml` config path is rejected.
- Assert `--verbose` emits expected `neo: step` prefixes (stderr capture).
- `make test` green is the gate.

## Non-goals

- Dual-read YAML or silent YAML fallback
- Intermediate config IR / full JSON Schema engine
- Disabling JSON5 (always use `YYJSON_READ_JSON5` for config load)
- Parallel DAG, durable execution state, retries-as-platform
- MCP host / plugin runtime
- Audit log files or fine-grained permission matrix
- Large rewrite of hand-built chat JSON bodies in `agent_tools.c` (out of scope)

## Success criteria

1. No YAML parser on the hot path; config loads via yyjson `YYJSON_READ_JSON5`; plan artifacts are (strict) JSON.
2. Example/fixture configs may use JSON5 comments and trailing commas and still load.
3. `neo plan` prints valid workflows JSON; `neo run -v` prints structured step lines on stderr.
4. Docs and examples are JSON5-first and runnable without yaml files.
