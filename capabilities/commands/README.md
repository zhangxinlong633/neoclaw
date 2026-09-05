# capabilities/commands/

存放**已启用**的命令能力定义（一文件一能力）。Neo 启动时依据上级 `manifest.json5` 的 `load` 列表扫描本目录中的 `*.json5` / `*.json`，校验后并入 Capability Matrix。

## 文件约定

字段与内联配置 `capability_matrix.commands[]` 一致，至少包括 `name`、`description`、`argv`；建议补充 `when`、`when_not`、`tags`、`outcome`（见 [`AGENTS.md`](../../AGENTS.md) §4.1）。

`argv[0]` 须为相对 `capability_matrix.root` 的路径，或显式绝对路径（禁止 `..`；不查 PATH 裸名）。参数传递由 `pass_args`（`stdin_json` 或 `env`）决定。选型元数据见 [`AGENTS.md`](../../AGENTS.md) §4.1。

本目录无子目录。
