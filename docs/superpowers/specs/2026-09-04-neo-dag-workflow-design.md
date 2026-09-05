# Neo-scale 确定性 DAG Workflow 设计

**日期：** 2026-09-04  
**状态：** 已批准（定位文档 + workflow→DAG 实现）  
**原则对齐：** 确定性拓扑骨架 + LLM 仅作 Worker；单二进制、无 Temporal/Airflow。

---

## 1. 定位调整（文档）

Neo 编排路径主张：

- **DAG 拓扑由配置声明**，模型不参与「下一步走哪」的流程控制。
- **LLM / tool 节点**是图中的算子：输入输出边界清晰。
- **自然语言 `./neo "..."`** 仍可自由对话；**`neo workflow run`** 走确定性图。

明确不做：Temporal、通用 hooks、自由 shell、生产级 OTel 全家桶。

---

## 2. DAG 模型

### 2.1 节点字段（在现有 step 上扩展）

| 字段 | 说明 |
|------|------|
| `id` / `type` | 同现有：`tool` / `llm` / `loop`；新增 `route` |
| `depends_on` | 字符串 id 列表（单行 `["a","b"]`）；缺省 = 无依赖（根） |
| `route` 专用 | `on`（模板字符串）、`match`（子串匹配）、`then` / `else`（要调度的节点 id 列表） |

### 2.2 执行语义

1. **兼容模式**：工作流内**没有任何** `depends_on` / `route` → 沿用旧 runner（顺序 + loop 引用跳过启发式）。
2. **DAG 模式**：存在任一 `depends_on` 或 `type: route`：
   - Kahn 拓扑排序；环报错。
   - 就绪节点按配置声明顺序串行执行（轻量：不真并行，但依赖正确）。
   - `loop`：执行时按 `over`×`max` 跑子节点（子节点须已在图中定义）。
   - `route`：展开 `on`，若包含 `match` 则只把 `then` 中节点标为可运行分支；否则 `else`。未入选分支及其仅依赖它们的下游**跳过**。
3. **输出**：最后一个**实际执行**的非 route 节点文本 → stdout。

### 2.3 示例

```yaml
workflows:
  - name: extract_pipeline
    steps:
      - id: fetch
        type: tool
        tool: echo_args
        args: {"src":"x"}
      - id: classify
        type: llm
        depends_on: ["fetch"]
        prompt: "只回答 pdf 或 text：\n{{steps.fetch}}"
        tools: off
      - id: branch
        type: route
        depends_on: ["classify"]
        on: "{{steps.classify}}"
        match: "pdf"
        then: ["as_pdf"]
        else: ["as_text"]
      - id: as_pdf
        type: tool
        tool: echo_args
        depends_on: ["branch"]
        args: {"kind":"pdf"}
      - id: as_text
        type: tool
        tool: echo_args
        depends_on: ["branch"]
        args: {"kind":"text"}
```

---

## 3. 可观测性（轻量）

- 每个执行节点 stderr：`neo dag: <wf> run <id> (type=...)`
- 跳过：`neo dag: <wf> skip <id>`
- 不做 OpenTelemetry。

---

## 4. 测试

- 解析带 `depends_on` 的 yaml
- 菱形依赖：A→B,A→C,B+C→D（用 count 脚本验证 B、C、D 各跑一次）
- 无 `depends_on` 的旧 `demo_loop` 仍通过
- route：match 命中只跑 then 侧

---

## 5. 文件

- `src/config.h` / `config.c` — 字段与解析、校验（环、未知 id）
- `src/workflow.c` — DAG runner
- `docs/workflow.md` / `README.md` — 定位与用法
- `tests/test_workflow_dag.c` 等
