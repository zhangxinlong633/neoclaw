# Swiss Army Knife Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Neo 通过 YAML 可配置命令工具、声明式 workflow（含简单 loop）、profiles 与 `neo-ask`，成为轻量瑞士军刀 agent。

**Architecture:** 保持单二进制 + libcurl。在现有 line-based YAML 解析上扩展 `tools.commands` 与 `workflows`；命令工具经 `execvp` 跑声明过的 argv（stdin JSON / env 传参）；新建 `workflow.c` 引擎；CLI 增加 `-p` 与 `workflow run`。

**Tech Stack:** C99、libcurl、yyjson、POSIX `fork/exec`（Linux/macOS）、bash 样例脚本、Makefile `test` 目标。

**Spec:** `docs/superpowers/specs/2026-09-04-swiss-army-knife-design.md`

## Global Constraints

- 不引入新运行时依赖（无 Python 插件宿主、无 MCP 运行时、无完整 YAML 库）。
- 禁止自由 shell 字符串执行；只 `exec` 配置里声明的 `argv[]`。
- `argv[0]` 必须落在 `tools.root` realpath 下；路径段禁止 `..`。
- `NEO_DISABLE_TOOLS=1` 与 `tools.enabled: false` 关闭全部 tools（含 commands）。
- 命令 tool 名禁止：`read_file`、`write_file`、`list_dir`、`http_get`。
- 自然语言 `./neo "..."` 行为不变；workflow 仅 `neo workflow run <name>`。
- 第一期 loop 仅 `max` + `until: always`；不做 if/else、并行、复杂 until。
- 相对路径在 `-p` 模式下相对 profile 目录。
- 用户可见文档与注释用中文或与现有 README 风格一致的中英混合；commit message 用英文 concise style 与仓库历史一致亦可。

## File map

| 文件 | 职责 |
|------|------|
| `src/config.h` / `src/config.c` | `tool_command_t`、`workflow_*` 结构体与解析；profile 根路径辅助 |
| `src/command_tools.c` / `.h` | 校验路径、exec、捕获输出；供 agent 与 workflow 共用 |
| `src/agent_tools.c` | 注册 commands 到 tools JSON；`run_one_tool` 分发到 command_tools |
| `src/workflow.c` / `.h` | 解析后的 workflow 执行（tool / llm / loop） |
| `src/main.c` | `-p`、`workflow run`、usage |
| `Makefile` | 新源文件、`make test` |
| `scripts/tools/echo-args.sh` | 5 分钟验收样例刀刃 |
| `scripts/neo-ask` | cron/管道薄壳 |
| `profiles/demo/neo.yaml` + 脚本 | profile 演示 |
| `tests/*.sh` / `tests/fixtures/*.yaml` | 无 LLM 的自动化验收 |
| `docs/tool.md` / `docs/workflow.md` / `README.md` | 用户文档 |
| `config.yaml.example` | 注释示例 |

---

### Task 1: Config — `tools.commands` 数据结构与解析

**Files:**
- Modify: `src/config.h`
- Modify: `src/config.c`
- Create: `tests/fixtures/commands_min.yaml`
- Create: `tests/test_parse_commands.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: 现有 `config_init` / `config_free` / `config_load_file` 模式
- Produces:
  - `typedef struct { char *name; char *description; char **argv; int argv_count; int timeout_sec; int max_output_bytes; int pass_args; /* 0=stdin_json, 1=env */ } tool_command_t;`
  - 在 `tools_config_t` 增加：`tool_command_t *commands; int command_count;`
  - `config_free` 释放全部 commands 字段
  - 解析 `tools.commands:` 下列表项（见下方 YAML 约定）

**YAML 约定（必须按此解析，避免歧义）：**

```yaml
tools:
  enabled: true
  root: "."
  commands:
    - name: echo_args
      description: "Echo arguments JSON"
      argv: ["./scripts/tools/echo-args.sh"]
      timeout_sec: 5
      max_output_bytes: 4096
      pass_args: stdin_json
```

- `argv` 第一期支持**单行括号列表** `argv: ["a","b"]`（实现一个小函数拆引号与逗号）。
- 进入 `- name:` 开始新 command；`name`/`description`/… 挂到当前项。
- 缺省：`timeout_sec=30`，`max_output_bytes=65536`，`pass_args=stdin_json`。
- 加载结束校验：空 name、空 argv、与内置名冲突、重复 name → `config_load_file` 返回非 0 并 `fprintf(stderr, ...)`。

- [ ] **Step 1: 写失败测试 `tests/test_parse_commands.c`**

```c
#include "config.h"
#include <stdio.h>
#include <string.h>
int main(void) {
  agent_config_t c;
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/commands_min.yaml") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (c.tools.command_count != 1) {
    fprintf(stderr, "want 1 command got %d\n", c.tools.command_count);
    return 1;
  }
  if (!c.tools.commands[0].name || strcmp(c.tools.commands[0].name, "echo_args") != 0) return 1;
  if (c.tools.commands[0].argv_count < 1) return 1;
  config_free(&c);
  printf("ok\n");
  return 0;
}
```

- [ ] **Step 2: 写 fixture `tests/fixtures/commands_min.yaml`**

```yaml
model:
  base_url: "http://127.0.0.1:9"
  name: "test"
  api_key: "x"
tools:
  enabled: true
  root: "."
  commands:
    - name: echo_args
      description: "Echo arguments JSON"
      argv: ["./scripts/tools/echo-args.sh"]
      timeout_sec: 5
      max_output_bytes: 4096
      pass_args: stdin_json
skills:
  directory: "skills"
memory:
  path: "MEMORY.md"
  max_chars: 100
session:
  max_turns: 2
```

- [ ] **Step 3: Makefile 增加测试目标（先会失败）**

```makefile
TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/config.c
	$(CC) $(CFLAGS) -o $@ tests/test_parse_commands.c src/config.c

test: $(TEST_PARSE_CMD)
	./$(TEST_PARSE_CMD)

.PHONY: clean test
```

- [ ] **Step 4: 运行测试确认失败**

Run: `make test`  
Expected: 编译可能成功但 `load failed` 或 `want 1 command got 0`

- [ ] **Step 5: 实现 `config.h` / `config.c` 解析与释放**

在 `tools_config_t` 增加 commands 字段；在 `in_tools` 分支识别 `commands:`、`- name:`、缩进字段、`argv: [...]` 行内列表；`config_free` 释放。

- [ ] **Step 6: 再跑 `make test`**

Expected: 打印 `ok`，exit 0

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/config.c tests/fixtures/commands_min.yaml tests/test_parse_commands.c Makefile
git commit -m "$(cat <<'EOF'
feat: parse tools.commands from config YAML

EOF
)"
```

---

### Task 2: `command_tools` — 安全 exec 与输出捕获

**Files:**
- Create: `src/command_tools.h`
- Create: `src/command_tools.c`
- Create: `scripts/tools/echo-args.sh`
- Create: `tests/test_command_exec.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `agent_config_t`、`tool_command_t`
- Produces:
  - `int command_tool_find(const agent_config_t *conf, const char *name);` → index 或 -1
  - `int command_tool_run(const agent_config_t *conf, const char *root_real, const tool_command_t *cmd, const char *args_json, char **out_text, size_t *out_len);`
    - 成功返回 0（进程退出码非 0 仍返回 0，但 `*out_text` 以 `EXIT:<code>\n` 开头）
    - 配置/路径/fork 失败返回 -1，`*out_text` 为 `ERROR: ...`
  - 路径校验：`argv[0]` 相对 `root_real`，拒绝绝对路径与 `..`
  - `pass_args==0`：stdin 写 `args_json`（若 NULL 则 `{}`）；`pass_args==1`：`setenv("NEO_TOOL_ARGS", ...)`
  - 始终 `setenv("NEO_TOOL_NAME", cmd->name, 1)`
  - timeout：`alarm` 或 `select`+`kill`（macOS/Linux 可用 `fork` + `waitpid` 轮询）

- [ ] **Step 1: 写 `scripts/tools/echo-args.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
# Read stdin JSON (or empty) and echo it; also print NEO_TOOL_NAME.
echo "name=${NEO_TOOL_NAME:-}"
if [[ -n "${NEO_TOOL_ARGS:-}" ]]; then
  echo "env=$NEO_TOOL_ARGS"
else
  echo -n "stdin="
  cat
  echo
fi
```

`chmod +x scripts/tools/echo-args.sh`

- [ ] **Step 2: 写失败测试 `tests/test_command_exec.c`**

加载 `tests/fixtures/commands_min.yaml`，`realpath(".")` 为 root，调用 `command_tool_run(..., "{\"ping\":1}", ...)`，断言输出含 `stdin={"ping":1}` 与 `name=echo_args`。

- [ ] **Step 3: 运行确认失败（符号未定义 / 失败）**

- [ ] **Step 4: 实现 `command_tools.c`**

最小实现：`pipe` + `fork` + `dup2` + `execvp` + 读到 `max_output_bytes`；超时后 `SIGKILL`。

- [ ] **Step 5: `make test` 包含本测试，期望 PASS**

- [ ] **Step 6: Commit**

```bash
git add src/command_tools.c src/command_tools.h scripts/tools/echo-args.sh tests/test_command_exec.c Makefile
git commit -m "$(cat <<'EOF'
feat: exec declared command tools under tools.root

EOF
)"
```

---

### Task 3: 注册 commands 到 LLM tool 循环

**Files:**
- Modify: `src/agent_tools.c`（`neo_build_tools_json`、`run_one_tool`）
- Modify: `Makefile`（`neo` 链接 `command_tools.c`）
- Modify: `config.yaml.example`
- Modify: `docs/tool.md`

**Interfaces:**
- Consumes: `command_tool_run`、`conf->tools.commands`
- Produces: 每个 command 追加一条 OpenAI function schema：
  - `parameters`: `{"type":"object","additionalProperties":true}`（或空 properties，允许任意 JSON 对象）
  - `run_one_tool`：非内置名则查 commands 并调用 `command_tool_run`，结果写入 `NeoBuf`

- [ ] **Step 1: 扩展 `neo_build_tools_json` 追加 commands**

对每个 command 输出：
`{"type":"function","function":{"name":"<name>","description":"<desc>","parameters":{"type":"object"}}}`  
注意 JSON 转义 `description` 中的引号与反斜杠（复用现有 escape 辅助）。

- [ ] **Step 2: `run_one_tool` 分发到 `command_tool_run`**

未知名仍返回 `ERROR: unknown tool`。

- [ ] **Step 3: 更新 `config.yaml.example` 注释块与 `docs/tool.md` 增加 commands 小节**

- [ ] **Step 4: 手动 smoke（有 API key 时）**

```bash
# 在启用 tools.commands 的 config 下
./neo "只调用一次 echo_args，arguments 为 {\"ping\":1}，然后原样引用工具输出"
```

Expected stderr: `neo tool: echo_args`

无 key 时可跳过，依赖 Task 2 单测。

- [ ] **Step 5: Commit**

```bash
git add src/agent_tools.c Makefile config.yaml.example docs/tool.md
git commit -m "$(cat <<'EOF'
feat: expose tools.commands to the LLM tool loop

EOF
)"
```

---

### Task 4: Config — `workflows` 解析

**Files:**
- Modify: `src/config.h`, `src/config.c`
- Create: `tests/fixtures/workflow_min.yaml`
- Create: `tests/test_parse_workflows.c`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `typedef enum { WF_STEP_TOOL=0, WF_STEP_LLM=1, WF_STEP_LOOP=2 } wf_step_type_t;`
  - `typedef struct { char *id; wf_step_type_t type; char *tool; char *args_json; char *prompt; int tools_on; char **over_ids; int over_count; int max_iters; } workflow_step_t;`
  - `typedef struct { char *name; char *description; workflow_step_t *steps; int step_count; } workflow_t;`
  - `agent_config_t` 增加：`workflow_t *workflows; int workflow_count;`
  - `const workflow_t *config_find_workflow(const agent_config_t *c, const char *name);`

**YAML 约定：**

```yaml
workflows:
  - name: demo_loop
    description: "echo then stop"
    steps:
      - id: fetch
        type: tool
        tool: echo_args
        args: {"ping": 1}
      - id: again
        type: loop
        over: ["fetch"]
        max: 2
        until: always
```

- `args:` 行：取第一个 `:` 后整段 trim，作为 `args_json` 字符串原样保存（不做 YAML 对象解析）。
- `over:` 单行 `["a","b"]` 列表。
- 校验：`loop` 必须有 `max >= 1`；`over` 中每个 id 必须在同 workflow 的 steps 中存在且不是自己（允许引用前面的 id）；`tool` 步的 tool 名在加载时可只做非空校验，运行时再查注册表。

- [ ] **Step 1–6:** 同 Task 1 模式：写测试 → 失败 → 实现 → PASS → commit

```bash
git commit -m "$(cat <<'EOF'
feat: parse workflows from config YAML

EOF
)"
```

---

### Task 5: Workflow 引擎 — `tool` + `loop`（无 LLM）

**Files:**
- Create: `src/workflow.h`
- Create: `src/workflow.c`
- Create: `tests/test_workflow_loop.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `command_tool_run`、内置 tool 可通过抽一层 `neo_invoke_tool_by_name(conf, root_real, name, args_json, out)`（若内置仍只在 agent_tools 内，则第一期 workflow 的 `type: tool` **仅支持 commands**；内置名可在本 task 末尾桥接，或 Task 5b 补上——**本计划要求：commands + 内置四名均可**，故在 `command_tools` 旁增加 `tool_invoke.c` 或把内置调用从 `agent_tools.c` 导出为 `neo_dispatch_tool(...)`）。
- Produces:
  - `int workflow_run(const agent_config_t *conf, const char *workflow_name, char **out_text);`
  - 维护 `id → last_output` 映射；`{{prev}}` / `{{steps.id}}` 仅在 Task 6 的 llm 使用，本 task 可先实现 map 供 loop 使用。
  - `type: loop`：将 `over` 列表重复执行 `max` 次（每次完整跑一遍 over 中各 id 对应 step 的定义；**不要**递归执行另一个 loop step——若 over 含 loop，加载期报错）。
  - stderr：`neo: workflow:<name> step:<id>: ...` 与 `neo tool: <name>`（tool 步）

**推荐抽公共分发（避免重复）：**

在 `src/agent_tools.c` 增加非 static：

```c
int neo_dispatch_tool(const agent_config_t *conf, const char *root_real,
                      const char *name, const char *args_json, NeoBuf *result);
```

`run_one_tool` 与 workflow 共用。

- [ ] **Step 1: 写 `tests/test_workflow_loop.c`**

用 `workflow_min.yaml`（仅 tool+loop，无 llm），断言 `echo_args` 被调用次数为 2（可在 echo 脚本旁用计数文件，或检查合并输出中 `stdin=` 出现 2 次）。更稳妥：让测试用的脚本 `scripts/tools/count-run.sh` 每次 append 一行到 `/tmp/neo-wf-count-$$`，测试查行数——**改用 fixture 旁脚本写 `tests/fixtures/count.txt` 相对 root**：

`tests/fixtures/bin/count.sh`：`echo x >> "$(dirname "$0")/../count.out"`  
workflow 跑完后 `count.out` 有 2 行。

- [ ] **Step 2–5:** 失败 → 实现 `workflow_run`（遇 `type: llm` 返回明确错误「not implemented」可接受，但 fixture 不含 llm）→ PASS → commit

```bash
git commit -m "$(cat <<'EOF'
feat: run declarative workflows with tool and loop steps

EOF
)"
```

---

### Task 6: Workflow — `llm` 步与模板

**Files:**
- Modify: `src/workflow.c`
- Modify: `tests/fixtures/workflow_llm.yaml`（可选，若无 API 则单测只测模板函数）
- Create: `tests/test_workflow_template.c`

**Interfaces:**
- Produces:
  - `char *workflow_expand_template(const char *tmpl, const char *prev, /* id→text map */ ...);` 调用方 free
  - `type: llm`：拼 system（复用 main 里 build system 逻辑——**必须抽出** `src/prompt.c` 的 `int neo_build_system_prompt(const agent_config_t *conf, char *buf, size_t cap);`，供 main/daemon/workflow 共用，避免三份拷贝）。若抽取范围过大，workflow llm 步可临时调用精简 system：「You are Neo workflow step.」+ memory 可选；**本计划要求抽取 `neo_build_system_prompt`**，main/daemon 改为调用它。
  - `tools: on` → `agent_run_with_tools`；`off` → 现有无 tools 的 `llm_chat`（查 `llm.h` 现有 API，沿用 main 单次路径）。

- [ ] **Step 1: 抽出 `neo_build_system_prompt` 到 `src/prompt.c` / `prompt.h`，main + daemon 改用它，行为与现网一致（可用 `-d` 对比）**

- [ ] **Step 2: 单测模板：`{{prev}}` 与 `{{steps.fetch}}` 替换；未知变量 → 返回 NULL 且 workflow 失败**

- [ ] **Step 3: 实现 llm 步**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add workflow llm steps and shared system prompt builder

EOF
)"
```

---

### Task 7: CLI — `workflow run`

**Files:**
- Modify: `src/main.c`
- Modify: `README.md`
- Create: `docs/workflow.md`

**Interfaces:**
- 解析：`neo [OPTIONS] workflow run <name>`
  - OPTIONS 仍支持 `-c`/`-p`/`-m`/`-d`
  - 成功：stdout 为 workflow 最终文本；失败 exit 1

- [ ] **Step 1: 更新 `print_usage`**

```text
./neo workflow run NAME
```

- [ ] **Step 2: 在 `main` 识别 `workflow` `run` `<name>`，加载 config 后 `workflow_run`**

- [ ] **Step 3: 文档 `docs/workflow.md` + README 链到该文档**

- [ ] **Step 4: 本地跑**

```bash
./neo -c tests/fixtures/workflow_min.yaml workflow run demo_loop
```

Expected: exit 0，计数文件 2 行（或 echo 输出两次痕迹）

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add neo workflow run CLI

EOF
)"
```

---

### Task 8: Profiles（`-p` / `NEO_PROFILE`）

**Files:**
- Modify: `src/main.c`
- Modify: `src/config.c`（可选：`config_resolve_profile_dir`）
- Create: `profiles/demo/neo.yaml`
- Create: `profiles/demo/scripts/tools/echo-args.sh`（或复用仓库 scripts，yaml 指向 `../../scripts/tools/echo-args.sh`——**禁止 `..`**，故应 **复制或 symlink 到 profile 内** `profiles/demo/scripts/tools/echo-args.sh`）
- Create: `tests/test_profile_paths.sh`

**Interfaces:**
- `-p name` → `profile_dir = "profiles/<name>"`（相对 cwd）
- 默认 config 文件：`profiles/<name>/neo.yaml`
- 加载前 `chdir(profile_dir)` **或** 在加载后把所有相对路径前缀 profile_dir——**推荐：加载配置前 `chdir` 到 profile 目录并记住原 cwd，退出前可还原**；daemon 同样适用。
- `NEO_PROFILE`：无 `-p` 时生效；`-p` 优先。
- `-c abs_path`：不强制相对 profile；`-c rel`：相对 profile 目录。

- [ ] **Step 1: 实现 CLI 与 chdir 逻辑**

- [ ] **Step 2: `profiles/demo/` 最小可跑配置（tools.commands + 一个 workflow）**

- [ ] **Step 3: shell 测试**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
make
./neo -p demo workflow run demo_loop
```

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add -p profile switching for config roots

EOF
)"
```

---

### Task 9: `neo-ask` + 文档收尾

**Files:**
- Create: `scripts/neo-ask`
- Modify: `README.md`
- Modify: `docs/claw.md`（入口表增加 neo-ask / workflow / -p）
- Modify: `docs/workflow.md`（cron 样例）

**Interfaces:**
- `neo-ask` bash：
  - 定位 `NEO_BIN` 或脚本旁 `../neo` / 仓库根 `neo`
  - 支持透传 `-p`/`-c`/`-m`/`-d`
  - `--stdin`：读 stdin 作为唯一 user message
  - `--workflow NAME`：转成 `neo ... workflow run NAME`
  - 其余参数拼成 user message

- [ ] **Step 1: 实现并可执行**

```bash
chmod +x scripts/neo-ask
./scripts/neo-ask -p demo --workflow demo_loop
```

- [ ] **Step 2: cron 样例写入 `docs/workflow.md`**

```text
# 每小时（用户自行安装）
0 * * * * cd /path/to/neoclaw && ./scripts/neo-ask -p demo --workflow demo_loop >>/tmp/neo-cron.log 2>&1
```

- [ ] **Step 3: `make test` 全绿**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add neo-ask wrapper and finish swiss-army docs

EOF
)"
```

---

## Spec coverage checklist

| Spec 要求 | Task |
|-----------|------|
| `tools.commands` 配置与校验 | 1 |
| exec 协议 stdin_json/env、timeout、截断、EXIT 前缀 | 2 |
| 注册进 LLM tool 循环 + `neo tool:` | 3 |
| workflows YAML | 4 |
| `type: tool` / `loop` + max | 5 |
| `type: llm` + `{{prev}}` / `{{steps.id}}` + tools on/off | 6 |
| `neo workflow run` | 7 |
| `-p` / `NEO_PROFILE` / 相对路径 | 8 |
| `neo-ask` + cron 文档 | 9 |
| 不做 hooks/MCP/自由 shell/复杂 until | 全局约束 |
| 5 分钟加刀刃（实现后仅 yaml+脚本） | 3 完成后即满足 |

## Placeholder / consistency self-review

- 无 TBD；`argv`/`over` 均规定为单行括号列表，匹配现有 line parser。
- `neo_dispatch_tool` 与 `neo_build_system_prompt` 在 Task 5/6 明确抽出，避免重复实现。
- Profile 禁止 `..`：demo 脚本放在 profile 目录内，与 spec 一致。
