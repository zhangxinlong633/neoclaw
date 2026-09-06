# Baseline A/B/C 强化设计

| 属性 | 内容 |
|------|------|
| 文档版本 | 1.0 |
| 日期 | 2026-09-06 |
| 状态 | 已批准（方案 1：薄垂直切片） |
| 关联 | [`docs/applications.md`](../../applications.md) 基础层；根 README「Ship today」 |

## 1. 目标

在**不扩行业/未来层**的前提下，让基础层「可摸到」：

| 代号 | 主题 | 成功标准 |
|------|------|----------|
| **A** | 工作区旗舰 DAG | 一条命令跑通「列目录 → LLM 整理 → 落盘」，与 README 叙事对齐 |
| **B** | plan/run 选型可靠 | 能力名误入 `use` 有单测覆盖；混用/未知名 stderr 更可读；catalog 与能力名更易区分 |
| **C** | 开箱组合说明 | 一张表写清默认 load、关键 DAG、policy；目录 README 与 example 配置互链 |

## 2. 非目标

- 新建 `demo/` / `industry` 场景包或改默认 `manifest.load`
- 计算漂移引擎、边缘自治、Agent-as-OS
- 全量 `docs/*.md` 英文化
- 默认打开 `shell_enabled`

## 3. A — `workspace_brief`

**路径**：`dags/workspace/workspace_brief.json5`（已在 `dags/manifest.json5` 的 `load: ["baseline","workspace"]` 内）。

**拓扑**：

1. `ls` — `type: tool`，`list_dir`，`args: { path: "." }`
2. `brief` — `type: llm`，`tools: "off"`，根据 `{{steps.ls}}` 产出：顶层结构要点 + 至多 3 条可执行观察（用户语言）
3. `save` — `type: tool`，`append_file`，追加到 `WORKSPACE_BRIEF.md`（与 `MEMORY.md` 分离，避免污染 claw 记忆）

**元数据（选型）**：`description` / `when` / `when_not` / `tags` / `requires: ["list_dir","append_file"]` / `outcome` 必填且足以让 planner 只靠元数据选型。

**文档**：`docs/examples.md`、根 `README.md` / `README_zh.md` 增加 `./neo workflow run workspace_brief`。

**落盘约定**：重复执行会多次 append；`when_not` 写明「不要用本图做 MEMORY 长期记忆」。

## 4. B — plan / run

**已有行为保留**：`use` 全为能力名 → 合成 `adhoc_tools` 工作流；混用 → 失败；未知名 → 失败。

**改动**：

1. 将 `plan_workflows_json_for_tools` 提升为可测 API（`plan.h` 声明），或抽出 `plan_resolve_use_selection` 供单测直接调用（优先最小暴露）。
2. `tests/test_plan_extract.c` 增补：合成 JSON 含 `adhoc_tools` 与 tool 名；prompt 中强调 catalog 名 ≠ capability 名（若文案调整）。
3. 未知名 stderr：说明「既非 catalog DAG 也非矩阵能力」；混用信息保持明确。
4. Catalog listing：每条名称行加醒目前缀（如 `DAG:`），降低与能力名单混淆（改动 `workflow_dir_catalog_listing`，并更新依赖该字符串的测试若有）。

## 5. C — 开箱组合

**主表位置**：`docs/examples.md` 新增一节「开箱组合（默认包）」（不另起长文，避免文档碎片）。

表列至少包含：

| 场景 | capabilities `load` | dags `load` | 推荐试跑 | Policy 注意 |
|------|---------------------|-------------|----------|-------------|
| 默认旁路助手 | local + git + unix（白名单） | baseline + workspace | `workspace_brief` / `repo_pulse` / `show_time` | `shell_enabled: false` |

**互链**：`capabilities/README.md`、`dags/README.md`、`config/config.json5.example` 注释指向该节；`docs/README.md` 阅读顺序可一句带过。

## 6. 验证

- `make test` 通过（含扩展后的 `test_plan_extract`）
- 有密钥时可手工：`./neo workflow run workspace_brief`（生成/追加 `WORKSPACE_BRIEF.md`）
- 文档链接无死链

## 7. 风险与约束

- `append_file` / `list_dir` 须为 builtin 且默认矩阵启用
- `WORKSPACE_BRIEF.md` 可能进入工作区；若需可后续 gitignore（本切片不强制）
- 遵守 AGENTS：顶层键 `capability_matrix`；步骤字段仍为 `"tools"`；复杂 C 逻辑补中文注释
