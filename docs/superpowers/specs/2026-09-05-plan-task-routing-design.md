# Planner task-type routing (short Q&A vs R&D pipeline)

Date: 2026-09-05  
Status: approved (approach A, soft default remains 10)

## Goal

Stop `neo plan` / `neo run` from turning **knowledge / Q&A** tasks into long fake R&D DAGs with `write_file` / implement steps. Knowledge uses a **~4-step editorial pipeline**; keep the **~10-step team pipeline** for **engineering** tasks.

## Decisions

| Topic | Choice |
|-------|--------|
| Mechanism | **Prompt-only** (no C keyword heuristics) |
| Soft default `target_steps` | **Still 10** (`--steps` / `plan.target_steps` override) |
| Knowledge tasks | Prefer **~4** llm editorial steps (understand → draft → self-check → summarize), `tools: "off"`; no file write / fake implement. `--steps 1` → single step |
| Engineering tasks | Existing team pipeline scaled to soft N |
| Final output | Last step answers the **user’s original question** in their language |

## Prompt rules (normative)

1. Classify the user task as **knowledge** vs **engineering**.
2. Knowledge: explain / compare / define / Q&A / “说明一下” — short DAG; forbid inventing coding work.
3. Engineering: implement / fix / write files / scripts / multi-tool pipelines — team roles ~N steps.
4. Soft N is a **ceiling preference for engineering**, not a mandate to pad knowledge tasks to N.
5. Remove prior rule: “Even for Q&A, use a scaled-down team flow…”.

## Docs

- `docs/workflow.md` / README: document dual mode (short Q&A vs team pipeline).
- Supersedes the “default for all plan/run” wording in `2026-09-05-team-pipeline-design.md` for knowledge tasks.

## Non-goals

- Runtime task classifier in C
- Changing `PLAN_DEFAULT_TARGET_STEPS`
- Hard validation of step count after plan
