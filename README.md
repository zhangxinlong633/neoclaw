# Neo — 简易 C 语言 Agent

一个进程处理一次查询（或 daemon 多轮），从命令行读用户输入，按 YAML 配置的 model、bootstrap、skills、memory 请求 LLM，回复打 stdout。支持 **skill 目录扫描**、**高优先级单独列**、**按需/摘要注入**，适合小参数模型（如 128k 上下文）。

**适用场景与资源**：适合 **本机/原生部署**：单二进制、仅依赖 libcurl，无运行时、无容器。使用 **小模型即可**，例如 **Qwen 3 8B**（`qwen/qwen3-8b`）即可满足日常问答与 skills 调用；**资源占用极小**（进程内存主要来自 curl 与少量缓冲），适合树莓派、边缘设备等 **IoT / 边缘场景**，也可在笔记本或服务器上常驻 daemon 做本地助手。

## 依赖

- **libcurl**
  - macOS: 通常已带
  - Ubuntu/Debian: `sudo apt install libcurl4-openssl-dev`

## 构建

```bash
cd neo
make
```

## 配置

复制示例并修改：

```bash
cp config.yaml.example config.yaml
```

### 配置项说明

| 节 | 说明 |
|----|------|
| **model** | `base_url`、`name`、`api_key`；可选 `max_tokens`（默认 4096，请求时上限 16384 以免 502）、`temperature`（默认 0.7） |
| **bootstrap** | 身份/系统上下文文件列表（如 AGENTS.md、SOUL.md），每文件可限制 `max_chars_per_file` |
| **skills** | 可选 `directory: "skills"` 扫描子目录的 SKILL.md；可选 `high_priority: [name]` 单独列高优先级；`unmatched: index \| skip`。见下方「Skill 注入框架」。 |
| **memory** | `path` 指向 MEMORY.md，`max_chars` 限制注入长度 |
| **session** | daemon 用：`max_turns` 为保留的 user+assistant 对话对数（默认 10） |

### 环境变量覆盖

- `NEO_CONFIG` — 配置文件路径（默认 `config.yaml`）
- `NEO_MODEL` — 覆盖 `model.name`
- `NEO_API_KEY` — 覆盖 `model.api_key`

### 命令行选项

- `-c, --config PATH` — 指定配置文件
- `-m, --model NAME` — 覆盖本次使用的模型名
- `-d, --debug` — 将请求参数、loaded skills、system prompt、用户消息打到 stderr（分色），便于排查
- `-h, --help` — 显示帮助

## 使用

### 单次查询（默认）

```bash
# 使用当前目录 config.yaml
./neo "你的问题"

# 指定配置与模型
./neo --config /path/to/config.yaml --model openai/gpt-4o-mini "总结一下"

# 环境变量
NEO_MODEL=qwen/qwen3-32b ./neo "你好"
```

- 用户输入：除选项外的命令行参数拼接成一条用户消息。
- 输出：LLM 回复到 **stdout**；错误到 stderr。

### 守护进程模式（多轮会话）

以常驻进程方式运行，维护会话历史，更像真实 agent：

```bash
# 从 stdin 读一行、打一行回复，支持多轮对话；输入 exit / quit 或 EOF 退出
./neo daemon

# 使用 Unix socket：客户端连上后发一行消息、收一行回复，会话在服务端共享
./neo daemon --socket /tmp/neo.sock
```

- **stdin 模式**：每行一条用户消息，回复打印到 stdout；会话轮数由配置 `session.max_turns` 限制（默认 10 对）。
- **socket 模式**（Linux/macOS）：监听指定路径；每个连接发一行、收一行，同一进程内共享会话历史。
- 配置中的 **bootstrap / skills / memory** 在 daemon 下同样生效；每次请求会重新注入当前时间。

### 典型效果示例

- **问「南京2026旅游数据」**：若配置了 `skills/nanjing/SKILL.md` 且为高优先级，会优先引用 skill 中的**必引**数据（2026 年春节首日 **134.8 万人次**、2025 年春节整假期 **1690 万人次**、收入 181 亿元等），而不是模型自行推测。
- **问「你是谁」**：若 `unmatched: index`，system 中会包含所有 skill 的简短摘要（含 summarize、note、todo、nanjing），模型可回答为「Neo 助手，能总结、记录笔记、管理待办、查南京旅游数据等」；若 `unmatched: skip` 则仅注入高优先级 skill，回复更偏通用模型身份。

### 默认 Skills 与 Memory

仓库内提供示例能力与记忆模板，可在 `config.yaml` 中引用：

| 路径 | 说明 |
|------|------|
| **skills/me/SKILL.md** | 「我是谁」身份说明：自称 Neo，并列举当前已加载能力（总结、笔记、待办、南京等） |
| **skills/summarize/SKILL.md** | 按用户要求总结对话或内容，并建议写入 memory |
| **skills/note/SKILL.md** | 将用户说的「记住」类内容整理成可追加到 MEMORY.md 的句子 |
| **skills/todo/SKILL.md** | 从对话或 memory 中整理/列出待办，建议更新 TODO 或 MEMORY.md |
| **skills/explain/SKILL.md** | 解释概念、用法或步骤，用通俗语言或分步说明 |
| **skills/code/SKILL.md** | 按需求给出可运行的代码、脚本或命令，并做必要注释与风险提示 |
| **skills/translate/SKILL.md** | 按用户指定或推断的语种翻译文本，标清原文/译文，保持语气与术语 |
| **skills/nanjing/SKILL.md** | 南京说明与**必引**旅游数据（2026 春节首日 134.8 万、2025 整假期 1690 万等），高优先级时全文优先注入 |
| **MEMORY.md** | 记忆模板：事实、任务、偏好等，由 `memory.path` 指向，按 `max_chars` 截断注入 |

在示例配置中可用 `directory: "skills"` + `high_priority: [nanjing]` + `unmatched: index` 或 `skip`，无需逐条写 `- path:`。

**Skill 按需加载**：与用户消息「匹配」的 skill（关键词如谁/身份、南京、总结、记住、待办、解释、代码/脚本）注入全文；未匹配的由 `unmatched` 决定：`index` 注入约 400 字摘要，`skip` 不注入。

**Skill 注入框架**（适合小参数/短上下文模型，如 128k 窗口）：

| 配置 | 含义 |
|------|------|
| **directory: "skills"** | 扫描该目录下每个子目录的 `SKILL.md`（如 `skills/nanjing/SKILL.md`），自动加入 skill 列表；可与 `- path:` 混用（目录项会排在前面）。 |
| **high_priority: [...]** | 单独列出高优先级：写子目录名（如 `nanjing`）或完整路径；这些 skill **必加载**全文且排在最前。 |
| **- path: ...** | 逐条指定 skill 文件；可加 `priority: high` 或依赖上面的 `high_priority` 列表。 |
| **priority: high**（单条） | 该条为高优先级（与 `high_priority` 二选一或共用）。 |
| **unmatched: index**（默认） | 未匹配的普通 skill 仍注入前约 400 字摘要。 |
| **unmatched: skip** | 未匹配的普通 skill 不注入，节省 context。 |

配置示例（目录 + 高优先级单独列）：`skills:` 下写 `directory: "skills"`、`high_priority: [nanjing]`。要节省 context 用 `unmatched: skip`（仅高优先级 + 匹配的 skill）；要让「你是谁」等回答体现全部能力用 `unmatched: index`（未匹配的也注入约 400 字摘要）。**注意**：`unmatched:` 后的值只认紧跟的 `index` 或 `skip`，行内注释若含 "skip" 会被忽略，不会误判。

### 排查与注意

- **debug**：`./neo -d "消息"` 会在 stderr 打印请求参数、loaded skills、完整 system prompt、用户消息；若为 TTY 会分色。`unmatched: skip` 时会提示「仅 high-priority + matched 在 prompt 中」。
- **502 / 未知错误**：请求里 `max_tokens` 会在代码中上限为 16384，避免部分厂商拒接；若仍 502，stderr 会打印响应体前 512 字便于查看原因。
- **部分 skill 没进 prompt**：检查是否为 `unmatched: skip` 且该 skill 未匹配用户消息；或配置里 `unmatched:` 的注释含 "skip" 导致被误解析为 skip（已修复为只解析冒号后第一个词）。

## 流程简述

1. 读取 **config.yaml**（或 `NEO_CONFIG` / `-c`）。
2. **System prompt** 顺序：固定说明 → **当前日期时间** → **高优先级 skills**（全文）→ **bootstrap** 文件 → **普通 skills**（匹配则全文，否则按 `unmatched` 注入摘要或跳过）→ **memory** 文件。
3. 用户消息 = 命令行参数拼接（或 daemon 下当前行）。
4. POST 到 `base_url/chat/completions`（OpenAI 兼容），带 `max_tokens`（上限 16384）、`temperature`；遇 429/5xx 自动重试一次，非 200 时在 stderr 打印响应片段。
5. 从响应中取出 `content`，写入 **stdout**。
