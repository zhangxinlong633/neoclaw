# Neo

**让 AI 可靠地办事，而不是只会聊天。**

Neo 是一个轻量、可移植的命令行智能助手：把「多步骤任务怎么走」「能调用哪些工具」「哪些事不许做」拆开管好，让云端大模型专心推理，本地负责执行与守门。

它不是要做成最强 IDE Agent，而是做成可拷到任意机器上的**瑞士军刀**——换环境改配置就能用，边界默认偏安全。

> 场景与战略定位见 [`docs/applications.md`](docs/applications.md)；上手命令见下文与 [`docs/examples.md`](docs/examples.md)。

---

## 它解决什么问题

| 业务痛点 | Neo 怎么帮 |
|----------|------------|
| 长任务容易跑偏、死循环 | 用**可复现的流程蓝图**（多步任务按图执行），而不是纯临场发挥 |
| 企业内部工具/脚本散乱，难给 AI 用 | 把可调用能力登记成一张**能力清单**，统一发现与调用 |
| 事事回云端，贵又慢 | **简单事本地做**（读文件、查时间、跑白名单命令），难事再问大模型 |
| 权限一开就怕出事 | **策略守门**（默认关随意 shell 等），按需放开 |

一句话：ChatGPT 像「大家共用的大脑」；Neo 更像「钉在你这台机器 / 这套流程上的手脚与纪律」。

---

## 适合谁

- 需要把 AI **接到真实文件、脚本、仓库、定时任务**上的个人或小团队  
- 希望流程**可审计、可重复**（同一任务多次跑结果路径一致）的业务/运维场景  
- 资源有限、希望**单文件二进制、少依赖**就能部署的环境  

不太适合：追求 Cursor 级多文件 IDE 体验、重型工作流平台（Temporal 等）替代品——那不是本产品目标。

---

## 你能拿它做什么（现行能力）

- **问一句就答**：终端里直接对话，可挂上身份、规则、记忆（例如领域必答材料）  
- **按流程办事**：先规划再执行，或直接跑已写好的流程（查时间、看仓库状态、列目录再总结……）  
- **让模型动手**：在授权范围内读文件、搜仓库、调用登记过的命令（含精选 Unix 工具）  
- **嵌进日常自动化**：daemon、管道、cron 脚本，同一核心多种唤起方式  

更完整的产业愿景（边缘、具身、IoT 等）见应用文档；**当前交付**以本机命令行助手为主。

---

## 五分钟上手

1. 安装依赖：macOS 一般已有 libcurl；Linux 可装发行版的 libcurl 开发包。  
2. 在仓库根目录构建：`make`  
3. 复制配置并填入模型密钥：

```bash
cp config/config.json5.example config/config.json5
# 编辑 api_key、模型名
```

4. 试跑：

```bash
./neo "你是谁"
```

---

## 简单样例

在仓库根执行（需已配置可用的模型密钥）：

```bash
# 日常问答
./neo "用三句话说明 Neo 能帮业务团队做什么"

# 按固定流程查看当前 UTC 时间
./neo workflow run show_time

# 用自然语言交代任务：规划并执行
./neo run "看下系统时间"

# 让助手读 README 并概括（会调用本地读文件能力）
./neo "请阅读 README.md，用三句中文概括产品价值。" 2>&1

# 只看计划、先不执行
./neo plan "帮我摸清当前仓库最近在忙什么"
```

更多场景（多轮会话、仓库脉搏、排错对照）见 [`docs/examples.md`](docs/examples.md)。

---

## 三种常用工作方式

| 你想… | 怎么用 |
|-------|--------|
| 随便问一句 | `./neo "…"` |
| 跑已经定好的流程 | `./neo workflow run 流程名` |
| 用自然语言交代，再自动规划执行 | `./neo run "…"`（只要计划用 `./neo plan`） |

后台常驻、定时任务等：`./neo daemon`、[`scripts/`](scripts/) 下的辅助脚本。细节见 [`docs/workflow.md`](docs/workflow.md)、[`docs/claw.md`](docs/claw.md)。

---

## 文档导航

| 文档 | 适合谁 |
|------|--------|
| [`docs/examples.md`](docs/examples.md) | 想对着命令练手 |
| [`docs/applications.md`](docs/applications.md) | 想了解业务场景与生态位 |
| [`docs/architecture.md`](docs/architecture.md) | 想了解目标架构与演进 |
| [`docs/tool.md`](docs/tool.md) | 要登记/治理可调用能力 |
| [`docs/workflow.md`](docs/workflow.md) | 要写或跑多步流程 |
| [`docs/claw.md`](docs/claw.md) | 要配身份、规则与记忆 |
| [`AGENTS.md`](AGENTS.md) | 要改本仓库代码的人与 AI |

---

## 给开发者（摘要）

仓库布局、配置键名、构建测试等技术细节以 [`AGENTS.md`](AGENTS.md) 为准。常用命令：

```bash
make          # 生成 ./neo
make test     # 单元测试 + CLI 冒烟
make test-cli # 仅 CLI 冒烟
```

产品实现上对应三层协作：**流程怎么走（DAG）∥ 能调用什么（能力矩阵）∥ 许不许做（策略）**。配置里对应 `workflow_directory` / `workflows`、`capability_matrix`、各类 Policy 开关。旧键 `skills` 已移除；旧顶层键 `tools` 仅兼容读取。
