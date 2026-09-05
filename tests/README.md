# tests/

单元测试源码、可执行测试目标，以及 CLI 级冒烟脚本。推荐入口：`make test`（单元 + CLI）、`make test-cli`（仅 CLI）。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `fixtures/` | JSON5 配置、能力包、DAG 包、mock MCP 与辅助可执行文件 |

编写或修改矩阵 / workflow 相关行为后，应保证本目录测试通过，且勿对 `make test` 盲目管道截断以致误判挂起。
