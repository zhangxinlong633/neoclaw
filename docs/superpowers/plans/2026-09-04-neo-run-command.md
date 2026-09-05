# Neo `run` command Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `neo run "task"` as Plan→validate→execute; `neo plan` plan-only; remove `plan --run`.

**Architecture:** Thin CLI wrap: extend `plan_run` with `quiet_yaml`; `run` calls `do_run=1, quiet_yaml=1`; `plan` calls `do_run=0, quiet_yaml=0`.

**Tech Stack:** C, existing `src/plan.c` / `src/main.c`, Make tests.

## Global Constraints

- stdout for `run`: execution result only (no planned YAML).
- Reject `plan --run` with hint to use `neo run`.
- No new DAG node types; reuse materialize + `workflow_run`.

---

### Task 1: `plan_run` quiet_yaml

**Files:**
- Modify: `src/plan.h`, `src/plan.c`
- Modify: `src/main.c` (call sites)

**Interfaces:**
- Produces: `int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_yaml, const char *save_path, int debug);`

- [x] Update signature; if `!quiet_yaml`, print YAML to stdout (current behavior); if `quiet_yaml`, skip that print (still allow `-o` save).
- [x] `neo plan` → `plan_run(..., 0, 0, plan_out, debug)`; prepare `neo run` → `plan_run(..., 1, 1, plan_out, debug)`.
- [x] `make && make test`

### Task 2: CLI `run` + reject `--run` on plan

**Files:**
- Modify: `src/main.c`

- [x] Add `run_mode`; parse `run` like `plan`.
- [x] If `plan` and `--run` seen: error `neo: use 'neo run' instead of 'plan --run'\n`, exit 1.
- [x] Remove `--run` from help; document `run` in usage.
- [x] Shared handler for plan/run config load.

### Task 3: Docs

**Files:**
- Modify: `docs/workflow.md`, `README.md`

- [x] Replace `plan --run` examples with `neo run`; plan = plan-only.

### Task 4: Verify

- [x] `make test`
- [x] Live: `./neo plan "…"`, `./neo run "…"`, `./neo plan --run "…"` (expect fail)
