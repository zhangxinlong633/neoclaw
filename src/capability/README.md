# src/capability/

实现 **Capability Matrix** 及相关加载器与调度：行表物化、OpenAI `tools` JSON、prompt listing、builtin / commands / MCP 注册，以及反应式 tool loop 与路径沙箱。

## 主要源文件

| 文件 | 职责 |
|------|------|
| `capability_matrix.*` | 建表、listing、tools JSON |
| `capability_dir.*` | 能力目录加载与 `propose_capability` |
| `agent_tools.*` | tool loop 与 `neo_dispatch_tool` |
| `command_tools.*` | 白名单 `commands` 的 exec |
| `mcp_stdio.*` | stdio MCP → 矩阵行 |

用户文档：[`docs/tool.md`](../../docs/tool.md)。本目录无子目录。
