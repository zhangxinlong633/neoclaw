# dags/baseline/

对应 [`docs/applications.md`](../../docs/applications.md) **基础层：复杂任务的稳定执行**——用冻结 DAG 替代纯 ReAct 长程漫游。

| 文件 | 图名 | 说明 |
|------|------|------|
| `show_time.json5` | `show_time` | `date_iso` → 打印 UTC ISO 时间 |
| `repo_pulse.json5` | `repo_pulse` | `git_status_short` → `git_log_five` → LLM 一段话汇总 |

依赖能力须已在矩阵中（`date_iso` 来自 `capabilities/local/`；git 能力来自 `capabilities/git/`）。本目录无子目录。
