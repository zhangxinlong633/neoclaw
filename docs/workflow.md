# Neo 确定性 DAG Workflow

编排路径主张：**拓扑在配置（JSON5）里声明，LLM 只当 Worker**（不参与「下一步走哪」）。  
自然语言 `./neo "..."` 仍可自由对话；`neo workflow run` 走确定性图。

不引入 Temporal / Airflow；引擎就是 `neo` 进程内的小 DAG runner。  
配置迁移见 [migrate-json.md](migrate-json.md)。

## 节点类型

| type | 角色 |
|------|------|
| `tool` | 确定性 / 本地命令算子 |
| `llm` | LLM Worker（步骤字段 `"tools": "on"|"off"`） |
| `loop` | 按 `over`×`max` 重复（兼容旧写法） |
| `route` | **确定性**路由：`on` 展开后是否包含 `match` → `then` / `else` |

## DAG：`depends_on`

存在任一 `depends_on` 或 `type: route` 时进入 **DAG 模式**（拓扑排序执行）。  
否则保持旧的线性 + loop 行为。

```json5
{
  workflows: [
    {
      name: "diamond",
      steps: [
        { id: "a", type: "tool", tool: "count_run" },
        { id: "b", type: "tool", tool: "count_run", depends_on: ["a"] },
        { id: "c", type: "tool", tool: "count_run", depends_on: ["a"] },
        { id: "d", type: "tool", tool: "count_run", depends_on: ["b", "c"] },
      ],
    },
  ],
}
```

就绪节点按配置顺序**串行**执行（轻量；依赖正确即可）。

## 确定性 route

```json5
{
  id: "branch",
  type: "route",
  depends_on: ["classify"],
  on: "{{steps.classify}}",
  match: "pdf",
  then: ["as_pdf"],
  else: ["as_text"],
}
```

未选中分支会 `skip`；**仅当某步的全部 `depends_on` 都被 skip 时**，该步才连带 skip（route 之后的汇合/总结步在另一侧分支被 skip 时仍可执行）。

## 运行

```bash
./neo workflow run diamond
./neo -p demo workflow run demo_loop
./neo -v workflow run diamond   # stderr 步骤摘要
./scripts/neo-ask --workflow diamond
```

## Plan then Run

| 命令 | 行为 |
|------|------|
| `neo run "task"` | 规划 → 校验 → **执行**（stdout 只有执行结果） |
| `neo plan "task"` | 规划 → 校验（stdout 为 workflows **JSON**） |
| `neo workflow run NAME` | 跑配置里已声明的图 |

由 LLM **一次性**生成冻结的 `workflows` DAG；执行期不重规划。

### 默认：研发团队式流水线

`plan` / `run` 默认按约 **10** 个角色步规划（可用 `--steps` 缩放，保留阶段顺序）：

1. 理解业务需求  
2–3. 分解 / 设计  
4–7. 实现  
8–9. 验证（失败可用 `route`/`loop` **最多回跳 2 次**到实现或分解）  
10. 总结输出  

验证信号建议用子串如 `PASS` / `FAIL` 供 `route` 匹配。不整图重 plan。

软目标步数：

```bash
./neo run --steps 12 "把任务拆成多步流水线"
./neo plan --steps 3 "尽量短的团队 DAG"
```

配置可选：

```json5
{ plan: { target_steps: 10 } }
```

优先级：`--steps` > `plan.target_steps` > 默认 10（上限 32）。

```bash
./neo run "用 echo_args 打出 hello，再让 llm 总结一句"
./neo plan -o /tmp/planned.json "列出当前目录再写一句说明"
./neo run -o /tmp/planned.json "调用 allowlist 里的脚本做两步流水线"
```

Planner 只能引用配置里已声明的 `tools.commands` 以及 builtin `read_file` / `write_file` / `list_dir`。

（旧写法 `neo plan --run` 已移除，请改用 `neo run`。）

## cron

```text
0 * * * * cd /path/to/neoclaw && ./scripts/neo-ask -p demo --workflow demo_loop >>/tmp/neo-cron.log 2>&1
```
