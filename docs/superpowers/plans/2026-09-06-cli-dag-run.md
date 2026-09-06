# CLI `dag run` + Unified `neo run` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename CLI `workflow run` → `dag run`, unify `neo run` so exact workflow-name match runs the DAG directly, else plan+execute; keep `workflow run` deprecated.

**Architecture:** All behavior changes stay in `src/cli/main.c` (parse + dispatch). Lookup uses existing `config_find_workflow`. No rename of config keys or `workflow_run` C API.

**Tech Stack:** C99 Neo CLI, shell CLI smoke (`tests/cli_capability_matrix.sh`), `make test`.

**Spec:** [`docs/superpowers/specs/2026-09-06-cli-dag-run-design.md`](../specs/2026-09-06-cli-dag-run-design.md)

## Global Constraints

- Exact name match only for `neo run` → DAG path (via `config_find_workflow`).
- Extra argv after `neo run <known-name>` → non-zero error (no silent drop).
- `--steps` / `-o` on DAG path → stderr warn + ignore (do not fail).
- `workflow run` keeps working with deprecation on stderr.
- Do not rename `workflows` / `workflow_directory` / `workflow_run()`.
- Update user-facing docs/README in the docs task; do not rewrite old historical specs' command strings unless touching those files anyway.

---

## File map

| File | Role |
|------|------|
| `src/cli/main.c` | Parse `dag`/`workflow`; `run` dispatch; usage text |
| `tests/cli_capability_matrix.sh` | Switch primary commands to `dag run`; add `neo run cli_count` + deprecation smoke |
| `docs/examples.md`, `docs/workflow.md`, `README.md`, `README_zh.md` | User examples |
| `config/profiles/demo/README.md` | Profile example command |

---

### Task 1: CLI smoke expectations first (TDD)

**Files:**
- Modify: `tests/cli_capability_matrix.sh`
- Test: same file (run via `make test-cli` or full `make test`)

**Interfaces:**
- Consumes: `./neo` binary built by Makefile
- Produces: failing assertions until Task 2 implements `dag run` / help text / unified `run`

- [ ] **Step 1: Update help assertion**

Change the help check from requiring `workflow run` to requiring `dag run`:

```bash
if [[ "$RC" -eq 0 ]] && grep -q 'dag run' <<<"$ERR$OUT"; then
  ok "help lists dag run"
else
  bad "help lists dag run (rc=$RC)"
fi
```

- [ ] **Step 2: Switch primary smoke commands to `dag run`**

Replace every successful-path `workflow run` invocation in this script with `dag run` (cli_count, cli_mcp, cli_run_command, cli_bad_tool, cli_fs_extras, cli_dir_echo, cli_propose, dir_count, matrix_off, demo profile). Keep assertion message text updated to say `dag run` where it describes the command.

Leave **one** call as `workflow run` for deprecation (Step 3).

- [ ] **Step 3: Add deprecation + unified `run` checks**

After the `cli_count` via `dag run` block, add:

```bash
# unified neo run: exact workflow name → direct DAG (no planner)
run_capture "$NEO" -c "$CFG" run cli_count
if [[ "$RC" -eq 0 ]]; then
  ok "run cli_count (name hit → DAG)"
else
  bad "run cli_count (rc=$RC err=$ERR)"
fi

# deprecated alias still works
run_capture "$NEO" -c "$CFG" workflow run cli_count
if [[ "$RC" -eq 0 ]] && grep -qi 'deprecated' <<<"$ERR"; then
  ok "workflow run deprecated but works"
else
  bad "workflow run deprecated (rc=$RC err=$ERR)"
fi
```

- [ ] **Step 4: Run CLI smoke — expect FAIL**

```bash
make neo && ./tests/cli_capability_matrix.sh
```

Expected: FAIL on help / `dag run` (command not implemented yet).

- [ ] **Step 5: Commit**

```bash
git add tests/cli_capability_matrix.sh
git commit -m "test(cli): expect dag run and unified neo run"
```

---

### Task 2: Implement `dag run`, deprecation, unified `run`

**Files:**
- Modify: `src/cli/main.c`

**Interfaces:**
- Consumes: `config_find_workflow(const agent_config_t *c, const char *name)` from `config.h`
- Consumes: `workflow_run(...)`, `plan_run(...)`
- Produces: CLI behavior per spec §2–§5

- [ ] **Step 1: Update `print_usage`**

```c
  fprintf(stderr, "       %s [OPTIONS] dag run NAME\n", prog);
  fprintf(stderr, "       %s [OPTIONS] plan [--steps N] [-o FILE] \"task\"\n", prog);
  fprintf(stderr, "       %s [OPTIONS] run NAME|\"task\" [--steps N] [-o FILE]\n", prog);
  /* ... */
  fprintf(stderr, "  dag run NAME        Run a declarative DAG from config/catalog\n");
  fprintf(stderr, "  plan \"task\"         LLM emits a DAG (validate only; JSON on stdout)\n");
  fprintf(stderr, "  run NAME|\"task\"     Run named DAG, or plan+execute a task\n");
  fprintf(stderr, "  workflow run NAME   Deprecated alias for dag run\n");
```

- [ ] **Step 2: Parse `dag run` and deprecate `workflow run`**

Replace the `workflow` block with shared handling:

```c
    if (strcmp(argv[arg_start], "dag") == 0 || strcmp(argv[arg_start], "workflow") == 0) {
      int deprecated = (strcmp(argv[arg_start], "workflow") == 0);
      if (arg_start + 2 >= argc || strcmp(argv[arg_start + 1], "run") != 0) {
        fprintf(stderr, "neo: usage: %s run NAME\n", deprecated ? "workflow" : "dag");
        return 1;
      }
      if (deprecated)
        fprintf(stderr, "neo: 'workflow run' is deprecated; use 'dag run' or 'run'\n");
      workflow_mode = 1;
      workflow_name = argv[arg_start + 2];
      arg_start += 3;
      continue;
    }
```

Keep the existing `if (workflow_mode) { ... workflow_run ... }` block unchanged.

- [ ] **Step 3: Split `run` into DAG-hit vs plan**

After config load inside `if (plan_mode || run_mode)`, for **`run_mode` only**:

```c
    if (run_mode) {
      const char *arg0 = argv[arg_start];
      if (config_find_workflow(&conf, arg0)) {
        if (arg_start + 1 < argc) {
          fprintf(stderr, "neo: run: unexpected arguments after workflow name '%s'\n", arg0);
          config_free(&conf);
          return 1;
        }
        if (cli_steps || plan_out)
          fprintf(stderr, "neo: run: --steps/-o ignored when running named DAG\n");
        {
          char *out = NULL;
          int wr = workflow_run(&conf, arg0, &out, verbose);
          if (wr == 0 && out) fputs(out, stdout);
          if (out && out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
          free(out);
          config_free(&conf);
          return wr != 0;
        }
      }
    }
    /* plan_mode, or run_mode miss → existing plan_run */
    r = plan_run(&conf, argv[arg_start], run_mode ? 1 : 0, run_mode ? 1 : 0, cli_steps, plan_out,
                 debug, verbose);
```

Note: load config **before** the name lookup (already true). Do not call `plan_run` when name hits.

- [ ] **Step 4: Run CLI smoke — expect PASS**

```bash
make neo && ./tests/cli_capability_matrix.sh
```

Expected: all ok, including `run cli_count` and deprecated `workflow run`.

- [ ] **Step 5: Full test + commit**

```bash
make test
```

Expected: all pass.

```bash
git add src/cli/main.c
git commit -m "feat(cli): dag run and unified neo run name dispatch"
```

---

### Task 3: User docs

**Files:**
- Modify: `docs/examples.md`
- Modify: `docs/workflow.md` (any `workflow run` CLI examples)
- Modify: `README.md`, `README_zh.md`
- Modify: `config/profiles/demo/README.md`

- [ ] **Step 1: Replace user-facing `workflow run` with `dag run`**

In examples and README tables/commands, prefer:

```bash
./neo dag run show_time
./neo run show_time          # same when name exists in catalog
./neo run "看下系统时间"       # plan+execute when not a workflow name
```

Add one short note that `workflow run` is deprecated.

- [ ] **Step 2: `docs/workflow.md` running section**

If it documents CLI, show `dag run` / unified `run`; keep JSON key `workflows` unchanged in config examples.

- [ ] **Step 3: Commit**

```bash
git add docs/examples.md docs/workflow.md README.md README_zh.md config/profiles/demo/README.md
git commit -m "docs: prefer dag run and unified neo run"
```

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| `dag run NAME` | 2 |
| `workflow run` deprecated | 1 + 2 |
| `neo run` name hit → DAG | 2 |
| Extra args error | 2 |
| `--steps`/`-o` warn on DAG | 2 |
| Help text | 1 + 2 |
| CLI smoke | 1 + 2 |
| User docs | 3 |
| Config/C API rename | out of scope |

## Self-review

- No placeholders; uses existing `config_find_workflow`.
- Plan path for unknown names unchanged (`plan_run`).
- Primary smoke uses offline fixtures (`cli_count`) so CI need not call LLM for the new `run` name-hit case.
