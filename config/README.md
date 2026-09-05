# config/

存放 Neo 的运行配置模板、本地配置（常被 gitignore）以及命名 **profile**。

默认优先路径为 `config/config.json5`；示例见 `config.json5.example`。配置格式为 **JSON5 only**（见 [`docs/migrate-json.md`](../docs/migrate-json.md)）。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `profiles/` | 命名 profile 根；`./neo -p NAME` 会切换至对应子目录 |

顶层键优先使用 `capability_matrix`；旧键 `tools` 仅兼容读取。详见 [`docs/tool.md`](../docs/tool.md)。
