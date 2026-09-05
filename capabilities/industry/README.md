# capabilities/industry/

**预留**目录：对应 [`docs/applications.md`](../../docs/applications.md) 行业层（工业边缘、具身、物联网执行器等）。

## 现状

- 当前**无**能力文件，且**未**列入 `capabilities/manifest.json5` 的 `load`。
- 不假装已交付 PLC、相机或传感器驱动；待有真实、可审计的 argv / MCP 实现时再登记。

## 启用方式（将来）

1. 按一文件一能力写入本目录（含 `when` / `when_not` 等选型元数据）。
2. 在 `manifest.load` 中增加 `"industry"`。
3. 重启 `neo`；确认 Policy（路径沙箱、超时、输出上限）满足现场约束。

本目录暂无其它子目录。
