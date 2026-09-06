# DAG 近端演进（D1 可观测 + D4 路线图分层）

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-06 |
| 状态 | 实行中（延续此前推荐的 D1+D4） |
| 非目标 | 图级自动重试引擎、并行调度、行业/边缘交付、计算漂移 |

## 目标

1. **D1**：`-v` 下 DAG 步骤日志可读（类型、能力名、成功/失败/跳过）；失败时即使无 `-v` 也打印失败步骤摘要。  
2. **D4**：在 `architecture.md` / `applications.md` / `workflow.md` 把「近端基础层」与「远期行业/愿景」分开写清，避免把 edge/OS 写成现行承诺。

## D1 行为

| 场景 | stderr |
|------|--------|
| `-v` 步骤开始 | `neo: step start workflow=W id=I type=tool tool=NAME` 或 `type=llm tools=off` |
| `-v` 步骤结束 | `neo: step end … status=ok\|skip`（含 type 信息） |
| 任一步失败 | **始终** `neo: step end … status=fail`（含 type/tool）；随后现有错误行可保留 |
| 整图结束 | 保持 `neo: run done workflow=W status=ok` |

不新增配置键；不改 stdout 契约。

## D4 文档

- `architecture.md` §8：增加「近端演进」表（可观测、catalog SOP、系统 DNS/HTTPS 等已交付或本切片）与「远期」对照。  
- `applications.md` §6：补充近端 DAG 能力指针。  
- `workflow.md`：catalog 列入 `workspace_brief`；说明 `-v` 可观测字段。

## 验证

`make test`；可选 `./neo -v workflow run show_time` 人工看 stderr。
