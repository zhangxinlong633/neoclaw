# dags/workspace/

工作区旁路助手 DAG：面向单机仓库内的读览、定位与短记忆写入，体现「本机执行 + 可选 LLM 整理」的组合。

| 文件 | 图名 | 主要能力 |
|------|------|----------|
| `inspect_path.json5` | `inspect_path` | `stat` + `read_file` + LLM |
| `list_overview.json5` | `list_overview` | `list_dir` + LLM |
| `search_context.json5` | `search_context` | `grep` + LLM |
| `append_memo.json5` | `append_memo` | LLM 起草 + `append_file` |

本目录无子目录。深度代码编辑 / IDE 级 apply_patch 非目标。
