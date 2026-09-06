# Flexible DAG Phase 1 (F1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add multi-way deterministic `route.cases` and optional `retry.max` on tool steps without breaking legacy `match`/`then`/`else`.

**Architecture:** Extend `workflow_step_t` in config; parse/validate in `config.c`; select case + skip non-selected arms in `workflow.c`; retry only inside `wf_run_tool_step`. Keep LLM out of scheduling.

**Tech Stack:** C99 Neo workflow runner, JSON5 via yyjson, `make test` / `tests/test_workflow_dag`.

**Spec:** [`docs/superpowers/specs/2026-09-06-flexible-dag-f1-design.md`](../specs/2026-09-06-flexible-dag-f1-design.md)

## Global Constraints

- Near-term routing stays deterministic; no `type: decide`.
- Do not build Temporal / graph-level retry engines / parallelism.
- Legacy route fixtures `route_demo` and `route_merge` must keep passing.
- `retry.max` only on `type: tool`, clamped to 0..3.
- When `cases` is non-empty, ignore top-level `match`/`then`/`else` for branching.
- Complex C needs Chinese comments (AGENTS §7).
- Product triad unchanged; step field `"tools"` unchanged.

---

## File map

| File | Role |
|------|------|
| `src/core/config.h` | `wf_route_case_t`, step fields |
| `src/core/config.c` | parse / free / validate |
| `src/workflow/workflow.c` | case selection + tool retry |
| `tests/fixtures/workflow_dag.json5` | `route_cases`, `tool_retry` workflows |
| `tests/test_workflow_dag.c` | assertions |
| `docs/workflow.md` | document cases + retry |
| `docs/architecture.md` | §8.1 one-line update |

---

### Task 1: Data model + parse/validate `cases` and `retry`

**Files:**
- Modify: `src/core/config.h`
- Modify: `src/core/config.c`
- Test: extend fixture later in Task 3; this task verified by compile + existing `make test` still green after Task 2

**Interfaces:**
- Produces:
  - `typedef struct { char *match; char **then_ids; int then_count; } wf_route_case_t;`
  - On `workflow_step_t`: `wf_route_case_t *route_cases; int route_case_count; int retry_max;`
  - `#define WF_MAX_ROUTE_CASES 16`

- [ ] **Step 1: Extend `config.h`**

Add after route fields on `workflow_step_t`:

```c
#define WF_MAX_ROUTE_CASES 16

typedef struct {
  char *match; /* NULL or empty => default arm */
  char **then_ids;
  int then_count;
} wf_route_case_t;
```

Fields on step:

```c
  wf_route_case_t *route_cases;
  int route_case_count;
  int retry_max; /* tool only; extra attempts after first failure; 0..3 */
```

- [ ] **Step 2: Free new fields in `config_free` path**

In the per-step free block that frees `route_else`, also free each `route_cases[i].match`, each then id, `then_ids`, then `route_cases`.

- [ ] **Step 3: Parse `cases` and `retry` in `config_append_workflow_val`**

After parsing legacy then/else:

```c
    /* cases: multi-way route; when present, runtime ignores match/then/else */
    cases_a = yyjson_obj_get(st, "cases");
    if (yyjson_is_arr(cases_a)) {
      size_t ci, cn = yyjson_arr_size(cases_a);
      if (cn > WF_MAX_ROUTE_CASES) { /* error */ return -1; }
      s->route_cases = calloc(cn, sizeof(wf_route_case_t));
      if (!s->route_cases) return -1;
      s->route_case_count = (int)cn;
      for (ci = 0; ci < cn; ci++) {
        yyjson_val *co = yyjson_arr_get(cases_a, ci);
        yyjson_val *th;
        s->route_cases[ci].match = yy_dup_str(yyjson_obj_get(co, "match"));
        th = yyjson_obj_get(co, "then");
        if (th) {
          snprintf(pathbuf, sizeof(pathbuf), "%s/steps/%zu/cases/%zu/then", ctx, si, ci);
          if (yy_string_array(th, &s->route_cases[ci].then_ids,
                              &s->route_cases[ci].then_count, MAX_ARGV, pathbuf) != 0)
            return -1;
        }
      }
    }
    retry_v = yyjson_obj_get(st, "retry");
    if (yyjson_is_obj(retry_v)) {
      yyjson_val *rm = yyjson_obj_get(retry_v, "max");
      if (yyjson_is_int(rm) || yyjson_is_uint(rm)) {
        int m = (int)yyjson_get_sint(rm);
        if (m < 0) m = 0;
        if (m > 3) m = 3;
        s->retry_max = m;
      }
    }
```

- [ ] **Step 4: Validate**

For `WF_STEP_ROUTE`:
- If `route_case_count > 0`: require `on`; count default arms (`!match || !match[0]`) ≤ 1; every then id exists; allow empty then arrays.
- Else: keep existing then/else checks.

For any step with `retry_max > 0` and `type != WF_STEP_TOOL`: error.

- [ ] **Step 5: Commit**

```bash
git add src/core/config.h src/core/config.c
git commit -m "feat(config): parse route cases and tool retry.max"
```

---

### Task 2: Runtime — multi-way route + tool retry

**Files:**
- Modify: `src/workflow/workflow.c`
- Consumes: `route_cases`, `retry_max` from Task 1

- [ ] **Step 1: Helper to pick case index**

```c
/* 返回选中 cases 下标；无 cases 返回 -1。default_idx 可为 -1。 */
static int wf_route_select_case(const workflow_step_t *st, const char *expanded) {
  int i, def = -1;
  if (!st || st->route_case_count < 1) return -1;
  for (i = 0; i < st->route_case_count; i++) {
    const char *m = st->route_cases[i].match;
    if (!m || !m[0]) {
      def = i;
      continue;
    }
    if (expanded && strstr(expanded, m)) return i;
  }
  return def;
}
```

- [ ] **Step 2: In `wf_run_dag` route branch**

If `st->route_case_count > 0`:
- `sel = wf_route_select_case(st, expanded)`
- For each case `i != sel`, `wf_mark_skip_ids(..., then_ids, then_count)`
- If `sel < 0`, skip all cases' then ids
- Map output text: `case:N` or `default` / `none`
- Verbose: log `case=%d` 

Else: keep existing hit/then/else logic.

- [ ] **Step 3: Retry in `wf_run_tool_step`**

```c
  int attempts = 1 + (st->retry_max > 0 ? st->retry_max : 0);
  int attempt;
  for (attempt = 0; attempt < attempts; attempt++) {
    /* existing dispatch */
    if (success) break;
    if (verbose && attempt + 1 < attempts)
      fprintf(stderr, "neo: workflow:%s step:%s: retry attempt %d/%d\n",
              wf_name, st->id, attempt + 2, attempts);
  }
```

Only retry when dispatch returns failure / non-zero exit as today.

- [ ] **Step 4: `make` and run existing dag test**

```bash
make tests/test_workflow_dag && ./tests/test_workflow_dag
```

Expected: `ok` (legacy routes still pass).

- [ ] **Step 5: Commit**

```bash
git add src/workflow/workflow.c
git commit -m "feat(workflow): multi-way route cases and tool step retry"
```

---

### Task 3: Fixtures + unit coverage

**Files:**
- Modify: `tests/fixtures/workflow_dag.json5`
- Modify: `tests/test_workflow_dag.c`

- [ ] **Step 1: Add workflow `route_cases`**

Three arms: seed writes nothing special — use fixed `on: "choose docx file"`; cases match `pdf` → `take_pdf`, `docx` → `take_docx`, default → `take_other`. Only `take_docx` should run → count.out lines = 2 (seed + take_docx).

- [ ] **Step 2: Add workflow `tool_retry`**

Use a command that fails once then succeeds, OR simpler: `retry.max: 0` smoke is weak. Prefer script:

Create `tests/fixtures/bin/flaky_count.sh` that fails if `tests/fixtures/flaky.flag` missing then creates flag and fails; second run succeeds and appends to count.out.

Actually keep it simpler for Neo: use existing pattern — a small shell:

```bash
#!/bin/sh
FLAG=tests/fixtures/flaky.flag
if [ ! -f "$FLAG" ]; then touch "$FLAG"; exit 1; fi
echo ok >> tests/fixtures/count.out
exit 0
```

Workflow: one tool step with `retry.max: 1`, unlink flag before run; expect 1 line in count.out and run status ok.

- [ ] **Step 3: Assert in `test_workflow_dag.c`**

After existing tests, run `route_cases` and `tool_retry` with line counts / rc checks. Unlink `flaky.flag` and `count.out` appropriately.

- [ ] **Step 4: Run**

```bash
make test
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add tests/fixtures/workflow_dag.json5 tests/fixtures/bin/flaky_count.sh tests/test_workflow_dag.c
git commit -m "test: cover route cases and tool retry"
```

---

### Task 4: Docs

**Files:**
- Modify: `docs/workflow.md`
- Modify: `docs/architecture.md` (§8.1 row)

- [ ] **Step 1: Document `cases` + `retry` in workflow.md** with JSON5 examples; note legacy still works.
- [ ] **Step 2: architecture §8.1** — add “多路 route / tool retry” under 近端.
- [ ] **Step 3: Commit**

```bash
git add docs/workflow.md docs/architecture.md docs/superpowers/specs/2026-09-06-flexible-dag-f1-design.md docs/superpowers/plans/2026-09-06-flexible-dag-f1.md
git commit -m "docs: flexible DAG phase-1 route cases and retry"
```

---

## Spec coverage check

| Spec item | Task |
|-----------|------|
| `cases` parse + validate | 1 |
| Legacy route unchanged | 2 + 3 |
| Case select + skip | 2 |
| `retry.max` 0..3 tool-only | 1 + 2 |
| Tests | 3 |
| Docs | 4 |
| F2/F3 | out of scope (spec §3 only) |
