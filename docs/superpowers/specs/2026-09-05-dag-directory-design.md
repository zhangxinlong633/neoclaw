# DAG Directory Design (plan A — select preferred)

Date: 2026-09-05  
Status: approved

## Goal

Mirror Capability Directory for workflows: **one DAG per file**, manifest-organized load, and planner that **prefers selecting** catalog entries via `{"use":["name"]}` over inventing steps.

## Layout

```
dags/
  manifest.json5          # load: ["library"], proposed: "proposed" (proposed not loaded)
  library/*.json5         # one workflow object per file
```

## Config

```json5
workflow_directory: "dags",
workflows: [ /* optional inline */ ],
```

## Planner

- System prompt lists catalog (`name` + `description` + optional `when`).
- Prefer output: `{"use":["summarize_qa"]}` (string or array).
- If `use` resolves to loaded workflows → validate names and run/print without inventing.
- Else fall back to existing full `{"workflows":[...]}` invent path.
