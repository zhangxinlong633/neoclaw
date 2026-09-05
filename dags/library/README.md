# dags/library/

存放**已启用**的 DAG 定义（一文件一个 workflow 对象）。加载后出现在 planner 的 DAG catalog，并可通过 `neo workflow run <name>` 直接执行。

## 文件约定

必填：`name`、`description`、`steps`。强烈建议补充选型元数据：`when`、`when_not`、`requires`、`outcome`、`tags`（见 [`AGENTS.md`](../../AGENTS.md) §4.1）。

步骤字段中的 `"tools": "on"|"off"` 仅控制该 LLM 步骤是否挂载矩阵工具，**不得**改名为 `capability_matrix`。

本目录无子目录。
