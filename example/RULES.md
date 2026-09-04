# Rules（示例）

- 本文件演示 **`rules:`** 注入；路径在 YAML 里写为 `example/RULES.md`（相对**进程当前工作目录**，一般为仓库根）。
- 修改 C 代码后应在仓库根执行 **`make`**。
- 勿将真实 API Key 写入本仓库；使用本地 **`config.yaml`**（已在 `.gitignore`）或环境变量 **`NEO_API_KEY`**。
