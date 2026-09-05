# capabilities/git/

仓库探针类命令能力：向模型与 DAG 暴露受控的 git 只读查询，对应 [`docs/applications.md`](../../docs/applications.md) 基础层中的**工具调用工程化**示范（白名单 argv，而非任意 shell）。

| 文件 | 能力名 | 说明 |
|------|--------|------|
| `git_status_short.json5` | `git_status_short` | `git status -sb` |
| `git_log_five.json5` | `git_log_five` | 近 5 条 oneline log |

本目录无子目录。完整 diff / blame 非目标；勿在此扩展为通用 shell。
