# Neo claw 示例（`example/`）

本目录演示 Neo **确实会读取** `soul` / `bootstrap` / `rules` / `memory` 所指向的 Markdown，并拼进 **system prompt**（与 `docs/claw.md` 描述一致）。

## 前提

1. 已在仓库根 **`make`** 生成 `./neo`。
2. 编辑 **`example/neo-claw-example.json5`**，将 `model.api_key` 换成你的 key（或依赖环境变量 **`NEO_API_KEY`**，见主 README）。
3. **必须在 neoclaw 仓库根目录执行**下面的命令（路径 `example/SOUL.md` 等是相对 cwd 解析的）。

## 一条命令验证 Soul 已进入上下文

关闭工具可减少请求体积（可选）：

```bash
cd "$(git rev-parse --show-toplevel)"
NEO_DISABLE_TOOLS=1 ./neo -c example/neo-claw-example.json5 -d "只回答：SOUL 里默认用哪种语言回答？" 2>&1 | grep -A2 '## Soul'
```

在 stderr 的 **system prompt** 段中应能看到 **`## Soul`** 以及 `example/SOUL.md` 的全文（或截断片段）。  
即使 API Key 未配置导致 **HTTP 401**，`-d` 仍会先打印完整 system，可用来确认 **Soul / Bootstrap / Rules / Workspace** 已注入。

## 普通对话示例

```bash
./neo -c example/neo-claw-example.json5 "根据 AGENTS 和 Soul，用两三句话介绍你是谁、在哪个仓库语境下工作"
```

## 文件与配置对应关系

| 本目录文件        | YAML 字段 |
|-------------------|-----------|
| `SOUL.md`         | `soul.path` |
| `AGENTS.md`       | `bootstrap` 下 `- path` |
| `RULES.md`        | `rules` 下 `- path` |
| `MEMORY.md`       | `memory.path` |

`skills` 仍使用仓库根的 **`skills/`** 目录；若只想看 claw 段，可用 `-d` 检查完整 system。
