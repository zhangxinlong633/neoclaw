# Neo 瑞士军刀 enrichment 设计

**日期：** 2026-09-04  
**状态：** 已评审通过（方案 1 + workflow D）  
**目标：** 在保持单二进制、仅依赖 libcurl、轻量好用的前提下，让 Neo 成为可配置扩展的瑞士军刀 agent——刀刃可加、刀柄可换、入口到处可用，并支持简单声明式 workflow loop。

---

## 1. 背景与范围

### 现状

- C CLI：`./neo` 单次、`neo daemon`（stdin / Unix socket）
- OpenClaw 式 prompt：soul / bootstrap / rules / skills / memory
- 内置 tools：`read_file` / `write_file` / `list_dir` / 可选 `http_get`
- 编排脚本：`scripts/neo-team`（多配置并行，非声明式 workflow）

### 要做

1. **YAML 可配置自定义命令工具**（`tools.commands`）
2. **声明式 workflow**（`tool` / `llm` / `loop`），步内可再开 agent tool 循环
3. **Profiles**（`-p` / `NEO_PROFILE`）整套刀柄切换
4. **薄入口** `scripts/neo-ask`（cron / 管道）

### 不做（YAGNI）

- 通用 hooks 总线、进程内动态库插件、完整 MCP 运行时
- 自由 `run_shell` 字符串执行
- workflow 条件分支 / 并行 / 复杂 `until` 谓词（第一期仅 `max`）
- 可视化编辑器

---

## 2. 架构

```text
外部入口                 Neo 核心                      刀刃
─────────                ────────                      ────
./neo [-p PROFILE]       config + prompt               内置 tools
./neo daemon             tool 注册表  ───────────────► tools.commands[] → exec argv
./neo workflow run NAME  workflow 引擎                 （沙箱 + timeout）
scripts/neo-ask          agent_run_with_tools
```

**原则**

1. 新能力优先进配置与脚本，不改核心也能挂刀刃。
2. 不拼 shell 字符串；只 `exec` 已声明的 `argv[]`。
3. 自然语言单次对话行为不变；workflow 走显式子命令。

---

## 3. Custom command tools

### 3.1 配置

挂在现有 `tools:` 下：

```yaml
tools:
  enabled: true
  root: "."
  commands:
    - name: echo_args
      description: "Echo tool arguments JSON (demo)"
      argv:
        - "./scripts/tools/echo-args.sh"
      timeout_sec: 30          # 默认 30
      max_output_bytes: 65536  # 默认 65536
      pass_args: stdin_json    # stdin_json（默认）| env
```

**规则**

| 规则 | 说明 |
|------|------|
| 总开关 | `tools.enabled: false` 或 `NEO_DISABLE_TOOLS=1` → 内置与 commands 均不注册 |
| 命名 | `name` 唯一；禁止与 `read_file` / `write_file` / `list_dir` / `http_get` 冲突 |
| 路径 | 第一期：`argv[0]` 必须解析到 `tools.root` realpath 之下的相对路径；禁止 `..` 段 |
| 平台 | 与现有 tools 一致：Linux / macOS |

### 3.2 Exec 协议

1. 模型（或 workflow `type: tool`）触发调用，`arguments` 为 JSON 对象（可 `{}`）。
2. `pass_args`：
   - `stdin_json`：arguments 原文写入子进程 stdin；环境变量 `NEO_TOOL_NAME=<name>`。
   - `env`：`NEO_TOOL_ARGS=<arguments JSON>` + `NEO_TOOL_NAME`；stdin 为空。
3. 工作目录：`tools.root` 的 realpath。
4. 合并捕获 stdout+stderr，截断到 `max_output_bytes`；超时杀进程。
5. 非 0 退出：结果前缀 `EXIT:<code>\n`，再跟输出。
6. stderr 打 `neo tool: <name>`（与内置一致）。

### 3.3 验收

不改 C、不重新 `make` 的前提下（仅改 yaml + 脚本）：5 分钟挂上 `echo_args`，`./neo "调用 echo_args，传入 {\"ping\":1}"` 出现 `neo tool: echo_args` 且结果含输入 JSON。  
（说明：首次实现该能力仍需改 C 并 make；验收指**之后**加新刀刃无需再改 C。）

---

## 4. Workflows

### 4.1 配置

```yaml
workflows:
  - name: demo_loop
    description: "tool → llm 摘要，最多重复 2 轮"
    steps:
      - id: fetch
        type: tool
        tool: echo_args
        args: {"ping": 1}
      - id: summarize
        type: llm
        prompt: "用一句中文总结：\n{{prev}}"
        tools: off          # off | on
      - id: again
        type: loop
        over: [fetch, summarize]
        max: 2
        until: always       # 第一期：仅配合 max；跑满 max 次即停
```

### 4.2 Step 类型

| type | 行为 |
|------|------|
| `tool` | 直接调用已声明 tool（内置或 `commands`），不经模型选 tool |
| `llm` | 一次用户消息（模板展开）；`tools: on` 时走 `agent_run_with_tools`（受 `max_rounds`） |
| `loop` | 按顺序重复 `over` 中的 step id，最多 `max` 次（`max` 必填且 ≥ 1） |

**模板变量（第一期）**

- `{{prev}}`：上一步文本输出
- `{{steps.<id>}}`：指定 id 最近一次输出

### 4.3 入口

```bash
./neo workflow run <name>
./neo -p <profile> workflow run <name>
```

自然语言 `./neo "..."` **不**自动跑 workflow。

### 4.4 错误与输出

- 任一步失败（tool 非 0、未知 tool、未知 step id、模板缺变量）：中止；stderr：`neo: workflow:<name> step:<id>: <reason>`
- `tools.enabled` 关闭时：`type: tool` 与 `llm.tools: on` 直接失败
- stdout：最后一步的文本结果

---

## 5. Profiles

### 5.1 布局

```text
profiles/<name>/
  neo.yaml          # 默认主配置文件名
  SOUL.md / RULES.md / AGENTS.md / MEMORY.md   # 可选
  skills/           # 可选
  scripts/tools/    # 可选，该 profile 的刀刃脚本
```

### 5.2 加载

- `./neo -p <name>` 或 `NEO_PROFILE=<name>`：配置根为 `profiles/<name>/`，默认读 `neo.yaml`
- `-c` / `--config`：若为相对路径，相对 **profile 目录**；若为绝对路径则原样使用
- yaml 内相对路径（soul、rules、bootstrap、memory、skills、tools.root、commands argv）相对 **profile 目录**
- 未指定 `-p`：与现网一致（cwd + `config.yaml` / `NEO_CONFIG`）
- 显式 `-p` 优先于 `NEO_PROFILE`

---

## 6. neo-ask 入口

`scripts/neo-ask`：只拼参数调用仓库根（或 `NEO_BIN`）下的 `neo`，不复制业务逻辑。

```bash
./scripts/neo-ask -p ops "状态怎么样"
echo "摘要这段" | ./scripts/neo-ask -p ops --stdin
./scripts/neo-ask -p ops --workflow demo_loop
```

文档提供一条 cron 样例（例如每小时跑某 workflow）。

---

## 7. 错误处理与测试

### 错误处理

- 配置解析失败：启动即退出，stderr 指明字段（重复 name、空 argv、loop 缺 max）
- exec 失败（找不到文件、非可执行）：tool 结果 `ERROR: ...`，不崩溃
- workflow 未知 name：exit 1，列出可用 workflow 名（若有）

### 测试策略

仓库目前无自动化测试框架。第一期采用：

1. **小测试二进制**（链接 `config.c` 等）或 `make test` 跑 shell 用例
2. **不依赖真实 LLM** 的用例：解析 yaml、`commands` 注册表、对本地脚本 `type: tool` + `loop`（mock：workflow 的 `llm` 步可用固定 fixture 或跳过集成）
3. **可选手动**：对接真实 API 的 smoke（不进 CI 强制）

最小必须通过：

- 解析含 `commands` + `workflows` 的 yaml
- `echo_args` 脚本经 tool 路径返回 stdin JSON
- `workflow run` 含 `loop max:2` 实际执行 tool 步 2 次

---

## 8. 实现分期（与计划对齐）

| 期 | 交付 | 可独立验收 |
|----|------|------------|
| 1 | `tools.commands` + echo 样例 + 文档 | 自定义 tool 可被模型调用 |
| 2 | `workflow run`（tool / llm / loop） | 声明式简单 loop |
| 3 | `-p` profiles + `neo-ask` + cron 样例 | 换刀柄 + 外部唤起 |

---

## 9. 代码落点（预期）

| 主题 | 文件 |
|------|------|
| 配置结构 / 解析 | `src/config.h`, `src/config.c` |
| 命令 tool exec + 注册进 tools JSON | `src/agent_tools.c`（可拆 `src/command_tools.c` 若膨胀） |
| workflow 引擎 | 新建 `src/workflow.c` / `src/workflow.h` |
| CLI：`-p`、`workflow run` | `src/main.c` |
| 样例脚本 / profile | `scripts/tools/`、`profiles/demo/`、`scripts/neo-ask` |
| 用户文档 | `README.md`、`doc/tool.md`、可选 `doc/workflow.md` |

---

## 10. 成功标准

1. 加新刀刃：只改 yaml + 脚本，无需改 C / make。
2. 简单 loop：yaml 内 `loop` + `max` 可重复 tool→llm（或 tool→tool）。
3. `-p` 切换整套人设与工具根，相对路径不串台。
4. cron / 管道可通过 `neo-ask` 一发一收或跑 workflow。
5. 仍保持单二进制 + libcurl；无新运行时依赖。
