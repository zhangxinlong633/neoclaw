# Neo — 到处能跑的灵活 Agent

## 定位

**Neo 不做「最强」的 agent，做「最灵活」、能在到处运行的瑞士军刀。**

- **强在接缝，不在堆能力**：刀刃用 YAML + 脚本挂上（`tools.commands`、skills、**确定性 DAG workflow**），换机器拷配置就能用。
- **编排确定性**：`neo workflow run` 的拓扑由配置声明，LLM **只当图中的 Worker**，不参与流程控制；自然语言对话仍可用 `./neo "..."`。
- **强在入口，不在生态锁**：终端、daemon / socket、管道与 cron（`neo-ask`）、`neo-team` 都能唤起同一核心。
- **强在轻量，不在全家桶**：单二进制 + libcurl + 内置 yyjson；不做 Temporal / 重插件 / 完整 MCP 宿主。

一句话：**能塞进 shell、CI、IoT 和仓库旁路的小助手**——能力按需配置，而不是预装巨无。

## 这个程序是做什么的

**Neo 是一个用命令行调用的 AI 助手**：你在终端输入问题，Neo 把问题和你配置好的「身份、技能、记忆」一起发给大模型（OpenAI 兼容 API），再把模型的回复打印到终端。

- **能做什么**：问答、翻译、解释、写代码片段、查领域数据、总结、记笔记、列待办、跑你声明的本地命令 / 短 workflow 等——取决于 `config/`、`skills/` 与脚本，而不是写死在二进制里。
- **怎么用**：`./neo "问题"`；多轮用 `./neo daemon` 或 socket；场景切换用 `-p` profile；定时 / 管道用 `scripts/neo-ask`。
- **配置放哪**：优先 `config/config.json5`；profile 在 `config/profiles/<name>/`（仍兼容根目录 `config.json5` 与旧 `profiles/`）。

---

## 快速开始

**1. 依赖**  
- macOS 一般已带 libcurl；Linux：`sudo apt install libcurl4-openssl-dev`

**2. 构建**

```bash
cd neo
make
```

**3. 配置**  
复制示例配置并填入自己的 API 与模型名：

```bash
cp config/config.json5.example config/config.json5
# 编辑 config/config.json5：model.api_key、model.name（如 qwen/qwen3-8b）
```

也兼容仓库根的 `config.json5`（若存在则仍可读；默认优先 `config/config.json5`）。

**4. 跑一条**

```bash
./neo "你是谁"
```

---

## 使用方式

### 单次查询（默认）

除选项外的参数会拼成一条用户消息，回复打到 stdout：

```bash
./neo "你的问题"
./neo -c config/config.json5 -m qwen/qwen3-8b "总结一下"
```

### 多轮对话（daemon）

- **stdin**：`./neo daemon`，然后逐行输入，输入 `exit` 或 EOF 结束。
- **Socket**：`./neo daemon --socket /tmp/neo.sock`，其它进程用 `echo "问题" | nc -U /tmp/neo.sock` 一发一收。

会话轮数由配置里 `session.max_turns` 限制（默认 10 对）。

### Workflow / Profile / neo-ask

- **自定义命令工具**：见 `docs/tool.md`（`tools.commands`）。
- **声明式 workflow**：`./neo workflow run NAME`，说明见 `docs/workflow.md`。
- **Plan then Run**：`./neo run [--steps N] [-o FILE] "task"` — 默认按约 10 步「研发团队」流水线规划并执行；`./neo plan` 只规划。
- **Profile**：`./neo -p demo ...` 使用 `config/profiles/demo/neo.yaml`（兼容旧路径 `profiles/demo/`）。
- **管道/cron**：`./scripts/neo-ask -p demo --workflow demo_loop`。

### 示例命令与运行效果（qwen3-8b）

```bash
# 身份 / 能力
./neo "你是谁"

# 领域数据（需 config 里 high_priority: [nanjing]）
./neo "南京2026旅游数据"

# 翻译、解释、要代码
./neo "把 Hello world 译成中文"
./neo "什么是 REST API？用一句话说"
./neo "写一个 bash 循环，列出当前目录下所有 .md 文件"
```

**实际输出片段（模型 qwen3-8b）：**

```text
$ ./neo "你是谁"
我是 Neo，一个基于配置运行的本地/命令行助手。我能够通过多种技能协助你，例如：  
- 提供南京旅游数据（如2026年春节首日接待游客134.8万人次）  
- 记录笔记、解释概念、翻译文本  
- 总结内容、编写代码、管理待办任务  
- 其他你需要的帮助 😊  

需要我帮你做什么？

$ ./neo "将你是谁翻译成英文"
原文：你是谁  
译文：Who are you?

$ ./neo "将你是谁翻译成日文"  
原文：你是谁  
译文：あなたは誰ですか
```

### 常用选项

| 选项 | 说明 |
|------|------|
| `-c, --config PATH` | 指定配置文件 |
| `-m, --model NAME` | 本次使用的模型名 |
| `-d, --debug` | 在 stderr 打印请求参数、loaded skills、system prompt、用户消息，便于排查 |
| `-h, --help` | 帮助 |

环境变量可覆盖配置：`NEO_CONFIG`、`NEO_MODEL`、`NEO_API_KEY`。

---

## 配置说明

| 配置节 | 说明 |
|--------|------|
| **model** | `base_url`、`name`、`api_key`；可选 `max_tokens`（默认 4096，内部上限 16384）、`temperature`（默认 0.7） |
| **soul** | （可选）OpenClaw 式人格/语气文件：`path`、`max_chars`；在 system 里排在高优 skills 之后、bootstrap 之前 |
| **bootstrap** | 身份/系统上下文文件列表（如 AGENTS.md），每文件可设 `max_chars_per_file` |
| **rules** | （可选）项目规则文件列表（如 RULES.md），`max_chars_per_file`；注入在 bootstrap 之后、普通 skills 之前 |
| **workspace** | （可选）`prompt_cwd: true` 时在 system 中写入 Neo 进程当前工作目录，便于对齐仓库根 |
| **skills** | 见下方「Skills」：`directory` 扫描、`high_priority`、`unmatched: index \| skip` |
| **memory** | `path` 指向 MEMORY.md，`max_chars` 限制注入长度 |
| **session** | daemon 用：`max_turns` 为保留的对话对数（默认 10） |

---

## Skills 与 Memory

### 自带的 Skills（`directory: "skills"` 时自动扫描）

| 路径 | 作用 |
|------|------|
| **skills/me/SKILL.md** | 「我是谁」：自称 Neo，列举当前能力 |
| **skills/summarize/SKILL.md** | 总结对话或内容，可建议写入 memory |
| **skills/note/SKILL.md** | 把「记住」类内容整理成可追加到 MEMORY.md 的句子 |
| **skills/todo/SKILL.md** | 从对话或 memory 列待办，建议格式与持久化 |
| **skills/explain/SKILL.md** | 解释概念、用法，通俗或分步说明 |
| **skills/code/SKILL.md** | 给出可运行代码/脚本/命令，带注释与风险提示 |
| **skills/translate/SKILL.md** | 翻译，标原文/译文 |
| **skills/nanjing/SKILL.md** | 南京旅游必引数据（134.8 万/1690 万等），建议设 high_priority |
| **MEMORY.md** | 记忆：事实、任务、偏好，由 `memory.path` 指向，按 `max_chars` 截断 |

### Skill 怎么进 prompt

- **高优先级**（`high_priority: [nanjing]` 等）：**全文**注入且排在最前。
- **普通 skill**：和用户消息**匹配**的（如含「谁/身份」「南京」「翻译」「代码」等关键词）注入全文；未匹配的由 `unmatched` 决定：
  - `unmatched: index`（默认）：未匹配的也注入前约 400 字摘要。
  - `unmatched: skip`：未匹配的不注入，省 context。

配置示例：`skills:` 下写 `directory: "skills"`、`high_priority: [nanjing]`、`unmatched: index` 或 `skip`。注意 `unmatched:` 只认紧跟的 `index`/`skip`，行内注释里的 "skip" 不会误判。

### 排查

- **看请求与 prompt**：`./neo -d "消息"`，stderr 会打 params、loaded skills、完整 system prompt、用户消息。
- **502**：`max_tokens` 已限制在 16384；若仍 502，stderr 会打响应体前 512 字。
- **某 skill 没进 prompt**：看是否 `unmatched: skip` 且该 skill 未匹配用户消息；或配置里 `unmatched:` 写错。

---

## 流程简述

1. 读 **config/config.json5**（或根目录 `config.json5` / `NEO_CONFIG` / `-c`）。
2. 拼 **system prompt**：固定说明 → 当前时间 →（可选）**cwd** → **高优先级 skills（全文）** →（可选）**soul** → bootstrap →（可选）**rules** → **普通 skills（匹配全文 / 未匹配摘要或跳过）** → memory 文件 →（若启用）tools 说明。
3. 用户消息 = 命令行参数拼接（或 daemon 下当前行）。
4. POST 到 `base_url/chat/completions`（OpenAI 兼容），带 `max_tokens`、`temperature`；非 200 时 stderr 打响应片段。
5. 取响应里的 `content` 写到 **stdout**。
