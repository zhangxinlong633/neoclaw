# Neo `run` command — Plan then Run as primary entry

Date: 2026-09-04  
Status: approved for implementation planning

## Goal

Add `neo run "task"` as the primary one-shot entry: **plan a frozen DAG → validate → execute**.  
Keep `neo plan` for plan-only (inspect / save YAML). Keep `neo workflow run NAME` for pre-declared workflows.

## Decisions (from brainstorming)

| Topic | Choice |
|-------|--------|
| Semantics | **A** — `run` = Plan then Run; `plan` no longer executes |
| stdout for `run` | **A** — execution result only; YAML via `-o` or not on stdout |
| Implementation | **1** — thin CLI wrap around existing `plan_run`, plus a quiet-YAML flag |

## Non-goals

- Replanning mid-run / agentic control flow
- Replacing `workflow run` for named config workflows
- New DAG node types
- Persisting plans under a fixed `~/.neo/` layout (optional `-o` only)

## CLI

```text
neo run  [-o FILE] [-d] "task"    # plan → validate → execute
neo plan [-o FILE] [-d] "task"    # plan → validate; YAML on stdout
neo workflow run NAME             # unchanged
```

- Remove public `plan --run`. If `--run` is passed with `plan`, fail with a short message: use `neo run`.
- Shared options: `-c` / `-p` / `-m` / `-d` / `-o` (save planned workflows YAML).
- `-o` works for both `run` and `plan`.

### Output contract

| Command | stdout | stderr |
|---------|--------|--------|
| `run` | Workflow execution result only | Progress (`validated`, `executing`, dag/tool logs); save notice if `-o` |
| `plan` | Planned `workflows:` YAML | Validation progress; save notice if `-o` |

## Internal design

Reuse `plan_run` in `src/plan.c` with an explicit quiet flag:

```c
int plan_run(const agent_config_t *conf, const char *task,
             int do_run, int quiet_yaml, const char *save_path, int debug);
```

| Caller | `do_run` | `quiet_yaml` |
|--------|----------|--------------|
| `neo plan` | 0 | 0 (YAML → stdout) |
| `neo run` | 1 | 1 (no YAML on stdout) |

Behavior unchanged otherwise: LLM plan → extract YAML → optional `-o` → materialize/validate → optional `workflow_run` on first planned workflow.

`main.c`:

- Add `run` subcommand (same config load path as `plan`).
- Drop `--run` from help; reject `--run` when mode is `plan`.
- Update usage strings.

## Docs

- `docs/workflow.md`: document `neo run`; show `plan` as plan-only; remove `plan --run` examples.
- `README.md`: one-line entry for `neo run`.

## Testing

- Unit: existing `test_plan_extract` stays; no API required.
- Manual / live smoke (DeepSeek or any configured model):
  - `neo plan "…"` → YAML on stdout, exit 0 after validate.
  - `neo run "…"` → no YAML block as primary stdout; tool/llm result present; exit 0.
  - `neo plan --run "…"` → non-zero + hint to use `neo run`.

## Compatibility

- Scripts that used `neo plan --run` must switch to `neo run`.
- `workflow run` and bare `./neo "message"` unchanged.
