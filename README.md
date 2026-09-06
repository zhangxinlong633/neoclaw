# Neo

**A goal-oriented, flexible agent system.**

Give Neo a concrete goal (check status, organize materials, run a fixed ops flow…), and it carries the work through as agreed. It is not built to “chat about anything”; it is built to **get a defined class of work done solidly**.

Two pillars drive that:

| Pillar | Business meaning | What it governs |
|--------|------------------|-----------------|
| **DAG (flow blueprint)** | How work advances step by step, and how data/results hand off | **Scheduling**: what runs first, what next, which steps depend on which |
| **Capability matrix** | The allow-list of “moves” (read files, whitelisted commands, external APIs, …) | **Invocation**: which capability a step may actually call |

Add policy (what is forbidden by default), and you get operating discipline: the LLM understands and generates; Neo **schedules by the graph and invokes only from the matrix**.

Copy it to another machine, adjust config, and use it—a portable **Swiss Army knife**, not a heavyweight studio.

Full scenarios and positioning: [`docs/applications.md`](docs/applications.md). Hands-on commands: below and [`docs/examples.md`](docs/examples.md).

[中文版 README](README_zh.md)

---

## Where the business can extend

Neo’s value is not “which model is smarter,” but whether a **goal-oriented agent can schedule stably, invoke from an allow-list, and keep cost and boundaries under control**. The layers below match [`docs/applications.md`](docs/applications.md): ship what works today first; treat the later layers as direction and vision (not fully delivered yet).

### Ship today: make agent tasks actually finish

For the common “hard to land” problems of individuals, small teams, and ops:

| Business need | How Neo holds it up |
|---------------|---------------------|
| **Multi-step goals must complete and be reviewable** | Encode “fetch → tidy → decide → notify/persist” as a repeatable DAG; the same goal follows a clear path every run |
| **Lots of internal skills—usable by agents, but governed** | Register scripts, commands, APIs as atomic capabilities in the matrix; tasks may call only listed capabilities |
| **Don’t send everything to the cloud: costly, slow, and data-sensitive** | Prefer local capabilities for simple steps; call the LLM for hard reasoning; keep sensitive ops inside the trust boundary when policy allows |

Typical picture: repo/doc sidekick, fire-and-forget goals on a schedule, SOP-style daily checks—**this layer is what the repo delivers today**.

### Next: field and edge (direction)

When “near the data, low latency, and closed loops under weak networks” become hard requirements, the same **DAG + capability matrix** can extend on-site (full form is on the architecture roadmap; today this is mostly reserved contracts):

| Direction | Business picture | Neo’s role |
|-----------|------------------|------------|
| **Industrial edge** | Inspection, interlocking, anomaly handling must close on the line | Schedule on industrial hosts; run registered capabilities on nodes; finish critical paths even when the network is unstable |
| **Embodied / mobile platforms** | Patrol, service robots, onboard task orchestration | Task-level scheduling and capability governance (not a replacement for hard real-time motion control) |
| **Building / plant linkage** | Sensor-triggered cross-device actions need short paths | Regional scheduling by graph; reduce “everything via the public cloud” inside policy |

### Further: intelligent infrastructure (vision)

As the architecture matures, the product may grow from “an assistant” into “a layer of infrastructure” (**not a current feature list**):

- Extend devices with **capability packs**, not only traditional apps  
- A **private digital assistant** on a home or org gateway: mail, docs, browser capabilities scheduled by graph; sensitive data stays in the trust domain by default  
- Cross-system capability distribution and reuse—a governable capability ecosystem  

### Ecological niche (one line)

Cloud LLMs supply deep cognition; **Neo is the scheduling and execution spine that plugs intelligence into real systems**—not a substitute for the brain, but a way to make goals orchestrable, capabilities governable, and boundaries enforceable. Detail and maturity: [`docs/applications.md`](docs/applications.md) §§5–6.

---

## Who it is for

- Individuals or small teams that need **goal-oriented** agent work (not just chat)  
- Business / ops that want the **same class of goals** to run controllably and reviewably  
- Anyone who wants light deploy: change config per environment—no heavy platform first  

If you want “the strongest coding IDE” or “a huge workflow middle platform,” that is not Neo’s direction—we deliberately stay flexible, landable, and sharp-edged on boundaries.

---

## What you can do now

Matching “ship today” above, on a local machine you can already:

1. **State a goal and get it done**: natural language in; plan then execute (or plan only).  
2. **Run a fixed DAG**: encode common goals as flows; one command schedules by the graph.  
3. **Invoke via the capability matrix**: read files, search the repo, run registered commands within policy.  
4. **Fit daily rhythm**: terminal, long-running listener, cron / pipes—one agent, many entry points.

---

## Five-minute start

1. A Mac or Linux box with network access (to call your chosen LLM API).  
2. In this repo: `make` (builds the `neo` binary).  
3. Copy config and fill in endpoint + key:

```bash
cp config/config.json5.example config/config.json5
# Edit the file: set api_key and model name
```

4. Try:

```bash
./neo "Who are you?"
```

---

## A few commands to learn by doing

From the repo root (after API key is set):

```bash
# What kinds of goal-oriented work Neo is good for
./neo "In three sentences, what goal-oriented tasks is Neo good for?"

# Run a catalog DAG: show current time
./neo dag run show_time

# Flagship workspace SOP: list → brief → append WORKSPACE_BRIEF.md
./neo dag run workspace_brief

# One-line goal: plan and execute
./neo run "show the system time"

# Use the read-file capability, then summarize product value
./neo "Read README.md and summarize the product value in three English sentences."

# Plan only (DAG draft), do not execute yet
./neo plan "figure out what this repo has been busy with lately"
```

More examples: [`docs/examples.md`](docs/examples.md).

---

## Everyday usage (three modes)

| You want… | Say |
|-----------|-----|
| A quick question | `./neo "your question"` |
| Finish a goal via a fixed DAG | `./neo dag run <dag-name>` (or `./neo run <dag-name>`) |
| State a goal in one line and finish it | `./neo run "the goal"` |

Plan without executing: use `plan` instead of `run`.

---

## Learn more

| Topic | Doc |
|-------|-----|
| Scenarios & positioning (full) | [`docs/applications.md`](docs/applications.md) |
| Hands-on examples | [`docs/examples.md`](docs/examples.md) |
| Capability matrix | [`docs/tool.md`](docs/tool.md) |
| DAG scheduling | [`docs/workflow.md`](docs/workflow.md) |
| Identity, rules, memory | [`docs/claw.md`](docs/claw.md) |
| Contributing | [`AGENTS.md`](AGENTS.md) |
| Chinese README | [`README_zh.md`](README_zh.md) |

---

## For developers

Build / test: `make` / `make test`. Product triad: **DAG ∥ Capability Matrix ∥ Policy**. Conventions: [`AGENTS.md`](AGENTS.md). Architecture: [`docs/architecture.md`](docs/architecture.md).
