# example/

演示 Neo 如何将 `soul` / `bootstrap` / `rules` / `memory` 所指向的 Markdown 注入 **system prompt**，行为与 [`docs/claw.md`](../docs/claw.md) 一致。

> 本目录中的 `AGENTS.md` 是**运行时** bootstrap 示例，**不是**仓库根开发约束文件（开发约束见根目录 `AGENTS.md` / `CLAUDE.md`）。

## 前提

1. 在仓库根执行 `make`，生成 `./neo`。
2. 编辑 `example/neo-claw-example.json5`，填入有效的 `model.api_key`，或设置环境变量 `NEO_API_KEY`。
3. 下列命令须在 **neoclaw 仓库根**执行（示例路径相对进程 cwd 解析）。

## 验证 Soul 已注入

```bash
cd "$(git rev-parse --show-toplevel)"
NEO_DISABLE_TOOLS=1 ./neo -c example/neo-claw-example.json5 -d "只回答：SOUL 里默认用哪种语言回答？" 2>&1 | grep -A2 '## Soul'
```

在 stderr 的 system prompt 段中应出现 `## Soul` 及 `example/SOUL.md` 内容（或按 `max_chars` 截断后的片段）。即使因缺少 API Key 返回 HTTP 401，`-d` 仍会先打印完整 system，可用于确认注入。

## 对话示例

```bash
./neo -c example/neo-claw-example.json5 "根据 AGENTS 和 Soul，用两三句话介绍你是谁、在哪个仓库语境下工作"
```

## 文件与配置对应关系

| 本目录文件 | 配置字段 |
|------------|----------|
| `SOUL.md` | `soul.path` |
| `AGENTS.md` | `bootstrap` 列表中的 `path` |
| `RULES.md` | `rules` 列表中的 `path` |
| `MEMORY.md` | `memory.path` |

`skills` 已废弃；示例配置改为加载仓库根 `rules/` 与 `capability_matrix.directory`。本目录无子目录。
