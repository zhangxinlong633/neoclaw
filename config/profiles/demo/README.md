# config/profiles/demo/

演示用 profile：使用本地占位模型端点，并内联示例 `capability_matrix.commands` 与 workflow，供 CLI 与冒烟测试引用。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `scripts/` | 本 profile 专用辅助脚本（含命令工具脚本） |

入口配置为同目录 `neo.json5`。运行示例：`./neo -p demo -v dag run demo_loop`。
