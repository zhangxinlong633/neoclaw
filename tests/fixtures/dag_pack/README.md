# tests/fixtures/dag_pack/

用于验证 `workflow_directory` 加载路径的 DAG 包。夹具使用扁平 `library/`；**生产示例包**见仓库根 [`dags/README.md`](../../dags/README.md)（`baseline/`、`workspace/` 等直接挂在包根下）。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `library/` | 启动时加载的测试 workflow |
