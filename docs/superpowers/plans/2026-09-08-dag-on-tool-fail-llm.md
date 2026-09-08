# DAG on_tool_fail LLM Hotline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or implement inline. Steps use checkbox (`- [ ]`) syntax.

**Goal:** After local tool `retry.max` is exhausted, optionally ask LLM RETRY/ABORT and retry the same tool step.

**Architecture:** Parse `dag.on_tool_fail` into `agent_config_t.dag_runtime`; hook at end of `dag_run_tool_step`; expose reply parser for unit tests.

**Tech Stack:** C99 Neo, yyjson JSON5, `llm_chat`, `make test`.

**Spec:** [`docs/superpowers/specs/2026-09-08-dag-on-tool-fail-llm-design.md`](../specs/2026-09-08-dag-on-tool-fail-llm-design.md)

## Global Constraints

- Default `llm: false`.
- LLM tools off; no args mutation; no jump to other steps.
- Only `type: tool` failures.
- `max_calls` clamp 0..2.
- No agent Co-authored-by trailers in commits (AGENTS §8).

---

### Task 1: Config + reply parser

**Files:**
- Modify: `src/core/config.h`, `src/core/config.c`
- Modify: `src/dag/dag.h`, `src/dag/dag.c` (parser + export)
- Create: `tests/test_dag_on_fail.c`
- Modify: `Makefile`

- [ ] Add `dag_runtime_config_t` on `agent_config_t`; parse `dag.on_tool_fail`.
- [ ] Add `int dag_parse_fail_llm_reply(const char *text);` /* 1 RETRY, 0 ABORT */
- [ ] Unit tests for parser + config load fixture.
- [ ] `make test` green for new tests; commit.

### Task 2: Runtime hook in `dag_run_tool_step`

**Files:**
- Modify: `src/dag/dag.c`
- Modify: `docs/dag.md`, `docs/architecture.md`

- [ ] After local attempts fail, LLM hotline loop per spec.
- [ ] Docs; `make test`; commit without agent trailers.
