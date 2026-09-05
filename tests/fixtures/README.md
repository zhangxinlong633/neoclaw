# tests/fixtures/

测试夹具集合：最小/组合配置、能力目录包、DAG 目录包、mock MCP 服务脚本，以及供 `argv` / `run_command` 调用的辅助可执行文件。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `bin/` | 测试专用可执行脚本 |
| `cap_pack/` | `capability_matrix.directory` 加载测试包 |
| `dag_pack/` | `workflow_directory` 加载测试包 |

根目录另有大量 `*.json5` 单文件夹具，由各 `tests/test_*.c` 与 `cli_capability_matrix.sh` 引用。
