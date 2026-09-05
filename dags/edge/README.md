# dags/edge/

**预留**目录：对应 [`docs/applications.md`](../../docs/applications.md) 行业层中的边缘自治与跨设备联锁编排。

## 现状

- 当前**无** DAG 文件，且**未**列入 `dags/manifest.json5` 的 `load`。
- 不登记虚假「拍照→剔除」或消防联锁图；待能力矩阵具备真实、可认证的执行器后再编写。

## 启用方式（将来）

1. 编写含 `requires` / `when` / `when_not` / `outcome` 的 DAG 文件。
2. 在 `manifest.load` 中增加 `"edge"`。
3. 重启 `neo`；现场部署须遵守上位安全与认证规范（见 applications 约束说明）。

本目录暂无其它子目录。
