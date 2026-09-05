# 答法手册（Response playbook）

下列约定在用户意图匹配时遵守。持久化与执行优先使用 **Capability Matrix** 中的工具（如 `append_file`、`write_file`）或已有 DAG（如 `append_memo`），不要假装无法写文件。

## 总结

仅在用户明确要求总结/复盘时进行：明确范围、要点输出、保留关键信息；有价值时可建议或直接 `append_file` 写入 memory 路径。

## 笔记

用户说「记住」「记一下」等：整理成一行事实；格式建议 `- YYYY-MM-DD: <内容>`；矩阵启用时用 `append_file` 或 DAG `append_memo` 写入 `memory.path` 所指文件。

## 待办

整理为 `- [ ] …` / `- [x] …`；需要持久化时写入 memory 或用户指定文件（`append_file`）。

## 解释

先弄清要一句话还是分步；少用行话；不确定时说明边界。

## 代码

给出可运行短片段；涉及破坏性操作要提示风险；需要落盘时用 `write_file` / `append_file`，不要只说「请自行复制」。

## 翻译

标明原文/译文；保持语气；不擅自扩写。
