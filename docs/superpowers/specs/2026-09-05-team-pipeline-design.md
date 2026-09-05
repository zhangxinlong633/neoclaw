# Default R&D-team DAG for neo plan/run

Date: 2026-09-05  
Status: approved for implementation

## Goal

By default, `neo plan` / `neo run` plan a **~10-step software R&D team pipeline** (requirements → decompose → implement → verify with limited retry → summarize), via **planner prompt only** (no new runner).

## Decisions

| Topic | Choice |
|-------|--------|
| Retry on verify fail | **A** — frozen DAG with `route`/`loop`, max ~2 retries back to implement/decompose |
| When enabled | **A** — default for all plan/run |
| Implementation | **1** — prompt convention only |

## Default step roles (~10)

| # | Role | Typical type |
|---|------|----------------|
| 1 | Understand business requirements | `llm` |
| 2–3 | Decompose / design | `llm` (optional `tool`) |
| 4–7 | Implement | `llm` / `tool` |
| 8–9 | Verify | `llm` / `tool`; on failure `route`/`loop` back ≤2 times |
| 10 | Summarize deliverable | `llm` (final stdout) |

## Retry semantics

- Topology is emitted once and frozen.
- Verify steps should produce a clear signal (e.g. substring `PASS` / `FAIL`) for `route`.
- Limited return to implement (or decompose) via existing `loop`/`depends_on`/`route`; **max 2** retry cycles.
- No whole-graph replan mid-run.

## Soft step count

- Default target remains **10** (`plan.target_steps` / `--steps`).
- If `--steps N` differs, prompt says: scale the team roles to about N steps, but keep the phases requirements → implement → verify → summarize.

## Code changes

- `src/plan.c` — `plan_build_system_prompt`: embed team role table + retry rules; keep existing type/prompt/tool rules.
- `doc/workflow.md` (+ brief README) — document default team pipeline.
- Unit: prompt contains role keywords / "retry" guidance (extend `test_plan_extract`).

## Non-goals

- Built-in YAML skeleton merge
- Dedicated `team` state machine / multi-process agents
- Human approval gates
- Unbounded goto across arbitrary stages
