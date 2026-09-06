# 硬切：去掉 workflow 概念，统一为 DAG

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-06 |
| 状态 | 已批准（硬切 A） |
| 范围 | 配置键、CLI、C API/类型、源码目录、用户文档与测试；执行语义不变 |

## 1. 目标与非目标

**目标：** 产品三角表述与代码标识一致——编排层只称 **DAG**，不再出现对外/对内一等 `workflow` 概念。

**非目标：** 不改变 route/retry/loop/plan 行为；不回写全部历史 `docs/superpowers/specs/*`；不做并行/Temporal。

## 2. 命名对照

| 旧 | 新 |
|----|-----|
| JSON `workflows` | `dags` |
| JSON `dag_directory` | `dag_directory` |
| CLI `workflow run` | **删除**（用 `dag run` / `run`） |
| `dag_t` | `dag_t` |
| `dag_run` | `dag_run` |
| `config_find_dag` | `config_find_dag` |
| `dag_directory` 字段 | `dag_directory` |
| `workflows` / `dag_count` | `dags` / `dag_count` |
| `src/dag/` | `src/dag/` |
| `workflow.c` / `workflow_dir.c` | `dag.c` / `dag_dir.c` |
| `docs/dag.md` | `docs/dag.md` |
| `WF_STEP_*` / `WF_MAX_*` | `DAG_STEP_*` / `DAG_MAX_*` |
| `wf_*` 静态函数前缀 | `dag_*` |
| 错误文案 `unknown DAG` | `unknown DAG` |

Planner / materialize 输出顶层键：`"dags"`。

## 3. 硬切行为

- 配置出现顶层 `workflows` 或 `dag_directory`：`config_load` **失败**，stderr 提示改用 `dags` / `dag_directory`，并指向 `docs/migrate-json.md` 新增小节。
- CLI 输入 `workflow`：usage 错误（不再弃用兼容）。
- Plan 抽出 JSON 只接受含 `"dags"` 的对象；旧 `"dags"` 判失败。

## 4. 验收

1. `make test` 全绿；fixture 无旧键。  
2. `./neo dag run …` / `./neo run <图名>` 工作。  
3. `./neo workflow run …` 非零退出。  
4. 含旧键的配置加载失败且信息可读。  
5. AGENTS/CLAUDE/用户 README 术语为 DAG。
