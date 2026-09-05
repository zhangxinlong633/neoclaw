# Plan task-type routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach the DAG planner to use 1–3 llm steps for knowledge Q&A and reserve the ~10-step R&D pipeline for engineering tasks.

**Architecture:** Prompt-only change in `plan_build_system_prompt`; extend `test_plan_extract` string checks; sync workflow/README docs. Soft default remains 10.

**Tech Stack:** C (`src/plan.c`), existing unit test harness, Markdown docs.

## Global Constraints

- Soft default `PLAN_DEFAULT_TARGET_STEPS` stays **10**.
- No C-side keyword heuristics.
- Knowledge tasks must not be forced to pad to N steps.

---

## File map

| File | Role |
|------|------|
| `src/plan.c` | Rewrite planner system prompt task-routing rules |
| `tests/test_plan_extract.c` | Assert knowledge vs engineering guidance strings |
| `docs/workflow.md` | Dual-mode docs |
| `README.md` | One-line Plan then Run blurb |
| `docs/superpowers/specs/2026-09-05-team-pipeline-design.md` | Note supersession for Q&A |

---

### Task 1: Failing prompt assertions

**Files:** `tests/test_plan_extract.c`

- [ ] Extend `plan_build_system_prompt` checks: must contain knowledge routing cue (e.g. `knowledge`) and forbid the old “Even for Q&A” phrase; must mention engineering / team pipeline and soft N.
- [ ] Run `make tests/test_plan_extract && ./tests/test_plan_extract` — expect fail until Task 2.

### Task 2: Update planner prompt

**Files:** `src/plan.c`

- [ ] Replace default-shape + “Even for Q&A…” block with knowledge vs engineering rules per spec.
- [ ] Keep JSON output format, tool allowlist, route/loop retry guidance for engineering.
- [ ] Soft N: “for engineering aim about N; for knowledge prefer 1 (max 3), do not pad to N”.
- [ ] Re-run unit test — pass.

### Task 3: Docs

**Files:** `docs/workflow.md`, `README.md`, team-pipeline design note

- [ ] Document dual mode; keep `--steps` / `plan.target_steps` semantics.
- [ ] `make test` green.
