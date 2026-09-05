# capabilities/

本目录为 **Capability Matrix** 的文件化能力包：一能力对应一个 JSON5 文件，由配置项 `capability_matrix.directory` 指向本目录后，在启动时按 `manifest.json5` 合并进矩阵。

权威说明见 [`docs/tool.md`](../docs/tool.md) 与 [`AGENTS.md`](../AGENTS.md) §4、§4.1。

## 职责边界

- 定义可执行的命令能力（`name`、`argv`、`parameters` 及选型元数据）。
- 不替代 builtin 路径工具，也不构成与矩阵并行的第二套名字空间。
- `proposed/` 中的草稿默认不加载；审核并移入加载子目录后，需重启 `neo` 方生效。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `commands/` | 按 `manifest.load` 加载的已启用能力定义 |
| `proposed/` | `propose_capability` 写入的草稿；默认不进入矩阵 |

根目录 `manifest.json5` 声明 `load` 子目录列表与 `proposed` 子路径。
