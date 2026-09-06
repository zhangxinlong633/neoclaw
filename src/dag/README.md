# src/dag/

确定性 **DAG** 执行器、`dag_directory` 加载，以及 `neo plan` / `neo run` 的规划提示、JSON 抽取与 materialize。

## 主要源文件

| 文件 | 职责 |
|------|------|
| `dag.*` | DAG / 线性 / loop / route 执行 |
| `dag_dir.*` | 目录加载与 catalog listing |
| `plan.*` | 规划提示、`use[]` 选型与现编 |

用户文档：[`docs/dag.md`](../../docs/dag.md)。本目录无子目录。
