# Config JSON Cutover + Verbose Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hand-written YAML config parsing with yyjson **JSON5** loading (`YYJSON_READ_JSON5`, yyjson 0.12.0), migrate plan/fixtures/docs off YAML, and add `--verbose` structured workflow step logging on stderr.

**Architecture:** Rewrite `config_load_file` to parse a JSON5 object into the existing `agent_config_t` (no IR layer). Plan extract/materialize emit **strict JSON** (valid JSON5 on reload). Extend `workflow_run` / `plan_run` with a `verbose` flag; default stderr is quiet except errors and one `run done` line.

**Tech Stack:** C99, embedded yyjson 0.12.0 (`src/yyjson.c` / `YYJSON_READ_JSON5`), libcurl, Makefile tests under `tests/`.

**Spec:** `docs/superpowers/specs/2026-09-05-config-json-verbose-design.md`

## Global Constraints

- JSON5 on read (`YYJSON_READ_JSON5`): refuse `.yaml` / `.yml`; no YAML dual-read. **Default filenames use `.json5`** (fallback `.json`).
- Keep `agent_config_t` field layout; do not change workflow node semantics.
- Lists must be arrays (no bare-string-as-list sugar).
- Plan/write path emits strict JSON; config examples may use JSON5 comments / trailing commas.
- `-d` / `--debug` meaning unchanged; `-v` / `--verbose` is orthogonal.
- Successful non-verbose runs: at most one `neo: run done ...` summary on stderr; no per-step spam.
- Do not add MCP, parallel DAG, audit log files, or YAML→JSON runtime converter.
- Every task ends with `make test` green (or the subset named in the task if full link not yet possible).

## File map

| File | Responsibility |
|------|----------------|
| `src/config.c` / `src/config.h` | JSON5 load via yyjson; path rejection; pointer-style error paths |
| `src/plan.c` / `src/plan.h` | Extract/materialize/prompt JSON; `quiet_plan`; pass `verbose` |
| `src/workflow.c` / `src/workflow.h` | Step logging; `workflow_run(..., int verbose)` |
| `src/main.c` | Default `*.json5` paths (fallback `*.json`); `-v`; profile `neo.json5`; wire verbose |
| `Makefile` | Link `yyjson.c` into all tests that use `config.c` |
| `tests/fixtures/*.json` | JSON fixtures (delete `*.yaml` fixtures; strict JSON OK) |
| `tests/test_*.c` | Point at `.json`; plan extract JSON; verbose stderr checks |
| `config/config.json5.example` | Replace `config.yaml.example` |
| `README.md`, `docs/*.md`, `example/` | JSON5-first docs + short migrate note |

### Canonical JSON shapes (lock these)

```json
{
  "model": {
    "provider": "openrouter",
    "base_url": "https://example/v1",
    "name": "model",
    "api_key": "KEY",
    "max_tokens": 4096,
    "temperature": 0.7
  },
  "bootstrap": { "max_chars_per_file": 8000, "paths": ["AGENTS.md"] },
  "soul": { "path": "SOUL.md", "max_chars": 8000 },
  "rules": { "max_chars_per_file": 6000, "paths": ["RULES.md"] },
  "workspace": { "prompt_cwd": true },
  "skills": {
    "directory": "skills",
    "high_priority": ["nanjing"],
    "unmatched": "index",
    "paths": [{ "path": "skills/me/SKILL.md", "priority": "high" }]
  },
  "memory": { "path": "MEMORY.md", "max_chars": 4000 },
  "session": { "max_turns": 10 },
  "tools": {
    "enabled": true,
    "root": ".",
    "commands": [
      {
        "name": "echo_args",
        "description": "Echo",
        "argv": ["./scripts/tools/echo-args.sh"],
        "timeout_sec": 30,
        "max_output_bytes": 65536,
        "pass_args": "stdin_json"
      }
    ]
  },
  "plan": { "target_steps": 10 },
  "workflows": [
    {
      "name": "diamond",
      "steps": [
        { "id": "a", "type": "tool", "tool": "count_run", "args": {} },
        { "id": "b", "type": "tool", "tool": "count_run", "depends_on": ["a"], "args": {} }
      ]
    }
  ]
}
```

- Step field `"tools": "off"` | `"on"` (string) for LLM steps.
- Omit `"type"` when `"prompt"` present → coerce to `WF_STEP_LLM`.
- Route: `"on"`, `"match"`, `"then": []`, `"else": []`.
- `args` object → serialize with `yyjson_val_write` into `workflow_step_t.args_json`.

---

### Task 1: JSON fixtures + Makefile link yyjson into config tests

**Files:**
- Create: `tests/fixtures/commands_min.json`, `workflow_min.json`, `workflow_dag.json`, `workflow_tools_off.json`, `workflow_omit_type.json`, `plan_live.json` (convert from existing yaml)
- Delete: corresponding `tests/fixtures/*.yaml` (after conversion)
- Modify: `Makefile` — every test binary that links `src/config.c` must also link `src/yyjson.c` with `-Wno-unused-function -Wno-unused-parameter`
- Modify: `tests/test_parse_commands.c`, `tests/test_parse_workflows.c`, `tests/test_command_exec.c`, `tests/test_workflow_*.c` — load `*.json` paths

**Interfaces:**
- Consumes: existing `config_load_file` (still YAML until Task 2 — tests will fail after this task until Task 2 lands; **do Task 1+2 in one commit if preferred**, but write fixtures first)
- Produces: fixtures ready for JSON loader

- [ ] **Step 1: Convert `commands_min.yaml` → `commands_min.json`**

```json
{
  "model": {
    "base_url": "http://127.0.0.1:9",
    "name": "test",
    "api_key": "x"
  },
  "tools": {
    "enabled": true,
    "root": ".",
    "commands": [
      {
        "name": "echo_args",
        "description": "Echo arguments JSON",
        "argv": ["./scripts/tools/echo-args.sh"],
        "timeout_sec": 5,
        "max_output_bytes": 4096,
        "pass_args": "stdin_json"
      }
    ]
  },
  "skills": { "directory": "skills" },
  "memory": { "path": "MEMORY.md", "max_chars": 100 },
  "session": { "max_turns": 2 }
}
```

- [ ] **Step 2: Convert remaining fixtures** (`workflow_min`, `workflow_dag`, `workflow_tools_off`, `workflow_omit_type`, `plan_live`) to the same JSON conventions; preserve workflow names/ids/route graphs exactly.

- [ ] **Step 3: Update test sources to `*.json` paths** (search/replace fixture filenames).

- [ ] **Step 4: Update Makefile** so `TEST_PARSE_CMD`, `TEST_PARSE_WF`, `TEST_CMD_EXEC` link yyjson:

```makefile
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/config.c src/config.h src/yyjson.c
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -o $@ tests/test_parse_commands.c src/config.c src/yyjson.c
```

(Apply the same pattern to every target that compiles `src/config.c`.)

- [ ] **Step 5: Commit fixtures + Makefile + test path updates** (expect `make test` still red until Task 2)

```bash
git add tests/fixtures Makefile tests/test_*.c
git rm tests/fixtures/*.yaml
git commit -m "test: convert fixtures to JSON and link yyjson in config tests"
```

---

### Task 2: Rewrite `config_load_file` with yyjson

**Files:**
- Modify: `src/config.c` (replace line parser body of `config_load_file` and helpers)
- Modify: `src/config.h` only if adding a small helper declaration (prefer keep helpers static in `.c`)

**Interfaces:**
- Consumes: `yyjson_read_file` / `yyjson_read` + `yyjson_obj_get` / array iterators
- Produces: unchanged `int config_load_file(agent_config_t *c, const char *path);` filling `agent_config_t`

- [ ] **Step 1: At top of `config_load_file`, reject yaml extensions**

```c
static int path_has_ext(const char *path, const char *ext) {
  size_t lp, le;
  if (!path || !ext) return 0;
  lp = strlen(path);
  le = strlen(ext);
  if (lp < le) return 0;
  return strcmp(path + lp - le, ext) == 0;
}

/* inside config_load_file: */
if (path_has_ext(path, ".yaml") || path_has_ext(path, ".yml")) {
  fprintf(stderr,
          "neo: config is JSON5-only; migrate to .json (see docs/migrate-json.md)\n");
  return -1;
}
```

- [ ] **Step 2: Read file with yyjson**

```c
yyjson_read_err err;
/* JSON5: comments, trailing commas, unquoted keys, single quotes, etc. */
yyjson_doc *doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
if (!doc) {
  fprintf(stderr, "neo: JSON5 parse error in %s: %s (at %zu)\n",
          path, err.msg ? err.msg : "unknown", (size_t)err.pos);
  return -1;
}
yyjson_val *root = yyjson_doc_get_root(doc);
if (!yyjson_is_obj(root)) {
  fprintf(stderr, "neo: config root must be a JSON object\n");
  yyjson_doc_free(doc);
  return -1;
}
```

- [ ] **Step 3: Implement section fillers** (static helpers), each taking `(agent_config_t *c, yyjson_val *sec, const char *base_ptr)` where `base_ptr` is used in error messages like `/workflows/0/steps/1`:

- `fill_model`, `fill_memory`, `fill_soul`, `fill_workspace`, `fill_session`
- `fill_bootstrap` — expect `paths` string array; `max_chars_per_file` number
- `fill_rules` — same as bootstrap
- `fill_skills` — `directory`, `unmatched` (`"skip"`→1), `high_priority` string array, `paths` array of `{path, priority?}`
- `fill_tools` — bools/numbers/strings; `commands` array; `pass_args` `"env"`→1 else stdin_json; `argv` string array
- `fill_plan` — `target_steps` int
- `fill_workflows` — array of workflow objects; steps with `depends_on`/`then`/`else`/`over` as string arrays; coerce omit-type+prompt→LLM; `tools` step string on/off

On type errors:

```c
fprintf(stderr, "neo: config error at %s: expected %s\n", json_path, expected);
```

- [ ] **Step 4: Delete obsolete YAML helpers** that are unused (`trim_quotes` line section flags, indent section switching). Keep `dup_str`, `parse_bracket_list` only if still needed — prefer delete `parse_bracket_list` and use yyjson arrays only.

- [ ] **Step 5: Run tests**

```bash
make clean && make test
```

Expected: `ok` from all test binaries.

- [ ] **Step 6: Commit**

```bash
git add src/config.c src/config.h
git commit -m "feat: load agent config from JSON via yyjson"
```

---

### Task 3: Default paths, profile `neo.json5`, CLI rejection message

**Files:**
- Modify: `src/main.c` (`default_config_path`, profile branch, help text)
- Create: `tests/test_config_path_reject.c` (optional small test) **or** extend `tests/test_parse_workflows.c` with a temp `.yaml` path check

**Interfaces:**
- Produces: default lookup `config/config.json5` → `config.json5` → `config/config.json` → `config.json`; profile uses `neo.json5` then `neo.json`

- [ ] **Step 1: Change default path helper**

```c
static const char *default_config_path(void) {
  if (access("config/config.json5", R_OK) == 0) return "config/config.json5";
  if (access("config.json5", R_OK) == 0) return "config.json5";
  if (access("config/config.json", R_OK) == 0) return "config/config.json";
  if (access("config.json", R_OK) == 0) return "config.json";
  return "config/config.json5";
}
```

- [ ] **Step 2: Profile without `-c` tries `neo.json5` then `neo.json`** (replace `neo.yaml`).

- [ ] **Step 3: Update `-h` text** to mention JSON5 paths.

- [ ] **Step 4: Add rejection test** — write a tiny yaml file under `tests/fixtures/legacy.yaml` and assert `config_load_file` returns nonzero (file can stay as negative fixture; never auto-loaded).

```c
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/legacy.yaml") == 0) {
    fprintf(stderr, "yaml should be rejected\n");
    return 1;
  }
  config_free(&c);
```

Create `tests/fixtures/legacy.yaml` with one line `model: {}` solely for this test.

- [ ] **Step 5: `make test` + commit**

```bash
git commit -m "feat: default to config.json5 and reject yaml configs"
```

---

### Task 4: Plan extract / materialize / prompt → JSON

**Files:**
- Modify: `src/plan.h`, `src/plan.c`
- Modify: `tests/test_plan_extract.c`

**Interfaces:**
- Produces:
  - `int plan_extract_workflows_json(const char *llm_text, char **out_json);`
  - `int plan_materialize_config(const agent_config_t *base, const char *workflows_json, agent_config_t *out_conf, char *out_path, size_t out_path_sz);` (writes temp `.json`)
  - `int plan_run(..., int quiet_plan, int target_steps, const char *save_path, int debug, int verbose);`
- Removes: `plan_extract_workflows_yaml` / `quiet_yaml` names

- [ ] **Step 1: Rewrite extract** — prefer fenced ` ```json ` … ` ``` `; else find first `{` whose parse has array `"workflows"`. Pretty-print result with `yyjson_write` (`YYJSON_WRITE_PRETTY`) into `*out_json`.

- [ ] **Step 2: Update failing test first** in `tests/test_plan_extract.c`:

```c
  const char *fenced =
      "Sure:\n```json\n"
      "{\"workflows\":[{\"name\":\"planned\",\"steps\":["
      "{\"id\":\"a\",\"type\":\"llm\",\"prompt\":\"hello\"}]}]}"
      "\n```\n";
  if (plan_extract_workflows_json(fenced, &json) != 0) { ... }
  fails += expect_contains(json, "\"workflows\"", "fenced");
  fails += expect_contains(json, "planned", "fenced");
```

Bare object without fence; reject `"no json here"`.

- [ ] **Step 3: Materialize** — write temp file like `/tmp/neo-plan-XXXXXX.json` containing:

```json
{
  "model": { ... from base ... },
  "tools": { ... from base ... },
  "memory": { ... },
  "session": { "max_turns": ... },
  "workflows": [ ... from extracted document ... ]
}
```

Implementation approach: build with `yyjson_mut_doc`, or fprintf a minimal JSON merging `workflows` array from extracted doc. Prefer mut doc + `yyjson_mut_write_file`. Then `config_load_file(out_conf, path)`.

- [ ] **Step 4: Update `plan_build_system_prompt`** — instruct ```json output with `"workflows":[...]`; remove YAML examples; keep team-pipeline / target_steps guidance.

- [ ] **Step 5: `plan_run`** — use extract JSON; `if (!quiet_plan) fputs(json, stdout)`; rename param; thread `verbose` into `workflow_run` (Task 5 may stub `verbose` unused until then — pass through).

- [ ] **Step 6: `make test` + commit**

```bash
git commit -m "feat: plan extract and materialize workflows as JSON"
```

---

### Task 5: `--verbose` + workflow step summaries

**Files:**
- Modify: `src/workflow.h`, `src/workflow.c`
- Modify: `src/plan.h`, `src/plan.c` (pass verbose)
- Modify: `src/main.c` (parse `-v` / `--verbose`, pass to workflow/plan)
- Modify: `tests/test_workflow_dag.c` (run with verbose via env **or** add `workflow_run` verbose=1 and capture — simplest: call internal only through `workflow_run(..., 1)` and allow stderr)

**Interfaces:**
- Produces: `int workflow_run(const agent_config_t *conf, const char *workflow_name, char **out_text, int verbose);`

- [ ] **Step 1: Update signature** in `workflow.h` and all call sites (`main.c`, `plan.c`, tests).

- [ ] **Step 2: Timing + log helpers** in `workflow.c`:

```c
static long neo_now_ms(void); /* clock_gettime or gettimeofday */

static void neo_step_log_start(int verbose, const char *wf, const workflow_step_t *st) {
  if (!verbose) return;
  fprintf(stderr, "neo: step start workflow=%s id=%s type=%s",
          wf, st->id, /* type name */);
  if (st->type == WF_STEP_TOOL && st->tool)
    fprintf(stderr, " tool=%s", st->tool);
  fputc('\n', stderr);
}

static void neo_step_log_end(int verbose, const char *wf, const char *id,
                             const char *status, const char *reason, long ms, ...) {
  if (!verbose && strcmp(status, "error") != 0) return; /* errors always */
  ...
}
```

Default (verbose=0): on overall success print one:

```c
fprintf(stderr, "neo: run done workflow=%s status=ok steps=%d skipped=%d\n", ...);
```

Remove or gate existing unconditional `neo dag:` / success `neo tool:` prints behind `verbose`.

- [ ] **Step 3: CLI** — `int verbose = 0;` parse `-v` / `--verbose`; pass into `workflow_run` and `plan_run`.

- [ ] **Step 4: Help text** documents `-v`.

- [ ] **Step 5: Test** — in `test_workflow_dag.c`, after diamond run with `verbose=1`, do not assert stderr content if hard; instead add `tests/test_workflow_verbose.c` that runs diamond with verbose=1 and checks nothing crashes + exit 0. Optional: freopen stderr to a temp file and `strstr` for `neo: step start`.

Recommended stderr capture:

```c
  char tmpl[] = "/tmp/neo-verb-XXXXXX";
  int fd = mkstemp(tmpl);
  int saved = dup(2);
  dup2(fd, 2);
  workflow_run(&c, "diamond", &out, 1);
  fflush(stderr);
  dup2(saved, 2);
  close(saved);
  /* read tmpl, expect "neo: step start" and "neo: run done" */
```

- [ ] **Step 6: `make test` + commit**

```bash
git commit -m "feat: add --verbose structured workflow step logs"
```

---

### Task 6: Example config + docs migration

**Files:**
- Create: `config/config.json5.example`, `docs/migrate-json.md`
- Delete: `config/config.yaml.example`
- Modify: `README.md`, `docs/tool.md`, `docs/workflow.md`, `docs/claw.md`, `example/README.md`, and any remaining `config.yaml` / `neo.yaml` / `workflows:` YAML fences in `docs/superpowers/specs/*.md` that describe runtime config (update to JSON5; historical plan files may note “superseded by JSON5” only where they instruct implementers)

**Interfaces:** none (docs only)

- [ ] **Step 1: Write `config/config.json5.example`** from current yaml example using canonical shapes; **use JSON5 comments** (`//` / `/* */`) and trailing commas where helpful (must still load with `YYJSON_READ_JSON5`).

- [ ] **Step 2: Write `docs/migrate-json.md`** — keys unchanged, arrays required, JSON5 comments OK, path table yaml→json5, profile `neo.json5`, note yyjson 0.12.0 `YYJSON_READ_JSON5`.

- [ ] **Step 3: Update user-facing docs** (README quick start `cp config/config.json5.example config/config.json5`, workflow/plan examples, claw config paths).

- [ ] **Step 4: Update `.gitignore`** — ignore secret configs:

```
config.json5
config/config.json5
config.json
config/config.json
```

Keep ignoring yaml paths too (harmless).

- [ ] **Step 5: Commit**

```bash
git add config docs README.md example .gitignore
git rm config/config.yaml.example
git commit -m "docs: migrate config examples and guides to JSON5"
```

---

### Task 7: Final verification

**Files:** none (verify only)

- [ ] **Step 1: Full rebuild**

```bash
make clean && make && make test
```

Expected: all tests print `ok`, exit 0.

- [ ] **Step 2: Grep hot path for yaml parser leftovers**

```bash
rg -n 'jsmn|workflows:|config\\.yaml|neo\\.yaml|quiet_yaml|plan_extract_workflows_yaml' src tests Makefile README.md docs/tool.md docs/workflow.md docs/claw.md docs/migrate-json.md config
```

Expected: no hits in `src/` except possible migration error string mentioning `.yaml`; docs may mention yaml only inside `migrate-json.md`.

- [ ] **Step 3: Smoke help**

```bash
./neo -h
```

Expected: mentions `config.json5`, `-v` / `--verbose`, plan/run.

- [ ] **Step 4: Final commit only if cleanup edits were needed; otherwise done.**

---

## Self-review (plan vs spec)

| Spec requirement | Task |
|------------------|------|
| JSON-only config via yyjson into `agent_config_t` | Task 2 |
| Refuse `.yaml` + migration hint | Task 2 + 3 + 6 |
| Default `config/config.json5`, profile `neo.json5` | Task 3 |
| Plan extract/stdout/materialize JSON; rename quiet | Task 4 |
| Path-aware config errors | Task 2 |
| Default one `run done`; `-v` step lines; `-d` unchanged | Task 5 |
| Fixtures + tests + reject yaml | Task 1, 3, 7 |
| Docs + example + migrate note | Task 6 |
| Non-goals (MCP, parallel, dual-read, audit files) | Global Constraints |

No TBD placeholders; signatures named consistently (`plan_extract_workflows_json`, `quiet_plan`, `workflow_run(..., verbose)`).
