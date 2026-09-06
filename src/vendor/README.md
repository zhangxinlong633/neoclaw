# src/vendor/

第三方源码 amalgamation 存放处。保持上游风格；升级时宜整文件替换，并运行 `make test` 验证。

| 文件 | 用途 |
|------|------|
| `yyjson.c` / `yyjson.h` | JSON5 配置与运行时 JSON |
| `BearHttpsClientOne.c` / `BearHttpsClient.h` | BearSSL 系 HTTPS 客户端（单文件 amalgamation）；由 `neo_http` 调用，业务代码勿直接 include |

不强制适用本仓库的中文注释约束（见 [`AGENTS.md`](../../AGENTS.md) §7）。本目录无子目录。
