# Neo

可移植的命令行 AI 助手：以 **DAG ∥ Capability Matrix ∥ Policy** 为产品三角，强调接缝灵活与边界可控，而非堆叠最强 IDE 级能力。

开发约定见 [`AGENTS.md`](AGENTS.md) / [`CLAUDE.md`](CLAUDE.md)（二者须保持同步）。完整命令样例见 [`docs/examples.md`](docs/examples.md)。

## 产品定位

| 原则 | 说明 |
|------|------|
| 接缝优先 | 能力通过 JSON5（`capability_matrix`、MCP stdio）与 claw 文件（soul/bootstrap/rules/memory）挂载；换机拷贝配置即可复用 |
| 编排确定性 | **DAG** 决定执行拓扑（`workflow` / `neo plan` / `neo run`）；LLM 作为步骤内工人，不改写图结构 |
| 能力可发现 | **Capability Matrix** 统一 builtin / commands / MCP 的稳定 `name`，供 tool loop、DAG `type: tool` 与 planner 共用 |
| 边界可控 | **Policy** 约束 shell、HTTP、轮次与输出上限等；物化矩阵时裁剪 `enabled` 行 |
| 入口多元 | 终端单次、daemon / Unix socket、管道与 cron（`scripts/neo-ask`）、多配置编排（`scripts/neo-team`）共用同一核心 |
| 运行时轻量 | 单二进制 + libcurl + 内置 yyjson；不做 Temporal、重插件宿主或完整 IDE agent |

**非目标**摘要：Cursor 级多文件 UX、MCP resources/prompts 一等运行时、v1 MCP HTTP/SSE、默认开启 `shell_enabled`。详见 `AGENTS.md` §5。

## 快速开始

1. **依赖**：macOS 通常已带 libcurl；Linux 可安装 `libcurl4-openssl-dev`（或发行版等价包）。
2. **构建**（在仓库根目录）：

```bash
make
```

3. **配置**：

```bash
cp config/config.json5.example config/config.json5
# 编辑 model.api_key、model.name
# 确认 capability_matrix.directory 与 workflow_directory（示例已含）
```

默认优先读取 `config/config.json5`；仍兼容仓库根 `config.json5`。可用 `-c` / `NEO_CONFIG` 覆盖。

4. **运行**：

```bash
./neo "你是谁"
```

## 简单样例

下列命令均在**仓库根**执行；需已配置可用的 `model.api_key`。更多场景与排错见 [`docs/examples.md`](docs/examples.md)。

```bash
# 单次对话
./neo "用三句话介绍 Neo 的产品三角"

# 确定性 DAG：打印 UTC 时间（需 workflow_directory: "dags"）
./neo workflow run show_time

# 规划并执行（选型 catalog 或现编 tool 步）
./neo run "看下系统时间"

# 反应式工具：读文件并概括
./neo "请使用 read_file，path 为 README.md；用三句中文概括。" 2>&1

# 仅看规划 JSON（不执行）
./neo plan "对比 Capability Matrix 与 DAG 各自管什么"
```

期望在涉及本地能力时，stderr 出现 `neo tool: …`；助手可见内容在 stdout。

## 使用方式

### 单次查询

除选项外的参数拼成一条用户消息；模型回复写入 stdout。

```bash
./neo "你的问题"
./neo -c config/config.json5 -m deepseek-chat "总结一下"
```

### 多轮会话（daemon）

- stdin：`./neo daemon`（`exit` 或 EOF 结束）
- Unix socket：`./neo daemon --socket /tmp/neo.sock`，例如 `echo "问题" | nc -U /tmp/neo.sock`

轮次上限由 `session.max_turns` 控制（默认 10 对）。

### 能力矩阵、Workflow 与 Plan

| 能力 | 入口 | 文档 |
|------|------|------|
| 自定义命令 / 矩阵 Policy | 配置 `capability_matrix`；可选 `directory: "capabilities"` | [`docs/tool.md`](docs/tool.md) |
| 声明式 DAG | `./neo workflow run NAME`；可选 `workflow_directory: "dags"` | [`docs/workflow.md`](docs/workflow.md) |
| 规划并执行 | `./neo run [--steps N] [-o FILE] "task"`；仅规划用 `./neo plan` | 同上 |
| 命令样例合集 | 对话 / DAG / plan / 排错 | [`docs/examples.md`](docs/examples.md) |
| Profile | `./neo -p demo ...` → `config/profiles/demo/neo.json5` | 本 README「配置」 |
| 管道 / cron | `./scripts/neo-ask -p demo --workflow demo_loop` | [`scripts/`](scripts/) |

知识类任务规划默认约 4 步（理解 → 起草 → 自检 → 终稿）；工程类默认约 10 步流水线，可用 `--steps` 调整。

### 常用选项与环境变量

| 选项 / 变量 | 说明 |
|-------------|------|
| `-c, --config PATH` | 配置文件路径 |
| `-m, --model NAME` | 覆盖本次模型名 |
| `-p, --profile NAME` | 切换 profile（chdir 至 profile 目录） |
| `-v, --verbose` | 步骤与能力相关日志输出到 stderr |
| `-d, --debug` | 打印请求参数、完整 system prompt 等 |
| `-h, --help` | 用法说明 |
| `NEO_CONFIG` / `NEO_MODEL` / `NEO_API_KEY` | 覆盖配置中的对应项 |
| `NEO_DISABLE_TOOLS=1` | 本次禁用工具（不注册矩阵行） |

## 仓库布局

| 路径 | 职责 |
|------|------|
| `src/` | C 源码（`cli` / `core` / `llm` / `capability` / `workflow` / `vendor`）；对象文件在 `build/` |
| `config/` | 默认配置示例与 profiles |
| `capabilities/` | 能力目录包（`local/`、`git/`、`unix/` 等；见目录 README） |
| `dags/` | DAG 目录包（`baseline/`、`workspace/` 等；见目录 README） |
| `rules/` | claw Rules：身份、领域必引数据、答法手册 |
| `docs/` | 用户文档（架构、场景、样例、矩阵、DAG、claw） |
| `tests/` | 单元测试与 CLI 冒烟；夹具在 `fixtures/` |
| `scripts/` | 仓库级辅助脚本 |
| `example/` | claw 式 soul / bootstrap / rules / memory 注入示例 |

各子目录均有正式 `README.md`（见 `AGENTS.md` §6）。

## 配置要点

| 配置节 | 说明 |
|--------|------|
| `model` | `base_url`、`name`、`api_key`；可选 `max_tokens`、`temperature` |
| `capability_matrix` | 矩阵总开关、沙箱 `root`、commands、MCP、directory、Policy 旋钮（旧顶层键 `tools` 仅兼容读取） |
| `workflows` / `workflow_directory` | 内联 DAG 与/或目录加载（catalog 选型依赖此项） |
| `soul` / `bootstrap` / `rules` | 人格、身份与硬约束 Markdown（见 [`docs/claw.md`](docs/claw.md)） |
| `workspace` | 可选 `prompt_cwd: true`，将进程 cwd 写入 system |
| `memory` | `path`、`max_chars` |
| `session` | daemon 用 `max_turns` |
| `plan` | 可选规划软目标（如 `target_steps`） |

| 文档 | 用途 |
|------|------|
| [`docs/examples.md`](docs/examples.md) | 使用样例（优先阅读） |
| [`docs/architecture.md`](docs/architecture.md) | 目标架构 |
| [`docs/applications.md`](docs/applications.md) | 应用场景与定位 |
| [`docs/tool.md`](docs/tool.md) | 能力矩阵 |
| [`docs/workflow.md`](docs/workflow.md) | DAG / plan / run |
| [`docs/claw.md`](docs/claw.md) | soul / bootstrap / rules / memory |
| [`docs/migrate-json.md`](docs/migrate-json.md) | YAML → JSON5 |

旧键 `skills` 已移除（配置中若仍出现仅 stderr 提示）。旧顶层键 `tools` 仅兼容读取。

## Rules、Memory 与能力发现

| 路径 | 作用 |
|------|------|
| `rules/identity.md` | 自称 Neo；能力以矩阵为准 |
| `rules/nanjing.md` | 南京旅游必引数据 |
| `rules/response-playbook.md` | 总结/笔记/待办/解释/代码/翻译答法 |
| `MEMORY.md`（`memory.path`） | 长期笔记，按 `max_chars` 截断注入 |

可发现能力依赖 **Capability Matrix**（`capabilities/`）与 **DAG catalog**（`dags/`）。排查：`./neo -d "消息"` 查看完整 system prompt。

## 请求流程（摘要）

1. 加载配置（`config/config.json5` 或 `-c` / `NEO_CONFIG`）。
2. 组装 system prompt：固定说明 → 时间 →（可选）cwd → soul → bootstrap → rules → memory →（若启用）能力矩阵 listing。
3. 用户消息来自命令行参数或 daemon 当前行。
4. 若矩阵启用：进入 tool loop 或执行 DAG；否则仅 chat completion。
5. 助手可见内容写入 **stdout**；诊断与步骤日志写入 **stderr**。

## 构建与测试

```bash
make          # 生成 ./neo
make test     # 单元测试 + tests/cli_capability_matrix.sh
make test-cli # 仅 CLI 冒烟
```

贡献或改矩阵 / workflow / plan 前，请按 `AGENTS.md` §9 自检。
