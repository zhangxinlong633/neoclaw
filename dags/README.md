# dags/

本目录为确定性 **DAG（workflow）** 的文件化库：一图对应一个 JSON5 文件，由顶层配置 `workflow_directory` 指向本目录后加载。

Planner（`neo plan` / `neo run`）会生成 catalog（含 `when` / `when_not` / `requires` / `outcome` 等），并优先输出 `{"use":["name"]}` 选型；无合适条目时再现编 `{"workflows":[...]}`。权威说明见 [`docs/workflow.md`](../docs/workflow.md) 与 [`AGENTS.md`](../AGENTS.md) §4.1。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `library/` | 按 `manifest.load` 加载的已启用 workflow |
| `proposed/` | 预留草稿区；当前版本默认不加载 |

根目录 `manifest.json5` 声明 `load` 与 `proposed`。
