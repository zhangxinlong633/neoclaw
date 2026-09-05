# Capability Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a first-class Capability Matrix so agent tool loops, DAG `type:tool` steps, and (P1) MCP stdio tools share one named contract table—peer to DAG in product terms.

**Architecture:** New `capability_matrix` module owns rows (name/source/schema/effect/binding). Config loaders populate builtins + `tools.commands` (+ MCP servers in P1). `neo_build_tools_json` / `neo_dispatch_tool` / planner allowlists consume the matrix. Policy gates (`http_fetch_enabled`, later `shell_enabled`, per-MCP enable) flip `enabled` before exposure.

**Tech Stack:** C99, yyjson (JSON5 config + JSON-RPC framing), libcurl (unchanged for LLM), fork/exec for command tools and MCP stdio, existing `Makefile` test binaries.

## Global Constraints

- Name in docs/code comments: **Capability Matrix / 能力矩阵** (module: `capability_matrix`, not “registry”).
- MCP is a **loader** into the matrix, not a second tool API.
- v1 MCP: **stdio only**; config may reserve `url` without implementing HTTP/SSE.
- Path builtins stay under `tools.root`; no Cursor-class IDE agent / auto loop→DAG compile.
- Platform: Apple/Linux only (same as current tools).
- TDD: failing test before production code for each task’s behavioral deliverable.
- Keep commits small and green (`make test`).

---

## File map

| File | Responsibility |
|------|----------------|
| `src/capability_matrix.h` / `.c` | Row types, build/free, find, OpenAI tools JSON emit, prompt listing text |
| `src/config.h` / `config.c` | Parse `parameters` on commands; `mcp_servers[]`; `shell_enabled`; grep caps; reserved names include `grep` |
| `src/agent_tools.c` | Build matrix once per run; dispatch via matrix; `grep` builtin; truncation warning; optional verbose capability lines |
| `src/mcp_stdio.h` / `.c` | (P1) Spawn server, JSON-RPC initialize/list/call, insert rows |
| `src/plan.c` | Planner allowlist from matrix listing (not only `tools.commands`) |
| `src/main.c` / `src/daemon.c` | Append matrix listing into system prompt tools section |
| `docs/tool.md` / `docs/workflow.md` / `README.md` | Matrix as peer to DAG; MCP config; migrate notes |
| `tests/test_capability_matrix.c` | Matrix build, schema passthrough, policy disable, name collision |
| `tests/fixtures/…` | JSON5 with command `parameters`; (P1) mock MCP script |
| `Makefile` | Link new objs into `neo` and tests |

---

## Phase P0 — Matrix + call quality + `grep`

### Task 1: Capability Matrix types + unit test skeleton

**Files:**
- Create: `src/capability_matrix.h`, `src/capability_matrix.c`
- Create: `tests/test_capability_matrix.c`
- Modify: `Makefile` (add `TEST_CAP_MATRIX`, link into `test` target; add `src/capability_matrix.o` to `neo` when wired—may stub until Task 2)

**Interfaces:**
- Produces:
```c
typedef enum {
  CAP_SRC_BUILTIN = 0,
  CAP_SRC_COMMAND = 1,
  CAP_SRC_MCP = 2
} cap_source_t;

typedef enum {
  CAP_EFFECT_READ = 0,
  CAP_EFFECT_WRITE = 1,
  CAP_EFFECT_EXEC = 2,
  CAP_EFFECT_NETWORK = 3
} cap_effect_t;

typedef struct {
  char *name;
  cap_source_t source;
  char *source_detail; /* e.g. mcp server name; may be NULL */
  char *description;
  char *parameters_json; /* full JSON Schema object text; never NULL when enabled */
  cap_effect_t effect;
  int timeout_sec;
  int max_output_bytes;
  int enabled;
  /* binding */
  int builtin_id; /* 0=none; else internal enum */
  const tool_command_t *command; /* non-owning; NULL if not command */
  char *mcp_server; /* owned string or NULL */
  char *mcp_tool;   /* remote tool name; NULL if not mcp */
} cap_row_t;

typedef struct {
  cap_row_t *rows;
  int count;
  int cap;
} capability_matrix_t;

void capability_matrix_init(capability_matrix_t *m);
void capability_matrix_free(capability_matrix_t *m);
const cap_row_t *capability_matrix_find(const capability_matrix_t *m, const char *name);
int capability_matrix_append_tools_json(const capability_matrix_t *m, NeoBuf *b); /* or write into char** */
char *capability_matrix_prompt_listing(const capability_matrix_t *m); /* caller frees */
```
- Note: If `NeoBuf` is private to `agent_tools.c`, either move a small buffer helper to a shared header or have `capability_matrix_write_tools_array(char **out_json)` allocate a JSON array string instead. Prefer **allocate JSON string** to avoid pulling NeoBuf into the matrix module:
```c
/* Returns heap JSON array of OpenAI tool objects for enabled rows; caller frees. */
char *capability_matrix_tools_json(const capability_matrix_t *m);
```

- [ ] **Step 1: Write failing test** — `tests/test_capability_matrix.c` that `#include "capability_matrix.h"`, calls `capability_matrix_init` / `free`, expects compile/link (will fail until files exist).

```c
#include "capability_matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  capability_matrix_t m;
  capability_matrix_init(&m);
  if (m.count != 0) {
    fprintf(stderr, "expected empty\n");
    return 1;
  }
  capability_matrix_free(&m);
  printf("ok\n");
  return 0;
}
```

- [ ] **Step 2: Run** — add Makefile rule; `make tests/test_capability_matrix` → FAIL (missing sources).

- [ ] **Step 3: Minimal impl** — empty init/free/find returning NULL; `capability_matrix_tools_json` returns `strdup("[]")`; `capability_matrix_prompt_listing` returns `strdup("(none)\n")`.

- [ ] **Step 4: Run test** — expect `ok`.

- [ ] **Step 5: Commit** — `feat: add capability_matrix stub and test harness`

---

### Task 2: Build matrix from config (builtins + commands)

**Files:**
- Modify: `src/capability_matrix.c` — `int capability_matrix_build_from_config(capability_matrix_t *m, const agent_config_t *conf);`
- Modify: `src/config.h` — add to `tool_command_t`: `char *parameters_json;` (nullable; owned)
- Modify: `src/config.c` — parse optional `parameters` object on each command via yyjson; `yyjson_val_write` into `parameters_json`; free in `free_tool_commands`; reserve name `grep`
- Modify: `tests/test_capability_matrix.c` — load a tiny JSON5 fixture or construct `agent_config_t` in memory
- Create: `tests/fixtures/tools_commands_schema.json5` (minimal model + tools.commands with parameters)

**Interfaces:**
- Consumes: `agent_config_t.tools`
- Produces: rows for `read_file`, `write_file`, `list_dir`, optional `http_get` (only if `http_fetch_enabled` && hosts), each command; `parameters_json` default `{"type":"object"}` when command omits schema
- Effect mapping: read_file/list_dir/grep→READ; write_file→WRITE; http_get→NETWORK; commands→EXEC
- Name collision: if command name equals builtin, `config_load` already rejects reserved; matrix build should skip disabled / duplicate with stderr once

- [ ] **Step 1: Failing test** — after `capability_matrix_build_from_config`, find `echo_args` (or fixture name) whose `parameters_json` contains `"properties"`; find `read_file`; `http_get` absent when fetch disabled.

- [ ] **Step 2: Run** — expect FAIL (build_from_config missing or incomplete).

- [ ] **Step 3: Implement** `capability_matrix_build_from_config` + config `parameters` parse. Builtin parameter schemas can be static string literals (copy existing NEO_TOOL_* parameter blobs).

- [ ] **Step 4: `make tests/test_capability_matrix && ./tests/test_capability_matrix`** — PASS; also extend `tests/test_parse_commands` if needed for parameters round-trip.

- [ ] **Step 5: Commit** — `feat: build Capability Matrix from builtins and commands`

---

### Task 3: Wire matrix into tool JSON + dispatch

**Files:**
- Modify: `src/agent_tools.c` — replace `neo_build_tools_json` body to call `capability_matrix_build_from_config` + `capability_matrix_tools_json`; free matrix at end of `agent_run_with_tools` or keep on stack per round (build once per `agent_run_with_tools` call)
- Modify: `neo_dispatch_tool` — `capability_matrix_find`; if missing → error string; if command → existing `command_tool_run`; if builtin → existing handlers; if mcp → not yet (P1)
- Modify: `Makefile` — ensure `neo` links `capability_matrix.o`

**Interfaces:**
- Consumes: `capability_matrix_build_from_config`, `capability_matrix_tools_json`, `capability_matrix_find`
- Produces: unchanged public `agent_run_with_tools` / `neo_dispatch_tool` signatures

- [ ] **Step 1: Failing test** — extend `tests/test_command_exec` or matrix test: `capability_matrix_tools_json` for a command with custom schema must embed that schema substring (not only `{"type":"object"}`).

- [ ] **Step 2: Implement emit** — for each enabled row, emit  
  `{"type":"function","function":{"name":"...","description":"...","parameters":<parameters_json>}}`

- [ ] **Step 3: Switch agent_tools** to matrix; delete duplicated NEO_TOOL_* append loop for commands (keep static schemas as data for builtin rows inside matrix build).

- [ ] **Step 4: `make test`** — all green.

- [ ] **Step 5: Commit** — `feat: serve OpenAI tools from Capability Matrix`

---

### Task 4: Observability — truncation warning + verbose capability lines

**Files:**
- Modify: `src/agent_tools.c` — `parse_tool_calls_yy`: if `yyjson_arr_size(tc) > NEO_MAX_TOOLS_PER_TURN`, `fprintf(stderr, "neo: tool_calls truncated to %d (got %zu)\n", ...)`
- Modify: `agent_run_with_tools` — add optional verbose: either new param `int verbose` **or** read `getenv("NEO_TOOL_VERBOSE")` for P0 to avoid churning all call sites; **prefer** adding `int verbose` to `agent_run_with_tools` and threading from `main`/`daemon`/`workflow` (workflow already has verbose)
- When verbose and a tool runs: `fprintf(stderr, "neo: capability=%s source=%s status=ok|err\n", ...)`

**Interfaces:**
- Change: `int agent_run_with_tools(..., int verbose);` — update `agent_tools.h`, `main.c`, `daemon.c`, `workflow.c`

- [ ] **Step 1: Unit-test truncation message** — hard without LLM; instead test a small exported helper `int neo_tool_calls_over_limit(size_t n, size_t max)` used before parse, or simulate by making `parse_tool_calls_yy` set a static counter—**simpler:** add `tests/test_capability_matrix` check documenting expected stderr is manual; for automation, extract:

```c
/* returns 1 if warned */
int capability_warn_tool_truncation(size_t n_calls, int max_per_turn, FILE *err);
```

Failing test calls with n=20, max=16, captures via `open_memstream` if available, or temp file.

- [ ] **Step 2: Implement warning + verbose dispatch logs**

- [ ] **Step 3: `make test`**

- [ ] **Step 4: Commit** — `feat: warn on tool_calls truncation; verbose capability logs`

---

### Task 5: System prompt + planner listing from matrix

**Files:**
- Modify: `src/main.c` / `src/daemon.c` — after tools blurb, if tools enabled: build matrix, `capability_matrix_prompt_listing`, append under `## Capabilities` / tools section
- Modify: `src/plan.c` — `plan_build_system_prompt`: append same listing (matrix names), keep command argv details as secondary or fold description-only to avoid duplication
- Modify: `tests/test_plan_extract.c` — with a base config that has a command, prompt must contain that command name via matrix path

- [ ] **Step 1: Failing test** in `test_plan_extract` — `strstr(prompt, "echo_args")` or fixture command name after building prompt with commands populated on `base`.

- [ ] **Step 2: Implement listing helper usage** — format like:
```
- read_file (builtin, read): ...
- my_cmd (command, exec): ...
```

- [ ] **Step 3: `make test`**

- [ ] **Step 4: Commit** — `feat: expose Capability Matrix names in system and plan prompts`

---

### Task 6: Builtin `grep`

**Files:**
- Modify: `src/config.h` — `int grep_max_matches;` (default 50), `int grep_max_file_bytes;` (default 262144)
- Modify: `src/config.c` — parse optional fields; reserve `grep`
- Modify: `src/capability_matrix.c` — always add `grep` row when tools enabled (effect READ)
- Modify: `src/agent_tools.c` — `tool_grep(args)`: `pattern` required; optional `path` (default `.`), optional `glob` (simple `*` suffix/prefix only or fnmatch); walk under root; line-oriented search; cap matches; return text `file:line:content\n`
- Create: `tests/test_grep_tool.c` or fold into matrix/dispatch test with temp dir

- [ ] **Step 1: Failing test** — write temp file `hello.txt` containing `needle`, call `neo_dispatch_tool(..., "grep", "{\"pattern\":\"needle\",\"path\":\".\"}", ...)`, expect `needle` in output.

- [ ] **Step 2: Implement grep + matrix row**

- [ ] **Step 3: `make test`**

- [ ] **Step 4: Commit** — `feat: add grep builtin capability`

---

### Task 7: P0 docs

**Files:**
- Modify: `docs/tool.md` — Capability Matrix section; `parameters` on commands; `grep`; fix YAML leftovers → JSON5
- Modify: `docs/workflow.md` — DAG `type:tool` names come from matrix
- Modify: `README.md` — one bullet: 能力矩阵 peer to DAG
- Modify: `config/config.json5.example` — commented `parameters` example + grep knobs

- [ ] **Step 1: Update docs** (no code)
- [ ] **Step 2: Commit** — `docs: document Capability Matrix (P0)`

---

## Phase P1 — MCP stdio loader

### Task 8: Config `tools.mcp_servers[]`

**Files:**
- Modify: `src/config.h`:
```c
typedef struct {
  char *name;
  char *command;
  char **args;
  int args_count;
  char **env_keys;
  char **env_vals;
  int env_count;
  char *url; /* reserved; if non-NULL nonempty → stderr warn and ignore server in v1 */
  int enabled; /* default 1 */
} mcp_server_config_t;
/* in tools_config_t: */
mcp_server_config_t *mcp_servers;
int mcp_server_count;
```
- Modify: `src/config.c` — parse array; free on `config_free`
- Create: `tests/fixtures/tools_mcp_stdio.json5` — one server pointing at `tests/fixtures/mock_mcp_server.py` (or `.sh`)
- Modify: `tests/test_parse_commands` or new `tests/test_parse_mcp.c`

- [ ] **Step 1: Failing parse test** for `mcp_servers`
- [ ] **Step 2: Implement parse/free**
- [ ] **Step 3: Commit** — `feat: parse tools.mcp_servers config`

---

### Task 9: MCP stdio client + matrix rows

**Files:**
- Create: `src/mcp_stdio.h`, `src/mcp_stdio.c`
- Modify: `capability_matrix_build_from_config` — after builtins/commands, call `mcp_stdio_load_all(conf, m)` which for each enabled server without `url`: spawn, initialize, tools/list, append rows `mcp_<server>_<tool>`
- Modify: `Makefile`

**Interfaces:**
```c
/* Append MCP rows into m. On server failure: stderr + skip that server (0). */
int mcp_stdio_load_into_matrix(const agent_config_t *conf, capability_matrix_t *m);

/* Call remote tool; caller frees *out. */
int mcp_stdio_call(const agent_config_t *conf, const cap_row_t *row,
                   const char *args_json, char **out, size_t *out_len);

void mcp_stdio_shutdown_all(void); /* kill kept children if process-long cache used */
```

**Protocol (v1 minimal):**
- Framing: write one JSON-RPC object per line (or Content-Length headers if needed—**prefer newline-delimited JSON** if mock uses that; if real MCP uses Content-Length, implement Content-Length reader/writer as used by official SDK)
- Methods: `initialize`, `notifications/initialized`, `tools/list`, `tools/call`
- Keep child process alive for the duration of `agent_run_with_tools` / workflow run (cache by server name in a static table freed on shutdown)

- [ ] **Step 1: Create `tests/fixtures/mock_mcp_echo.py`** — speaks enough MCP to list tool `echo` and return arguments
- [ ] **Step 2: Failing test** — build matrix from fixture config; `capability_matrix_find(m, "mcp_mock_echo")` non-NULL (name rule: `mcp_<server>_<tool>` with sanitizing non `[A-Za-z0-9_]` → `_`)
- [ ] **Step 3: Implement client + load**
- [ ] **Step 4: Commit** — `feat: load MCP stdio tools into Capability Matrix`

---

### Task 10: Dispatch MCP + planner/DAG use names

**Files:**
- Modify: `neo_dispatch_tool` — if row `source==CAP_SRC_MCP` → `mcp_stdio_call`
- Modify: `plan_build_system_prompt` — matrix listing already includes MCP after load; ensure `plan_run` builds matrix (with MCP) before prompting **or** load MCP when building plan prompt
- Lifecycle: `plan_run` / `agent_run_with_tools` / `workflow_run` should call matrix build once (MCP spawn cost); `mcp_stdio_shutdown_all` on process exit paths

- [ ] **Step 1: Integration test** — dispatch `mcp_mock_echo` with `{"text":"hi"}`; expect `hi` in output
- [ ] **Step 2: Implement dispatch path**
- [ ] **Step 3: Manual smoke** — `./neo plan "call mcp mock if present"` only if fixture config; document in tool.md
- [ ] **Step 4: Commit** — `feat: dispatch MCP tools via Capability Matrix`

---

### Task 11: P1 docs + example

**Files:**
- `docs/tool.md` — MCP section; naming `mcp_<server>_<tool>`; stdio only
- `config/config.json5.example` — commented `mcp_servers`
- `README.md` — mention MCP blades in matrix

- [ ] **Step 1: Docs**
- [ ] **Step 2: Commit** — `docs: MCP stdio as Capability Matrix loader`
- [ ] **Step 3: `make test` full green**

---

## Phase P2 — Policy + `run_command` + narrative polish

### Task 12: `shell_enabled` + `run_command` builtin

**Files:** config flag default 0; matrix row only if enabled; effect EXEC; args `{"argv":["echo","hi"]}` or `{"command":"..."}` — **prefer argv array only** (no shell); resolve argv[0] under root or allowlist; reuse patterns from `command_tool_run`.

- [ ] Tests for disabled (not in tools JSON) and enabled echo
- [ ] Commit — `feat: optional run_command behind shell_enabled`

### Task 13: Docs elevate Matrix beside DAG

- [ ] `docs/claw.md` / README architecture blurb: DAG ∥ Capability Matrix ∥ Policy
- [ ] Commit — `docs: position Capability Matrix as peer to DAG`

---

## Spec coverage checklist

| Spec item | Tasks |
|-----------|-------|
| Matrix rows/columns | 1–2 |
| Shared consumers (loop/DAG/plan) | 3, 5, 10 |
| Policy gates / budgets / observability | 4, 12 |
| Command `parameters` schema | 2–3 |
| System listing | 5 |
| `grep` | 6 |
| MCP stdio loader + naming | 8–10 |
| Reserve URL | 8 |
| Docs peer narrative | 7, 11, 13 |
| Non-goals respected | no HTTP MCP, no loop→DAG compiler tasks |

## Placeholder / consistency self-check

- Public entrypoints named consistently: `capability_matrix_*`, `mcp_stdio_*`.
- OpenAI tools JSON always uses row `parameters_json`.
- MCP external name format fixed: `mcp_<server>_<tool>`.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-09-05-capability-matrix.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — execute in this session with executing-plans checkpoints  

Which approach?
