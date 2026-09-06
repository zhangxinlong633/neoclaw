# CLI：`dag run` + 统一 `neo run` 设计

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-06 |
| 状态 | 已批准（方案 A） |
| 范围 | 仅 CLI 对外表面 + 文档/测试；配置键与 C API 不改名 |

## 1. 目标与非目标

**目标**

1. 将 `neo workflow run NAME` 对外改名为 **`neo dag run NAME`**，与产品「DAG」术语对齐。  
2. 统一日常执行入口：**`neo run`** 既能跑命名图，也能对提示词做 plan+execute。  
3. 减少用户需要记忆的「neo 后面的子命令」分叉。

**非目标**

- 不重命名配置键 `workflows` / `workflow_directory`，不重命名 C 函数 `workflow_run`。  
- 不删除 `neo plan`、`neo daemon`、`neo "..."`（对话 / tool loop）。  
- 不做 workflow 名的模糊匹配或前缀匹配。  
- 不做 `neo show_time` 这类「裸名当子命令」的全局解析。

## 2. 对外命令面（目标态）

| 命令 | 行为 |
|------|------|
| `neo "…"` | 不变：单次对话 / 反应式 tool loop |
| `neo plan "…"` | 不变：规划并校验；stdout 为 `use` 或 workflows JSON |
| `neo run ARG…` | **分流**：见 §3 |
| `neo dag run NAME` | 显式执行已加载配置/catalog 中的命名 DAG |
| `neo daemon` | 不变 |
| `neo workflow run NAME` | **兼容**：行为同 `dag run`；stderr 弃用提示 |

Help / usage 文案以 `dag run` 与统一 `run` 为主；`workflow run` 标为 deprecated。

## 3. `neo run` 分流规则

解析完全局 options（`-c` / `-p` / `-v` / `--steps` / `-o` 等）后：

1. 要求至少一个非 option 参数；记第一个为 `arg0`，其余按现有 `run` 规则拼进任务串（若走 plan 路径）。  
2. 加载配置（含 `workflow_directory` 物化进 `conf.workflows`）之后：  
   - 若存在 **name 与 `arg0` 精确相等** 的 workflow → 走 **`workflow_run(conf, arg0, …)`**（忽略后续多余参数，或 stderr 警告后忽略；实现选「多余参数则报错」更严——**本期采用：若还有后续非 option 参数则报错**，避免 `neo run show_time now`  silently 丢词）。  
   - 否则 → 现有 **`plan_run(..., execute=1)`**，任务串 = 拼接后的用户任务。  
3. `--steps` / `-o` 仅在 **plan 路径**有意义；若命中命名图仍带了这些 flag：stderr 警告并忽略（不失败），以免破坏脚本。

**撞名规避**

- 任务字符串恰好等于某 catalog 名时会跑图而非 plan。  
- 规避：使用 `neo dag run NAME` 强制跑图；或把任务写成更长句子（不精确等于图名）。

## 4. `neo dag run`

```
neo [OPTIONS] dag run NAME
```

- `NAME` 必填；未知名 → 非零退出（同今日 `workflow run`）。  
- 不经过 planner。  
- `-v` 传入 `workflow_run` 的 verbose。

## 5. 兼容与迁移

| 旧 | 新 | 兼容策略 |
|----|----|----------|
| `neo workflow run NAME` | `neo dag run NAME` 或 `neo run NAME` | 保留解析；stderr：`neo: 'workflow run' is deprecated; use 'dag run' or 'run'` |
| 文档 / README / examples | 全部改为 `dag run` / 说明统一 `run` | 同一切片更新 |
| CLI 冒烟脚本里的 `workflow run` | 改为 `dag run`；另留一条测弃用路径可选 | 见 §6 |

配置与内部标识符本切片 **零改名**，避免牵动 JSON5 / plan materialize / 能力矩阵。

## 6. 测试

- `tests/cli_capability_matrix.sh`（或等价 CLI 冒烟）：  
  - `neo dag run …` 成功路径（如现有 `cli_count` / `show_time` 类）。  
  - `neo run <已知图名>` 走直接跑图（可用 count fixture 断言输出，无需 LLM）。  
  - `neo workflow run …` 仍成功且 stderr 含 deprecated（可选断言）。  
- 单元侧：若分流逻辑抽成小函数，可在纯 C 测试里对「名命中 / 未命中」做表驱动；否则以 CLI 冒烟为主即可（YAGNI：优先改 `main.c` + 冒烟）。

`neo run "自然语言"` 的 plan 路径依赖 LLM，本切片不新增强制在线用例；保持现有 plan 相关测试不变。

## 7. 文档触点

- `src/cli/main.c` usage  
- `docs/examples.md`、`docs/workflow.md`（若写到 CLI）  
- `README.md` / `README_zh.md`  
- 相关 profile README（如 `config/profiles/demo`）  
- 不强制回写历史 `docs/superpowers/specs/*` 旧设计里的命令字符串（可只改用户文档）

## 8. 实现落点

| 文件 | 改动 |
|------|------|
| `src/cli/main.c` | 解析 `dag run`；`workflow run` 弃用；`run` 分流 |
| `tests/cli_capability_matrix.sh` 等 | 命令字符串 + 可选弃用断言 |
| 用户文档 / README | 示例更新 |

无需改 `src/workflow/workflow.c` 的执行语义。

## 9. 验收

1. `./neo dag run <已知名>` 与改名前 `workflow run` 行为一致。  
2. `./neo run <已知名>` 不调用 planner，直接出图结果。  
3. `./neo run "与任何图名都不等的任务"` 仍走 plan+execute（人工或现有流程验证）。  
4. `./neo workflow run <名>` 仍可用 + 弃用提示。  
5. `make test` 通过。
