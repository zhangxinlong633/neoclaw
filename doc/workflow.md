# Neo 声明式 Workflow

在 `config/config.yaml`（或 profile 的 `neo.yaml`）里写短流程，用 `tool` / `llm` / `loop` 做简单循环。

## 配置示例

```yaml
tools:
  enabled: true
  root: "."
  commands:
    - name: echo_args
      argv: ["./scripts/tools/echo-args.sh"]

workflows:
  - name: demo_loop
    steps:
      - id: fetch
        type: tool
        tool: echo_args
        args: {"ping": 1}
      - id: summarize
        type: llm
        prompt: "用一句中文总结：\n{{prev}}"
        tools: off
      - id: again
        type: loop
        over: ["fetch", "summarize"]
        max: 2
        until: always
```

模板：`{{prev}}`、`{{steps.<id>}}`。被 `loop.over` 引用的 step 只作定义，由 loop 按 `max` 执行。

## 运行

```bash
./neo workflow run demo_loop
./neo -p demo workflow run demo_loop
./scripts/neo-ask -p demo --workflow demo_loop
```

## cron 样例

```text
0 * * * * cd /path/to/neoclaw && ./scripts/neo-ask -p demo --workflow demo_loop >>/tmp/neo-cron.log 2>&1
```
