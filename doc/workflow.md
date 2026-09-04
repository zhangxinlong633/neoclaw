# Neo 确定性 DAG Workflow

编排路径主张：**拓扑在 YAML 里声明，LLM 只当 Worker**（不参与「下一步走哪」）。  
自然语言 `./neo "..."` 仍可自由对话；`neo workflow run` 走确定性图。

不引入 Temporal / Airflow；引擎就是 `neo` 进程内的小 DAG runner。

## 节点类型

| type | 角色 |
|------|------|
| `tool` | 确定性 / 本地命令算子 |
| `llm` | LLM Worker（`tools: on\|off`） |
| `loop` | 按 `over`×`max` 重复（兼容旧写法） |
| `route` | **确定性**路由：`on` 展开后是否包含 `match` → `then` / `else` |

## DAG：`depends_on`

存在任一 `depends_on` 或 `type: route` 时进入 **DAG 模式**（拓扑排序执行）。  
否则保持旧的线性 + loop 行为。

```yaml
workflows:
  - name: diamond
    steps:
      - id: a
        type: tool
        tool: count_run
      - id: b
        type: tool
        tool: count_run
        depends_on: ["a"]
      - id: c
        type: tool
        tool: count_run
        depends_on: ["a"]
      - id: d
        type: tool
        tool: count_run
        depends_on: ["b", "c"]
```

就绪节点按配置顺序**串行**执行（轻量；依赖正确即可）。

## 确定性 route

```yaml
      - id: branch
        type: route
        depends_on: ["classify"]
        on: "{{steps.classify}}"
        match: "pdf"
        then: ["as_pdf"]
        else: ["as_text"]
```

未选中分支及其依赖它们的下游会 `skip`（stderr：`neo dag: ... skip ...`）。

## 运行

```bash
./neo workflow run diamond
./neo -p demo workflow run demo_loop
./scripts/neo-ask --workflow diamond
```

## cron

```text
0 * * * * cd /path/to/neoclaw && ./scripts/neo-ask -p demo --workflow demo_loop >>/tmp/neo-cron.log 2>&1
```
