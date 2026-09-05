# skills/

按主题划分的 Skill 包集合。配置 `skills.directory` 指向本目录后，Neo 扫描各子目录的 `SKILL.md`，按匹配与优先级规则注入 system prompt。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `<name>/` | 单个 skill；权威内容为该目录下的 `SKILL.md`；另有正式 `README.md` 指向之 |

注入与 `unmatched` 行为见仓库根 [`README.md`](../README.md)「Skills 与 Memory」及 [`docs/claw.md`](../docs/claw.md)。
