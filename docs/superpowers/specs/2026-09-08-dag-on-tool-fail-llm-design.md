# DAG tool 失败自动 LLM 热线（方案 1）

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-08 |
| 状态 | 已批准（方案 B → 落地法 1：诊断后重试） |
| 范围 | 配置 + `dag_run_tool_step` 钩子 + 测试 + 用户文档；不改 args、不跳步 |

## 1. 目标与非目标

**目标（业务）**：办事流程某工具步失败时，可自动问一次模型「还要不要再试」；同意再试则再跑该步，否则停图。图本身不用为每个失败点手画 LLM 步。

**非目标**

- LLM 改写 tool args / 换工具名 / 指定下一 step id（方案 2/3）
- llm / route 步失败套娃热线
- 热线内 `tools: on`
- 默认开启（Policy：默认关）

## 2. 配置

顶层对象 `dag`（与 `dags` 数组、`dag_directory` 并列）：

```json5
dag: {
  on_tool_fail: {
    llm: false,     // 默认 false
    max_calls: 1,   // 每步最多问 LLM 几次；钳制 0..2；0 等同关闭
  },
},
```

C：

```c
typedef struct {
  int on_tool_fail_llm;       /* 0/1 */
  int on_tool_fail_max_calls; /* 0..2，默认 1；仅当 llm=1 时有意义 */
} dag_runtime_config_t;

/* 挂在 agent_config_t.dag_runtime */
```

缺省：`llm=0`，`max_calls=1`。

## 3. 运行时语义

在 `dag_run_tool_step` 内，**先**走现有本地 `retry.max` 循环。

若本地尝试全部失败，且 `on_tool_fail_llm && on_tool_fail_max_calls > 0`：

1. `llm_call = 0`
2. while `llm_call < max_calls`：
   - 用 `llm_chat`（**无 tools**）提问；system 固定要求最终一行只含 `RETRY` 或 `ABORT`。
   - user 含：dag 名、step id、tool 名、最近错误输出（截断）、简短 prior 上下文。
   - 解析回复：见 §4。
   - `ABORT` 或不清 → 返回失败。
   - `RETRY` → 再执行 **一次** 该 tool（同一已展开 args）；成功则步成功；失败则 `llm_call++` 继续循环。
3. 耗尽仍失败 → 返回失败。

Verbose：stderr 打 `on_fail_llm call=i/n action=RETRY|ABORT`。

## 4. 解析规则

对模型回复全文（大小写不敏感）：

1. 若匹配词边界 `ABORT`（或单独一行 `ABORT`）→ ABORT  
2. 否则若匹配 `RETRY` → RETRY  
3. 否则 → ABORT（保守）

LLM 请求本身失败 → 视为 ABORT（不静默重试）。

## 5. 测试

- 配置解析：`dag.on_tool_fail.llm` / `max_calls` 钳制  
- `dag_parse_fail_llm_reply` 表驱动  
- 默认关闭时现有 `test_dag_*` / CLI 冒烟不变  
- （可选）llm=true + 必失败 tool + 不可达 model → 图失败且 stderr 含热线痕迹

## 6. 文档

`docs/dag.md`、`docs/architecture.md` §8.1 一行；`AGENTS` 术语仍写「近端 LLM 不当调度器」——热线只做 RETRY/ABORT 闸门。

## 7. 验收

1. 默认配置行为与今日一致。  
2. 打开 `dag.on_tool_fail.llm` 后，本地 retry 耗尽会调 LLM；`RETRY` 可再跑 tool。  
3. `make test` 通过。
