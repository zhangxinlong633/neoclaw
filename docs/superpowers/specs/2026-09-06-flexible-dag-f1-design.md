# Flexible DAG（F1/F2/F3）设计

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-06 |
| 状态 | 已批准方向；本文件含分期；**本期实现范围 = 期 1（F1）** |
| 调度边界 | 近端坚持确定性路由（选项 C）；LLM `decide` 选支为远期 |

## 1. 目标与非目标

**目标**：在保持「LLM 不当调度器、不做 Temporal」的前提下，提高 DAG 表达力。

| 期 | 主题 | 本期？ |
|----|------|--------|
| **1** | F1 控制流：多路 `route` + 可选 tool 步 `retry` | **是** |
| 2 | F2 数据流：`{{steps.id.field}}` 轻量字段 | 否（下期） |
| 3 | F3 规划：`{"use","with"}` 参数化 catalog | 否（下期） |

**非目标（全程近端）**：并行调度、图级重试策略引擎、子图 include、LLM 选边、计算漂移。

## 2. 期 1 — F1 控制流

### 2.1 多路 route（`cases`）

在现有 `type: route` 上扩展。**若存在非空 `cases` 数组，则忽略顶层 `match`/`then`/`else` 的分支语义**（仍可读 `on`）；否则保持旧行为（单一 `match` + `then`/`else`）。

```json5
{
  id: "branch",
  type: "route",
  depends_on: ["classify"],
  on: "{{steps.classify}}",
  cases: [
    { match: "pdf", then: ["as_pdf"] },
    { match: "docx", then: ["as_docx"] },
    { then: ["as_other"] }, // 无 match = 默认支（至多一条）
  ],
}
```

**匹配规则**：

1. 展开 `on` 模板得字符串 `S`。  
2. 按 `cases` **顺序**扫描：若条目含非空 `match` 且 `S` 含该子串 → 选中该支，停止。  
3. 若无命中：使用唯一无 `match`（或 `match` 为空）的默认支；若无默认支 → 视为选中空 `then`（仅 skip 所有带 `match` 的支）。  
4. **Skip**：凡未选中支的 `then[]` 中的 step id 一律 `skip`（同今日对 `then`/`else` 的处理）。

**校验**：

- `on` 仍必填。  
- `cases` 若出现：长度 ≥ 1，且 ≤ 16；每个 `then` 中的 id 必须存在；默认支至多 1 条。  
- 无 `cases` 时：沿用旧校验（`then`/`else` 至少一个非空等）。

**兼容**：现有 `route_demo` / `route_merge` fixture 与文档示例不得破坏。

### 2.2 Tool 步有限重试

仅 `type: tool`：

```json5
{ id: "t", type: "tool", tool: "count_run", retry: { max: 2 }, args: {} }
```

- `retry.max`：首次失败后的**额外**尝试次数；合法范围 **0..3**（缺省 0）。  
- 总尝试次数 = `1 + retry.max`。  
- 任一次成功即继续；耗尽仍失败 → 同今日失败路径（含 `status=fail` 日志）。  
- `type: llm|loop|route` 上出现 `retry` → 配置校验报错。

### 2.3 可观测

`-v` 下 route 命中可增加 `case=N` 或 `hit=default`（可选增强）；失败 retry 可在 verbose 打 `attempt=i/n`（建议有）。

## 3. 期 2 / 3（仅规格占位，不实现）

- **F2**：步骤输出若为 JSON 对象，支持 `{{steps.id.field}}`；找不到则空串。  
- **F3**：`{"use":["dag"],"with":{...}}` 覆盖 catalog 默认 args；校验未知键。  
- **远期**：`type: decide`（LLM 从候选 id 选择）。

## 4. 实现落点

| 模块 | 改动 |
|------|------|
| `src/core/config.h` | `wf_route_case_t`；`route_cases` / `route_case_count`；`retry_max` |
| `src/core/config.c` | 解析 `cases` / `retry`；校验；free |
| `src/workflow/workflow.c` | route 选支；tool 重试循环 |
| `tests/fixtures/workflow_dag.json5` | 新图 `route_cases`、`tool_retry` |
| `tests/test_workflow_dag.c` | 覆盖新图 |
| `docs/workflow.md`、architecture §8.1 | 文档 |

## 5. 验证

`make test`；手工可选：含 `cases` 的 catalog 样例（可放 `dags/baseline/` 或仅 fixture）。
