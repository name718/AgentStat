# AgentStat 🤖

> **AI Agent 使用情况、Token 消耗与代码采纳率分析工具 (CLI + Web Analytics Dashboard)**

`agentstat` 是一款基于 **C 语言 (C11)** 和 SQLite 的本地 AI 辅助开发分析工具。它从 Codex、Claude Code 与 AGY/Antigravity 的本地日志采集真实会话、模型、Token、工具调用和代码变更，并与 Git 提交进行保守的精确行归因。

---

## ✨ 核心特性

- ⚡ **轻量本地运行**：C11 单一可执行文件，无 Node.js/Python 运行时依赖；构建时需要 SQLite3 开发库。
- 📊 **真实数据口径**：CLI 和 Web 共用统一的 Session、ModelUsage、ToolCall 与 CodeChange 事件表，不读取演示记录。
- 🌐 **中文 Web 看板**：展示真实模型、Token/缓存、Agent 来源、最近会话、工具排行、代码结构和 Git 采纳率。
- 🔌 **多 Agent 适配器**：支持 Codex、Claude Code 与 AGY/Antigravity，新增 Agent 只需实现新的日志适配器。
- 🔒 **本地数据安全**：所有数据自动持久化到本地 SQLite 数据库 `~/.agentstat/agentstat.db`，纯本地运行，数据零上报。
- 🧾 **可核验归因**：源码只在内存中处理，数据库仅保存计数、路径和新增非空行的 SHA-256 指纹。

---

## 🛠️ 快速开始

### 1. 编译构建

项目提供标准的 `Makefile` 与 `CMakeLists.txt`，支持 macOS, Linux 与 Windows (MSVC/MinGW)。

```bash
# 克隆仓库并进入项目目录
cd agent-cli

# 一键编译
make
```

编译成功后，将在根目录下生成二进制可执行文件 `agentstat`。

---

## 🚀 使用指南

### 1. 同步并查看真实汇总

```bash
./agentstat sync
./agentstat summary
```

### 2. 启动 Web 可视化看板

```bash
./agentstat web --port 8080
```

打开浏览器访问 [http://localhost:8080](http://localhost:8080)。页面全部读取真实事件 API，不会在空数据时自动生成 mock 数据。

---

### 3. 查看真实模型明细

```bash
./agentstat chart
```

### 4. 查看最近真实会话

```bash
./agentstat list --limit 10
```

### 5. 查看分项统计

```bash
./agentstat usage
./agentstat code
./agentstat git-stats
./agentstat attribution
```

### 6. 自动同步 Codex、Claude Code 与 AGY/Antigravity

```bash
./agentstat sync
./agentstat usage
./agentstat code
./agentstat attribution
```

`sync` 会自动发现三种 Agent 的标准数据目录，并在当前目录是 Git 仓库时同步当前仓库：

| Agent | 默认数据目录 | Session | Token/缓存 | 工具/MCP | 代码变更 |
|---|---|---:|---:|---:|---:|
| Codex | `~/.codex/sessions` | 支持 | 支持 | 支持 | 支持 |
| Claude Code | `~/.claude/projects` | 支持 | 支持 | 支持 | 支持 |
| AGY / Antigravity | `~/.gemini/antigravity-cli` | 支持 | 日志暂未暴露稳定字段 | 支持 | 支持 |

三种解析器只负责把本地日志转换为统一的 Session、Usage、ToolCall 和 CodeChange 事件，SQLite 统计及 Git 归因层不依赖具体 Agent。Claude Code 的重复消息片段会按消息 ID 去重，只有成功返回的 Edit、Write 和 MultiEdit 才计入代码变更；Antigravity 支持 `replace_file_content`、`multi_replace_file_content` 与 `write_to_file`。

标准安装无需配置。非标准目录可用环境变量覆盖：

```bash
AGENTSTAT_CODEX_DIR=/custom/codex \
AGENTSTAT_CLAUDE_DIR=/custom/claude \
AGENTSTAT_ANTIGRAVITY_DIR=/custom/antigravity \
./agentstat sync
```

数据库目录仍可用 `AGENTSTAT_DATA_DIR` 覆盖。其他 Git 仓库需要各执行一次 `./agentstat sync-git /path/to/repository`；后续重复同步不会重复计数。

也可以单独控制每个适配器：

```bash
./agentstat sync-codex
./agentstat sync-claude
./agentstat sync-antigravity
```

### 7. 导入 Codex 真实使用数据

```bash
./agentstat import-codex ~/.codex/sessions/2026/08/07/rollout-xxx.jsonl
./agentstat sync-codex ~/.codex/sessions
./agentstat usage
./agentstat code
```

`sync-codex` 不传目录时默认扫描 `~/.codex/sessions`。导入器只持久化会话元数据、模型、Token/缓存计数、工具名称，以及成功 patch 的文件路径和增删行数；不保存用户消息、模型回复、推理内容、工具参数或源码 diff。同一源文件或目录可以重复执行，已导入的事件不会重复计数；Codex 重复写入的相同 Token 累计快照也会被跳过。

`code` 统计业务代码、测试、文档、生成文件和其他文件中的 Agent 应用变更。这里的行数表示 Agent 成功应用的 patch，不代表这些代码已经进入 Git 提交或被最终保留。Web 服务提供 `GET /api/usage` 和 `GET /api/code` 两个聚合接口。

### 8. 导入 Git 提交基线

```bash
./agentstat sync-git /path/to/repository
./agentstat git-stats
```

Git 同步只保存规范化仓库路径、commit hash、提交时间、作者，以及逐文件的新增/删除行数与文件分类；不保存提交消息、patch 或源码内容。重复同步不会重复统计。Web 服务提供 `GET /api/git` 聚合接口。Git 数据目前是提交侧基线，尚未与 Agent patch 做最终采纳归因。

### 9. 计算精确代码采纳率

```bash
./agentstat attribution
```

归因引擎分别对 Agent patch 和后续 Git commit 的新增非空行生成 SHA-256 指纹，只在同一仓库、同一文件、相同内容且提交时间不早于 Agent patch 时计为采纳。重复行按两侧出现次数的较小值计算，生成文件默认排除。数据库只保存不可逆指纹，不保存源码正文。该指标是保守的精确匹配：原样保留的行会被识别，经过修改的行暂不计入。Web 服务提供 `GET /api/attribution`。

---

## 📁 存储规范与数据格式

所有交互记录持久化在用户家目录下的 SQLite 数据库 `~/.agentstat/agentstat.db` 中。首次启动时，旧版 `~/.agentstat/records.csv` 会自动导入数据库，原 CSV 文件保留不动。

**字段说明：**

| 字段 | 类型 | 说明 |
|---|---|---|
| `session_id` | String | 会话唯一 ID (如 `sess_20260806_001`) |
| `timestamp` | String | ISO8601 格式时间戳 |
| `project` | String | 所属项目名称 (如 `agent-cli`) |
| `language` | String | 编程语言 (如 `C`, `TypeScript`, `Go`) |
| `model` | String | 模型名称 (如 `claude-3-5-sonnet`) |
| `input_tokens` | Long | Prompt Token 消耗量 |
| `output_tokens` | Long | Completion Token 消耗量 |
| `cost_usd` | Double | 自动换算的预估花费 ($ USD) |
| `lines_suggested` | Int | Agent 推荐的代码总行数 |
| `lines_accepted` | Int | 用户实际保留/采纳的代码行数 |
| `snippets_suggested`| Int | 代码块推荐数量 |
| `snippets_accepted` | Int | 代码块采纳数量 |
| `duration_sec` | Double | 会话交互响应耗时 (秒) |

---

## 📂 项目结构

```
agent-cli/
├── README.md              # 项目中文说明文档
├── Makefile               # Makefile 编译构建配置
├── CMakeLists.txt         # CMake 构建配置
├── docs/                  # 产品与技术方案设计文档
│   └── agentstat_design_spec.md
├── include/               # C 语言头文件目录
│   ├── agentstat.h        # 核心数据结构与统一定义
│   ├── cli.h              # 命令行参数解析器
│   ├── stats.h            # 指标引擎与费用计算逻辑
│   ├── storage.h          # SQLite 数据持久化引擎
│   ├── importer.h         # Codex 数据适配器
│   ├── claude_importer.h  # Claude Code 数据适配器
│   ├── antigravity_importer.h # AGY/Antigravity 数据适配器
│   ├── server.h           # 内嵌 Web HTTP Server
│   └── ui.h               # ANSI 终端渲染组件
├── src/                   # C 语言源代码目录
│   ├── main.c             # 主入口
│   ├── cli.c              # CLI 参数分发调度
│   ├── stats.c            # 手工兼容记录的基础统计
│   ├── storage.c          # SQLite 读写与 Seeder
│   ├── importer.c         # Codex JSONL 解析
│   ├── claude_importer.c  # Claude Code JSONL 解析
│   ├── antigravity_importer.c # AGY transcript 解析
│   ├── server.c           # Socket HTTP Server 与 JSON API
│   └── ui.c               # ANSI 终端 Dashboard 渲染
└── web/                   # Web Dashboard 前端静态页面
    └── index.html         # 中文真实数据看板
```

---

## 📄 开源许可

[MIT License](LICENSE)
