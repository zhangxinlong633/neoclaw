# 废弃 Skills：迁入 Capability Matrix + Claw（方案 A）

日期：2026-09-05  
状态：已落地（2026-09-05）
范围：删除 `skills` 子系统；知识/答法迁入 claw；可执行意图迁入矩阵能力与 DAG。

## 1. 决策

**废弃** Neo 的 `skills:` 配置、`skills/` 目录扫描与 `SKILL.md` 按关键词注入。

**不**把 Markdown skill 伪装成可 `tool_calls` 的假工具（避免平行名字空间与空壳 capability）。

「转换成能力矩阵」在本仓库的含义是：

| 原 skill 角色 | 去向 |
|---------------|------|
| 可调用动作 / SOP | **Capability Matrix** 行 +（可选）`dags/library` |
| 身份、必引知识、答法契约 | **claw**：`bootstrap` / `soul` / `rules` / `memory` |
| 仅靠对话完成的软技能 | 并入 `rules` 答法手册；由模型直接答，靠矩阵 listing 发现真正刀刃 |

产品三角不变：**DAG ∥ Capability Matrix ∥ Policy**。claw 只负责 prompt 身份与硬约束，不再有第四套「skill 发现」系统。

## 2. 迁移对照表

| 原 skill | 转换去向 | 说明 |
|----------|----------|------|
| `me` | `bootstrap` 或仓库/示例 `AGENTS.md` 身份段 | 自称 Neo、勿报底层模型名 |
| `nanjing` | `rules/nanjing.md`（配置 `rules.paths`） | 必引数字保留；始终注入（原 high_priority） |
| `note` | 已有 `append_file` + DAG `append_memo`；规则中写「持久化用 append_file / append_memo」 | 不再用 skill 口头建议 |
| `todo` | `rules/response-playbook.md` 待办格式 + 可选 DAG（llm→append） | 无独立假工具 |
| `summarize` / `explain` / `code` / `translate` | `rules/response-playbook.md` 对应小节 | 非 exec；矩阵不注册空壳 tool |
| （发现「能干什么」） | 矩阵 prompt listing + DAG catalog | 替代 skill 目录式能力列举 |

仓库根可新增：

- `rules/nanjing.md` — 自 `skills/nanjing/SKILL.md` 迁入  
- `rules/response-playbook.md` — 合并 summarize/note/todo/explain/code/translate 的要点  
- 示例配置：`rules: { paths: ["rules/nanjing.md", "rules/response-playbook.md"] }`，去掉整个 `skills:` 块  
- 默认示例打开 `capability_matrix.directory` + `workflow_directory`（或文档明确指引）

## 3. 代码与配置变更（实现清单）

1. **删除注入路径**：`main.c` / daemon 中 `skills_append_to_system_prompt`；可删除或掏空 `src/core/skills.c`（优先删除并改 Makefile）。  
2. **配置**：`config.h` / `config.c` 移除 `skills_config_t` 与 `fill_skills` / `scan_skills_directory`；若仍读到 `skills` 键 → stderr 弃用提示并忽略（一轮兼容）。  
3. **删除目录** `skills/`（含 README / SKILL.md）。  
4. **文档**：根 `README.md`、`docs/claw.md`、`AGENTS.md` / `CLAUDE.md`、`config.json5.example`、`example/` —— 去掉 skills 叙述；强调矩阵 + claw。  
5. **测试**：去掉依赖 skills 扫描的断言（若有）；`make test` 必须通过。  
6. **矩阵文案**：tools 段说明改为「能力以 Capability Matrix / DAG catalog 为准；勿假设 skills/ 包」。

## 4. 明确不做

- 不为 translate/code 等注册无 `argv` 的「假 command」。  
- 不引入 `kind: knowledge` 第二套矩阵语义（若未来需要，另开 spec）。  
- 不把 skill 热加载逻辑搬进 `propose_capability`。  
- 不扩张成外部评论中的 enterprise security / container 执行网格（非本仓库目标）。

## 5. 验收

- [ ] 无 `skills:` 时配置可加载；带旧 `skills:` 仅警告不崩溃。  
- [ ] `./neo -d "你是谁"` 的 system 中无 skill 段，可有 bootstrap/rules；矩阵 listing 仍在（若启用）。  
- [ ] `./neo -d "南京旅游"` 能看到 rules 中迁入的必引数字。  
- [ ] `make test` 通过。  
- [ ] `AGENTS.md` 源码地图不再把 skills 列为能力发现路径。

## 6. 风险

- 未配置 `rules.paths` 的用户会失去南京数据与答法提示 → 示例配置必须带上迁移后的 paths。  
- 关键词按需注入消失 → rules 常驻会占 context；playbook 须控制篇幅（建议合计 &lt; 4k 字）。
