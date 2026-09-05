# Neo plan/run target step count (soft)

Date: 2026-09-05  
Status: approved for implementation

## Goal

Let users **specify** a target DAG size for auto-planned workflows; **default ~10 steps**. Soft preference only (prompt), not hard validation.

## Decisions

| Topic | Choice |
|-------|--------|
| UX | CLI `--steps N` + optional `plan.target_steps` in config |
| Default | 10 when neither CLI nor config set |
| Enforcement | Soft only — guide the planner prompt; do not fail/retry on count mismatch |
| Cap | Clamp `N` to 1..32 (same as `MAX_PATHS` step limit) |

## CLI

```text
neo plan [--steps N] [-o FILE] "task"
neo run  [--steps N] [-o FILE] "task"
```

- `--steps` / optional short form not required beyond `--steps`.
- Invalid `N` → error and exit 1.

## Config (optional)

```yaml
plan:
  target_steps: 10
```

Resolution order: CLI `--steps` > `plan.target_steps` > **10**.

## Internal

- Parse `plan:` section in `config_load_file` → `agent_config_t.plan.target_steps` (0 = unset).
- `plan_build_system_prompt(conf, target_steps)` embeds: aim for about N steps.
- Replace “Prefer small DAGs (2-8 steps)” with the soft N guidance.
- `plan_run(..., int target_steps, ...)` receives resolved N from `main`.

## Docs / tests

- Update `doc/workflow.md`, README help line, `-h`.
- Unit: prompt builder includes target when N=10/15 (no API).
- Live smoke optional: `neo plan --steps 3 "..."` still validates.

## Non-goals

- Hard min/max reject or auto-retry
- Skeleton YAML / `--from` file (deferred)
- Raising `MAX_PATHS` beyond 32
