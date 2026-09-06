# Baseline A/B/C Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship workspace flagship DAG, harden plan `use` selection tests/UX, and document the default capability/DAG pack combo.

**Architecture:** Thin vertical slice on existing loaders—no new manifest scenes. A is one JSON5 DAG; B exposes a small synthesizer API for unit tests and clarifies catalog listing/stderr; C is docs + example config comments.

**Tech Stack:** C99 Neo core (`plan.c`, `workflow_dir.c`), JSON5 DAG/capability packs, `make test`.

**Spec:** [`docs/superpowers/specs/2026-09-06-baseline-abc-design.md`](../specs/2026-09-06-baseline-abc-design.md)

## Global Constraints

- Top-level config key remains `capability_matrix`; LLM step field remains `"tools": "on"|"off"`.
- Do not enable `shell_enabled` by default; do not add parallel tool namespaces.
- Complex C logic needs Chinese comments (AGENTS §7).
- New/changed directories need formal README updates in the same change.
- Prefer `WORKSPACE_BRIEF.md` over mutating `MEMORY.md`.

---

## File map

| File | Role |
|------|------|
| `dags/workspace/workspace_brief.json5` | Flagship DAG (A) |
| `dags/README.md` | Mention flagship + packs link |
| `src/workflow/plan.h` / `plan.c` | Export synthesizer; clearer unknown stderr |
| `src/workflow/workflow_dir.c` | `DAG:` prefix in catalog listing |
| `tests/test_plan_extract.c` | Tests for synthesizer + catalog prefix |
| `docs/examples.md` | Packs table + workspace_brief example |
| `capabilities/README.md` | Link to packs section |
| `config/config.json5.example` | Comment pointing at packs |
| `README.md` / `README_zh.md` | One sample command each |
| `docs/README.md` | Optional one-line packs note |

---

### Task 1: A — Add `workspace_brief` DAG

**Files:**
- Create: `dags/workspace/workspace_brief.json5`
- Modify: `dags/README.md`

- [ ] **Step 1:** Add DAG with steps `ls` → `brief` → `save` as in spec (§3), including `when` / `when_not` / `requires` / `outcome`.
- [ ] **Step 2:** Update `dags/README.md` workspace row to mention `workspace_brief` as the multi-step SOP sample.
- [ ] **Step 3:** Commit: `feat(dags): add workspace_brief SOP sample`

---

### Task 2: B — Testable synthesizer + catalog/stderr clarity

**Files:**
- Modify: `src/workflow/plan.h`, `src/workflow/plan.c`, `src/workflow/workflow_dir.c`, `tests/test_plan_extract.c`

- [ ] **Step 1:** Write failing tests: `plan_workflows_json_for_tools` output contains `adhoc_tools` and tool name; catalog listing contains `DAG:` prefix for `dir_count`.
- [ ] **Step 2:** Run `tests/test_plan_extract` (or `make test`) and confirm new asserts fail.
- [ ] **Step 3:** Export `char *plan_workflows_json_for_tools(char **names, int n);` in `plan.h`; remove `static` in `plan.c`; Chinese comment retained.
- [ ] **Step 4:** Prefix catalog lines with `DAG: ` in `workflow_dir_catalog_listing`.
- [ ] **Step 5:** Improve unknown-name stderr to state neither catalog DAG nor matrix capability.
- [ ] **Step 6:** Run `make test`; confirm pass.
- [ ] **Step 7:** Commit: `fix(plan): harden use selection tests and catalog labels`

---

### Task 3: C — Packs docs + README samples

**Files:**
- Modify: `docs/examples.md`, `capabilities/README.md`, `dags/README.md` (if needed), `config/config.json5.example`, `README.md`, `README_zh.md`, `docs/README.md`

- [ ] **Step 1:** Add「开箱组合（默认包）」section to `docs/examples.md` with the table from the spec; add `workspace_brief` command under DAG samples.
- [ ] **Step 2:** Cross-link from capabilities/dags READMEs and `config.json5.example`.
- [ ] **Step 3:** Add one sample line to English/Chinese root READMEs.
- [ ] **Step 4:** Commit: `docs: document default capability/DAG pack combo`

---

### Task 4: Verify

- [ ] **Step 1:** `make test`
- [ ] **Step 2:** If `config/config.json5` exists with key: optional `./neo workflow run workspace_brief` smoke (do not commit `WORKSPACE_BRIEF.md` unless user wants).
