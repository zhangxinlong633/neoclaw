# Neo 的 OpenClaw 式用法与操作

Neo 把「钉在仓库上的助手」常见做法拆成 **配置 + 若干 Markdown 文件**：身份（bootstrap）、人格（soul）、硬约束（rules）、长期笔记（memory）、按需技能（skills），以及可选的 **工作区 cwd 提示** 与 **本地工具**。本文说明这些块的**用途、在 prompt 里的顺序、YAML 写法与日常操作**。

更细的 **read_file / write_file / list_dir / http_get** 见同目录下的 [tool.md](tool.md)。

---

## 对照：哪些是「真的 claw 味」、哪些不是

下面这张表用来**对齐预期**：Neo **没有**去调用某个独立的「官方 OpenClaw」运行时或专有协议；**有**的是与「钉在仓库上的助手」常见分工**相似**的配置与文件角色。

| 维度 | 常见 OpenClaw / 类 claw 生态里可能有的 | Neo（本仓库）实际 |
|------|----------------------------------------|-------------------|
| **AGENTS / SOUL / Rules / Memory** | 用固定文件名或约定承载身份、人格、约束、笔记 | ✅ 用 **`bootstrap` / `soul` / `rules` / `memory`** 读 Markdown（或 README）**拼进 system prompt**；不替你托管远端账号 |
| **运行时** | 可能是独立守护进程、插件宿主、或与 IDE 深度绑定 | ❌ **仅** `neo` 进程：`./neo` 单次或 **`neo daemon`**（stdin / Unix socket），无第三方 claw 二进制 |
| **模型 API** | 视产品而定 | ✅ **OpenAI 兼容** `chat/completions`；可选 **function tools**（本地执行，见 [tool.md](tool.md)） |
| **自动化** | 定时唤醒、Webhook、多代理编排等 | ⚠️ **部分**：daemon 常驻会话、**`scripts/neo-team`** 多配置并行/合并；**无**内置 cron / 通用钩子系统 |
| **可审计** | 依产品 | ✅ 用 **`./neo -d`** 可在 stderr 看到**完整 system prompt**，claw 相关段落是否出现一目了然 |

**一句话**：`doc/claw.md` 描述的是 **Neo 里真实存在的读配置、读文件、拼 prompt 等操作**；「OpenClaw 式」指 **习惯与结构上的类比**，不是与某一上游产品 **1:1 行为兼容** 的声明。若你本地另有 OpenClaw 发行版，需自行对照其文档，不要把 Neo 当成该产品的子进程或插件。

---

## 1. 各块分别解决什么问题

| 能力 | 典型内容 | 谁维护 |
|------|----------|--------|
| **bootstrap** | 仓库身份、协作约定、AGENTS 类说明 | 团队 / 仓库维护者 |
| **soul** | 语气、自称、幽默程度等人格层（OpenClaw 常叫 SOUL） | 个人或团队 |
| **rules** | 编码规范、禁止事项、必须用的栈（类似 Cursor Rules） | 团队 |
| **memory** | 会话间要记住的事实、偏好、进行中的任务摘要 | 你 + 模型建议后手改 |
| **skills** | 按主题拆好的 SKILL.md，按匹配与高优注入 | 仓库 |
| **workspace.prompt_cwd** | 告诉模型 Neo 进程**当前工作目录**（一般是仓库根） | 配置开关 |
| **tools** | 模型发起 tool_calls，Neo 在本地读/写/列目录或受控 HTTPS | 配置 + 允许列表 |

---

## 2. System prompt 里的顺序（实现约定）

单次 `./neo` 与 `neo daemon` 使用**同一套顺序**（便于你对照 `-d` 打出来的全文）：

1. 固定开场白（要求遵守 soul / rules / skills / bootstrap）
2. **当前 UTC 时间**
3. 若 `workspace.prompt_cwd: true`：**工作目录**（`getcwd()`，仅 Linux / macOS）
4. **高优先级 skills**（`high_priority` 命中者，全文）
5. 若配置了 **soul**：`## Soul` + `soul.path` 文件内容（截断到 `max_chars`）
6. **bootstrap**：每个 `## Bootstrap: <path>` + 内容（`max_chars_per_file`）
7. 若配置了 **rules**：每个 `## Rules: <path>` + 内容
8. **普通 skills**（匹配全文，或未匹配时的 index/skip 策略）
9. **memory**：`## Memory` + `memory.path` 内容
10. 若启用 tools：工具说明段（见 [tool.md](tool.md)）

**操作建议**：在仓库根目录执行 `./neo`，并与 `tools.root: "."`（或指向仓库 realpath）一起使用，这样 cwd、bootstrap 里的相对路径、工具沙箱三者一致。

---

## 3. 配置文件怎么写

配置文件默认可为 **`config/config.yaml`**（优先），其次仓库根 **`config.yaml`**；也可用 `-c /path/to/config.yaml` 或环境变量 `NEO_CONFIG`。Profile 优先 **`config/profiles/<name>/`**，兼容旧路径 **`profiles/<name>/`**。

完整字段示例以仓库内 **`config/config.yaml.example`** 为准；下面只强调 **OpenClaw 相关三节**。

### 3.1 `soul:`（人格 / 语气）

```yaml
soul:
  path: "SOUL.md"
  max_chars: 8000
```

- **`path`**：相对**你启动 Neo 时的当前工作目录**（一般为仓库根）。
- **`max_chars`**：单文件注入上限；缺省或非法时内部会回到合理默认（见源码 `config_init` / 收尾校验）。

**不要重复**：若已在 `soul.path` 指向 `SOUL.md`，就不要再把同一文件放进 `bootstrap` 的 `- path` 列表，否则同一段文字会进两次 prompt、浪费上下文。

### 3.2 `rules:`（项目规则，可多文件）

```yaml
rules:
  max_chars_per_file: 6000
  - path: "RULES.md"
```

- 与 bootstrap 类似：支持 **`max_chars_per_file`** 与多条 **`- path:`**（路径须真实存在，否则该段不会出现在 prompt 里）。
- 适合写：语言风格、必须跑的测试、禁止直接改生产配置等**硬约束**。

### 3.3 `workspace:`（把工作目录写进 prompt）

```yaml
workspace:
  prompt_cwd: true
```

- **`true`**：在 system 中增加 `## Workspace` 与 Neo 进程的当前工作目录字符串。
- **平台**：仅在 **Linux / macOS** 上生效；其他平台忽略该选项不报错。
- **操作要点**：在**本仓库根**执行 `./neo "..."` 或 `./neo daemon`，这样模型看到的目录与 `AGENTS.md`、`.git` 所在根一致。

### 3.4 与 bootstrap / memory 的配合

- **bootstrap**：继续用来列 `AGENTS.md` 等「任务与边界」；顺序上在 soul **之后**，因此 AGENTS 可以引用「人格见 Soul 段」这类分工。
- **memory**：仍是 `memory.path` + `max_chars`；适合放**会随时间变**的笔记，与相对稳定的 soul/rules 区分。

---

## 4. 日常操作命令

| 目的 | 命令或操作 |
|------|------------|
| 单次提问 | `./neo "你的问题"` |
| 指定配置 | `./neo -c /path/to/config.yaml "问题"` |
| 换模型名（覆盖 YAML） | `./neo -m qwen/qwen3-8b "问题"` |
| 看完整 system prompt 与请求 | `./neo -d "问题"`（详情在 stderr） |
| 多轮 stdin | `./neo daemon`，逐行输入，输入 `exit` 或 EOF 结束 |
| 多轮 socket | `./neo daemon --socket /tmp/neo.sock`，例如 `echo "问题" \| nc -U /tmp/neo.sock` |
| 会话长度 | 配置里 `session.max_turns`（daemon 保留的 user+assistant 对数） |
| 临时关掉工具 | `NEO_DISABLE_TOOLS=1 ./neo "..."`（与 [tool.md](tool.md) 一致） |
| API / 模型环境覆盖 | `NEO_MODEL`、`NEO_API_KEY`；配置路径仍可用 `NEO_CONFIG` |

**调试**：某段 soul/rules 没出现，先确认 `path` 相对 cwd 可读、且没有被 `max_chars` 截成空；再用 `-d` 在 stderr 里搜 `## Soul` / `## Rules`。

---

## 5. 示例（与本仓库真实路径、文件一致）

下列命令默认在 **neoclaw 仓库根**（即含 `Makefile`、`src/`、`skills/` 的目录）执行；二进制为 **`./neo`**（先在该目录 `make`）。进入目录可用：

```bash
cd "$(git rev-parse --show-toplevel 2>/dev/null)"   # 在任意子目录时回到仓库根；非 git 仓库则请手写路径
```

说明：**`MEMORY.md`** 与 **`README.md`** 仓库里已有；**`AGENTS.md` / `SOUL.md` / `RULES.md`** 默认不在版本库中（`config.yaml.example` 里写的是常见约定名）。示例里 bootstrap 用真实存在的 **`README.md`**；soul/rules 用「可复制落盘」的正文，避免指向不存在的路径。

### 5.0 本仓库真实截取（帮助、`README`、skill、`-d`）

下列内容来自 **neoclaw 仓库** 当前文件与一次本机命令输出；其中 **UTC 日期**、**模型回复** 会随你运行时间与网关变化，仅作形态参考。

#### 5.0.1 `./neo -h`（仓库根已 `make` 出 `./neo`）

```text
Usage: ./neo [OPTIONS] "your message"
       ./neo daemon [--socket PATH]
Options:
  -c, --config PATH   Config file (default: config.yaml or NEO_CONFIG)
  -m, --model NAME    Override model name
  -d, --debug         Print system prompt, user message and request params to stderr
  -h, --help          Show this help
  daemon              Run as daemon: read from stdin, reply to stdout
  --socket PATH       (with daemon) Listen on Unix socket instead of stdin
```

#### 5.0.2 `README.md` 里「多轮对话（daemon）」原文（节选）

与 **README.md** 第 52–57 行一致（行号随文档演进可能略有偏移，以文件为准）：

```markdown
### 多轮对话（daemon）

- **stdin**：`./neo daemon`，然后逐行输入，输入 `exit` 或 EOF 结束。
- **Socket**：`./neo daemon --socket /tmp/neo.sock`，其它进程用 `echo "问题" | nc -U /tmp/neo.sock` 一发一收。

会话轮数由配置里 `session.max_turns` 限制（默认 10 对）。
```

#### 5.0.3 `skills/me/SKILL.md` 开头（仓库内真实正文）

```markdown
# Skill: 我是谁（身份说明）

当用户问「你是谁」「你是啥」「介绍一下自己」「what are you」等与身份相关的问题时，按以下方式回答：

- **身份名称**：自称 **Neo**，是基于配置运行的助手；可说明「我是 Neo，一个本地/命令行助手」。
- **能力范围**：根据当前已加载的 skills 简要说明能做什么，例如：总结对话、记录笔记、管理待办、查南京旅游数据等（仅列举配置中存在的 skill，不要编造）。
- **使用方式**：可顺带一句「你通过命令行或 daemon 和我对话，我会按你的问题调用相应能力」。
- **语气**：简短、友好，用中文回答（除非用户明确用英文问且希望英文答）。不要以底层模型名称（如通义千问、Qwen）作为身份，统一以 Neo 作答。

仅在用户问题明显在问助手身份、能力或「你是谁」时启用本 skill。
```

#### 5.0.4 当前仓库内 skill 文件列表（shell 实录）

在仓库根执行 `ls skills/*/SKILL.md | sort`：

```text
skills/code/SKILL.md
skills/explain/SKILL.md
skills/me/SKILL.md
skills/nanjing/SKILL.md
skills/note/SKILL.md
skills/summarize/SKILL.md
skills/todo/SKILL.md
skills/translate/SKILL.md
```

#### 5.0.5 `NEO_DISABLE_TOOLS=1 ./neo -d "ping"` 的调试形态（本机一次截取）

在仓库根、已配置有效 **`config.yaml`**（本例未启用 `workspace` / `bootstrap` / `soul`，且关闭工具）、合并 **stderr + stdout** 后截取如下。**`Current date and time`** 会随运行变化；最后一行 **`pong`** 为模型 stdout 回复。

```text

=== NEO DEBUG: request params ===
base_url: https://openrouter.ai/api/v1
model: qwen/qwen3-32b
max_tokens: 4096
temperature: 0.70
loaded skills: skills/note/SKILL.md, skills/explain/SKILL.md, skills/translate/SKILL.md, skills/nanjing/SKILL.md, skills/summarize/SKILL.md, skills/code/SKILL.md, skills/todo/SKILL.md, skills/me/SKILL.md

=== NEO DEBUG: system prompt (778 chars) ===
You are a helpful assistant. Follow any soul, rules, skills, and bootstrap instructions below.

Current date and time: 2026-04-11 11:40 UTC

## Memory (context)



# Memory (context for Neo)

This file is loaded into the agent's system prompt (see `config.yaml` → `memory.path`). Keep it short; only the first `memory.max_chars` characters are sent to the model.

## Format

- **Facts**: `- YYYY-MM-DD: One-line fact or decision.`
- **Tasks**: `- [ ] Short task description` or `- [x] Done task.`
- **Preferences**: e.g. `- User prefers answers in Chinese.`

## Example

- 2025-02-23: Neo daemon mode uses stdin or Unix socket; session history is kept in memory.
- [ ] Try daemon with `neo daemon` and a few turns.
- User prefers concise replies unless they ask for detail.



=== END system prompt ===

=== NEO DEBUG: user message (4 chars) ===
ping
=== END user message ===

pong

```

若启用 **`workspace.prompt_cwd: true`**，在 `Current date and time` 之后会出现 **`## Workspace`** 与 `Neo process working directory: ...`；若启用 **bootstrap / soul / rules**，`system prompt` 字节数与段落会相应增加。启用 **tools** 且未设置 `NEO_DISABLE_TOOLS=1` 时，末尾会多出 **「## Tools (executed by host)」** 长段（见 [tool.md](tool.md)）。

### 5.1 与本仓库对齐的 `config.yaml` 片段

在仓库根已有 **`config.yaml.example`**；若你自建 `config.yaml`，可在此基础上增加 claw 段（模型与 key 请填你自己的，勿提交）：

```yaml
# 与 config.yaml.example 一致的部分略；以下为 neoclaw 演示用的真实路径

workspace:
  prompt_cwd: true

soul:
  path: "SOUL.md"
  max_chars: 4000

bootstrap:
  max_chars_per_file: 8000
  - path: "README.md"

rules:
  max_chars_per_file: 4000
  - path: "RULES.md"

skills:
  directory: "skills"
  high_priority:
    - "nanjing"
  unmatched: index

memory:
  path: "MEMORY.md"
  max_chars: 4000
```

- **`bootstrap: README.md`**：仓库根 **`README.md`** 真实存在，首段即写明 Neo 为「命令行 AI 助手」、依赖 libcurl、单次与 daemon 等（与当前文档一致）。
- **`high_priority: [nanjing]`**：对应真实文件 **`skills/nanjing/SKILL.md`**（README 中表格亦列出）。
- **`memory.path: MEMORY.md`**：与默认示例一致；正文见 **5.5**（与仓库文件逐字一致）。

### 5.2 新建 `SOUL.md`（仓库默认无此文件）

将以下内容保存为仓库根 **`SOUL.md`** 后，`soul.path` 才会被读入：

```markdown
# Soul

- 用中文回答，除非用户明确要求其它语言。
- 不编造未读过的仓库文件内容；需要时建议用 read_file / list_dir（若已启用 tools）。
```

### 5.3 新建 `RULES.md`（仓库默认无此文件）

将以下内容保存为仓库根 **`RULES.md`** 后，`rules` 段才会生效：

```markdown
# Rules（neoclaw）

- C 源码在 `src/`；构建命令为仓库根执行 `make`，产物为同目录下的 `neo` 二进制。
- 本地密钥与网关配置放在 `config.yaml`（该文件名在仓库 `.gitignore` 中，勿提交）。
- 技能 Markdown 位于 `skills/<name>/SKILL.md`；与 OpenClaw 相关的说明见 `doc/claw.md`，工具说明见 `doc/tool.md`。
```

以上三条与当前 **`.gitignore`**（含 `config.yaml`）、**`Makefile`**（生成 `neo`）、目录布局一致。

### 5.4 可选：自建 `AGENTS.md`

若希望与 `config.yaml.example` 完全一致，可在仓库根添加 **`AGENTS.md`**，并把上节 `bootstrap` 改回 `- path: "AGENTS.md"`。本仓库未自带该文件，故演示用 **`README.md`** 作 bootstrap。

### 5.5 仓库内真实的 `MEMORY.md`（节选即全文）

当前版本库中的 **`MEMORY.md`** 内容为（用于核对 `memory` 注入；若你本地改过，以你文件为准）：

```markdown
# Memory (context for Neo)

This file is loaded into the agent's system prompt (see `config.yaml` → `memory.path`). Keep it short; only the first `memory.max_chars` characters are sent to the model.

## Format

- **Facts**: `- YYYY-MM-DD: One-line fact or decision.`
- **Tasks**: `- [ ] Short task description` or `- [x] Done task.`
- **Preferences**: e.g. `- User prefers answers in Chinese.`

## Example

- 2025-02-23: Neo daemon mode uses stdin or Unix socket; session history is kept in memory.
- [ ] Try daemon with `neo daemon` and a few turns.
- User prefers concise replies unless they ask for detail.
```

### 5.6 终端示例（问题与 README / MEMORY 原文对应）

```bash
cd "$(git rev-parse --show-toplevel 2>/dev/null)"
make

# 对照 README「多轮对话（daemon）」小节：两种用法为 stdin 与 Socket（见上文 5.0.2 原文）
./neo -d "只回答两个词：README 里 daemon 两种用法叫什么（中文）" 2>&1 | grep -E 'stdin|socket|Socket|daemon' | head

# 对照 MEMORY.md 的 Format 小节：Facts / Tasks / Preferences
./neo "根据 MEMORY.md 的 Format 小节，列出三个小标题名称，不要解释" 2>&1

# 复现与 5.0.5 同类的调试形态（不发起 tool 循环，便于只看 system）
NEO_DISABLE_TOOLS=1 ./neo -d "ping" 2>&1 | head -45

# 若已按 5.1 启用 workspace：stderr 中应出现 ## Workspace 与本机 cwd
./neo -d "只回复：ok" 2>&1 | grep -E '## Workspace|working directory'

# 与 doc/tool.md 一致：临时关闭工具时的单次对话
NEO_DISABLE_TOOLS=1 ./neo "只回复一个词：pong" 2>&1
```

### 5.7 daemon 与 socket（与 README 表述一致）

README 写明：**stdin** 用 `./neo daemon`；**Socket** 用 `./neo daemon --socket /tmp/neo.sock`，并用 `nc -U` 一发一收。示例：

```bash
./neo daemon --socket /tmp/neo.sock
# 另一终端：
echo "用一句话说明 session.max_turns 在配置里管什么" | nc -U /tmp/neo.sock
```

（`session.max_turns` 说明见仓库根 **README.md** 配置表与 daemon 小节。）

### 5.8 `neo-team`：真实 JSON 与命令

仓库内 **`team.example.json`** 当前内容为：

```json
{
  "description": "Same config.yaml, two user_suffix styles in parallel, then one merge pass.",
  "mode": "parallel",
  "members": [
    {
      "name": "bullets",
      "config": "config.yaml",
      "user_suffix": "Answer in bullet points only (max 5)."
    },
    {
      "name": "prose",
      "config": "config.yaml",
      "user_suffix": "Answer in one short paragraph."
    }
  ],
  "merge": {
    "config": "config.yaml",
    "user_intro": "Two teammates answered the same question below. Write a 2–3 sentence synthesis in Chinese."
  }
}
```

在仓库根、已存在可执行的 **`./neo`** 时：

```bash
./scripts/neo-team team.example.json "README 里单次查询的示例命令是什么（只抄命令行）"
```

该脚本会读取上述 JSON 中的 **`config.yaml`**（成员与 merge 共用）；若你尚未创建 `config.yaml`，请先 `cp config.yaml.example config.yaml` 并填入 API，否则子进程会失败。

---

## 6. 仓库里建议准备的文件（可选）

本仓库已提供 **`example/`** 目录：`example/neo-claw-example.yaml` 指向 **`example/SOUL.md`**、`example/AGENTS.md`、`example/RULES.md`、`example/MEMORY.md`，在仓库根执行 `./neo -c example/neo-claw-example.yaml -d "ping"` 即可在 stderr 中看到 **`## Soul`** 等真实注入（详见 **`example/README.md`**）。

在仓库根按需创建（名称可自定，与 YAML 一致即可）：

- **`AGENTS.md`** — bootstrap：你是谁、为谁工作、默认分支与 PR 习惯等。
- **`SOUL.md`** — 建议用 **`soul:`** 引用，而不是再塞进 bootstrap。
- **`RULES.md`** — 用 **`rules:`** 引用；可与 AGENTS 交叉引用，避免单文件过长。
- **`MEMORY.md`** — 由 `memory.path` 指向；适合「上次做到哪」「用户偏好」等。

---

## 7. 多实例编排（可选）

仓库提供 **`scripts/neo-team`**：用 JSON 描述多个成员配置、并行或顺序跑多条 `./neo`，可选合并轮。与「单仓库单助手」互补，适合拆角色、多配置对比。用法见脚本顶部注释与 **`team.example.json`**（完整 JSON 见上文 **5.8**）。

---

## 8. 与代码的对应关系

| 主题 | 文件 |
|------|------|
| 解析 `soul` / `rules` / `workspace` | `src/config.c`、`src/config.h` |
| 单次模式拼 system prompt | `src/main.c` |
| daemon 拼 system prompt | `src/daemon.c` |
| 工具循环 | `src/agent_tools.c`（见 [tool.md](tool.md)） |

若你希望某能力在协议层也显式化（例如单独 HTTP 头），需要在 `src/llm.c` 侧扩展；当前 claw 相关能力都落在 **system 字符串** 与 **config** 上。
