# tests/fixtures/cap_pack/

用于验证 `capability_matrix.directory` 加载路径的能力包。夹具使用扁平 `commands/`（无场景分层），以便单测简单；**生产示例包**见仓库根 [`capabilities/README.md`](../../capabilities/README.md)（`local/`、`git/` 等直接挂在包根下）。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `commands/` | 启动时加载的测试能力 |
| `proposed/` | `propose_capability` 测试写出目录 |
