# capabilities/proposed/

存放由内建能力 `propose_capability` 写出的**能力草稿**（方案 A：仅落盘、当轮不热加载）。

## 行为约定

- 启动扫描**不会**将本目录文件并入 Capability Matrix。
- 人工审核后，应将文件移至 `../commands/`（或其它列入 `manifest.load` 的子目录），并重启 `neo` 后生效。
- 若文件已存在同名草稿，`propose_capability` 将拒绝覆盖。

本目录无子目录。详见 [`docs/tool.md`](../../docs/tool.md)。
