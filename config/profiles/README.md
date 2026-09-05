# config/profiles/

命名 profile 的根目录。执行 `./neo -p <name>` 时，进程会将工作目录切换到 `profiles/<name>/`（并兼容历史上的仓库根 `profiles/<name>/` 布局）。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `<name>/` | 单个 profile：通常含 `neo.json5`，以及可选的 MEMORY、辅助脚本等 |

Profile 内路径相对于该 profile 目录解析。
