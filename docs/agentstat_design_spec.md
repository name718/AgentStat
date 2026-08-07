# AgentStat (AI Agent Analytics & Code Acceptance CLI + Web Dashboard) 
## 完整产品方案与技术方案设计文档

> **项目名称**：`agentstat` (Agent + Statistics，借鉴 UNIX `vmstat` / `iostat` 简洁命名风格)  
> **核心定位**：极轻量、零外部依赖的 AI Agent 效能与代码采纳率分析工具（支持 CLI 终端 + Web 极速大屏双模式）  
> **实现状态说明（2026-08-07）**：本文前半部分保留早期产品探索，出现的 Chart.js、成本估算、mock 记录和手工 Suggested/Accepted 指标不代表当前真实数据口径。当前实现以 Codex、Claude Code、AGY/Antigravity 本地日志、SQLite 统一事件表和 Git SHA-256 行归因为准；Web 为无外部前端依赖的中文看板。
> **语言/技术选型**：C 语言核心引擎 (C11/POSIX) + SQLite + 原生 HTML/CSS/JS Web 看板

---

## 目录 (Table of Contents)
- [1. 产品方案 (Product Design Specification)](#1-产品方案-product-design-specification)
  - [1.1 核心痛点与定位](#11-核心痛点与定位)
  - [1.2 核心功能矩阵 (CLI + Web Feature Matrix)](#12-核心功能矩阵-cli--web-feature-matrix)
  - [1.3 核心指标体系 (Metrics Architecture)](#13-核心指标体系-metrics-architecture)
  - [1.4 CLI 交互设计 (Command Line UX Design)](#14-cli-交互设计-command-line-ux-design)
  - [1.5 Web Dashboard 产品界面规划](#15-web-dashboard-产品界面规划)
- [2. 技术方案 (Technical Architecture Specification)](#2-技术方案-technical-architecture-specification)
  - [2.1 系统分层架构 (System Architecture)](#21-系统分层架构-system-architecture)
  - [2.2 数据采集与 Hook 机制设计 (Data Collection & Hooking)](#22-数据采集与-hook-机制设计-data-collection--hooking)
  - [2.3 Web 架构与 C 内置 Http 服务设计 (Web Architecture & Server)](#23-web-架构与-c-内置-http-服务设计-web-architecture--server)
  - [2.4 关键算法与计算逻辑 (Algorithms & Calculation Engines)](#24-关键算法与计算逻辑-algorithms--calculation-engines)
  - [2.5 C 语言数据结构定义 (Core C Data Structures)](#25-c-语言数据结构定义-core-c-data-structures)
  - [2.6 持久化存储规范 (Storage Schema)](#26-持久化存储规范-storage-schema)
- [3. 集成与扩展计划 (Integration Roadmap)](#3-集成与扩展计划-integration-roadmap)

---

## 1. 产品方案 (Product Design Specification)

### 1.1 核心痛点与定位

在当前 AI 驱动开发（AI-Assisted Software Engineering）盛行的背景下，开发者和技术团队面临以下痛点：
1. ** Token 消耗黑盒**：无法精准统计各个模型（Claude 3.5, GPT-4o, DeepSeek, Gemini 等）的输入/输出 Token 消耗量与实际花费。
2. **代码采纳率难以量化**：AI 输出了大量代码，但开发者最终保留了多少？拒绝/修改了多少？缺乏直观的数据支撑。
3. **团队/项目效能盲区**：不知道哪些项目、哪些编程语言对 AI 的依赖度最高或 ROI 收益最大。
4. **多样化展现需求**：终端开发者喜欢 **CLI 命令行极速查看**；团队 Leader / 敏捷汇报需要 **炫酷的 Web 可视化大屏** 导出图表。

**产品定位**：`agentstat` 是一款双引擎 AI 效能分析与监控系统——既提供微秒级响应的 C 语言 CLI 命令行，又提供通过 `agentstat web` 即可一键拉起的一站式 **Web Analytics Dashboard**。

---

### 1.2 核心功能矩阵 (CLI + Web Feature Matrix)

| 模块 | 功能项 | 命令行 CLI 表现 | Web Analytics 看板表现 |
|---|---|---|---|
| **数据采集** | **Record API** | `agentstat record --project ...` 命令录入 | 接收 IDE 插件 HTTP POST 格式录入 |
| | **Log Parser** | `agentstat import --log-path` | Web 界面支持拖拽上传日志解析 |
| **指标计算** | **代码采纳率** | 终端显示行级/块级采纳率百分比 | 环形图、趋势折线图展示采纳率变化 |
| | **Token & 成本** | 估算每次及累计 $ USD 开销 | 按模型开销排行榜、 Token 消耗堆叠图 |
| **视图展现** | **全景看板** | ANSI 双栏彩色 Dashboard | Vibrant Dark Mode 双栏统计大屏 |
| | **图表可视化** | ANSI 字符柱状图 | 动态折线图、柱状图、饼图 (Chart.js) |
| | **多维过滤** | 命令行参数 `--project` / `--model` 过滤 | 页面顶栏交互式 Filter 下拉框 |
| **导出集成** | **报告导出** | `agentstat export --format json/md` | 一键导出 PNG 图表、PDF 周报、JSON/CSV |

---

### 1.3 核心指标体系 (Metrics Architecture)

```
                              ┌───────────────────────────┐
                              │    Agent Metric System    │
                              └─────────────┬─────────────┘
                                            │
        ┌───────────────────────────────────┼───────────────────────────────────┐
        │                                   │                                   │
        ▼                                   ▼                                   ▼
┌───────────────────────┐       ┌───────────────────────┐       ┌───────────────────────┐
│ Token & Cost Metrics  │       │ Quality & Acceptance  │       │ Productivity & Speed  │
├───────────────────────┤       ├───────────────────────┤       ├───────────────────────┤
│ • Prompt Tokens       │       │ • Lines Suggested     │       │ • Turns per Session   │
│ • Completion Tokens   │       │ • Lines Accepted      │       │ • Session Duration    │
│ • Context Cache       │       │ • Line Acceptance %   │       │ • Tokens / Sec Speed  │
│ • Estimated Cost USD  │       │ • Snippets Acceptance │       │ • Lang / Proj Breakdown│
└───────────────────────┘       └───────────────────────┘       └───────────────────────┘
```

---

### 1.4 CLI 交互设计 (Command Line UX Design)

```bash
# 1. 查看全景统计 Dashboard (CLI)
agentstat summary

# 2. 一键启动 Web Dashboard (浏览器自动打开 http://localhost:8080)
agentstat web --port 8080

# 3. 记录一次交互
agentstat record --project agent-cli --model claude-3-5-sonnet --input 12000 --output 3000 --suggested 100 --accepted 90

# 4. 导出 HTML 报告或数据
agentstat export --format html --out report.html
```

---

### 1.5 Web Dashboard 产品界面规划

Web 端界面采用 **Cyberpunk Dark Mode + Glassmorphism (玻璃拟态)** 现代风格：

1. **顶栏 (Header Bar)**：
   - 品牌 Logo (`AgentStat`) + 状态标识 (Live Sync)
   - 项目选择器 (`All Projects` / `agent-cli` / `web-app`)
   - 时间粒度选择器 (`Today`, `Last 7 Days`, `Last 30 Days`, `All Time`)
2. **核心 KPI 卡片区 (Top Key Cards)**：
   - **Total Sessions**: 总交互次数
   - **Token Usage**: 累计消耗 Token 数 (Prompt / Output 占比)
   - **Line Acceptance Rate**: 代码采纳率卡片 (含环形百分比图)
   - **Total Estimated Cost**: 累计开销 ($ USD)
3. **图表主展现区 (Chart Panels)**：
   - **左区：代码采纳率趋势图 (Line Acceptance Trend)** —— 每日 suggested vs accepted 堆叠柱状图与折线。
   - **右区：模型分布与花费 (Model Distribution & Cost Breakdown)** —— 各模型 (Claude 3.5, GPT-4o, DeepSeek) Token 占比饼图。
4. **会话明细表格 (Session History Table)**：
   - 实时可检索的交互列表，支持点击查看单次建议的具体行数与采纳情况。

```
+---------------------------------------------------------------------------------------+
|  [AgentStat]   Project: [ All Projects v ]   Time: [ Last 7 Days v ]   ( Export PDF ) |
+---------------------------------------------------------------------------------------+
|  +------------------+  +-------------------+  +------------------+  +---------------+ |
|  |  SESSIONS        |  |  TOTAL TOKENS     |  | ACCEPTANCE RATE  |  | TOTAL COST    | |
|  |  142 sessions    |  |  969.7 K Tokens   |  |     88.1%        |  |  $ 2.8450 USD | |
|  +------------------+  +-------------------+  +------------------+  +---------------+ |
+---------------------------------------------------------------------------------------+
|  +--------------------------------------------+  +---------------------------------+  |
|  | Code Acceptance Trend (Line & Snippets)   |  | Model Cost Breakdown (Pie)      |  |
|  | [ Chart.js Stacked Bar & Line Chart ]      |  | • Claude 3.5 (65%)              |  |
|  |                                            |  | • GPT-4o (25%)                  |  |
|  |                                            |  | • DeepSeek (10%)                |  |
|  +--------------------------------------------+  +---------------------------------+  |
+---------------------------------------------------------------------------------------+
|  Session History Details (Table with Filter & Search)                                 |
+---------------------------------------------------------------------------------------+
```

---

## 2. 技术方案 (Technical Architecture Specification)

### 2.1 系统分层架构 (System Architecture)

```
                       +-----------------------------------+
                       |    User Access (CLI or Browser)   |
                       +-----------------+-----------------+
                                         |
                +------------------------+------------------------+
                |                                                 |
                v                                                 v
    +───────────────────────+                         +───────────────────────+
    |   CLI Engine (C11)    |                         |  Web Dashboard (SPA)  |
    | (main.c / cli.c / ui) |                         | HTML5/CSS3/Chart.js   |
    +───────────┬───────────+                         +───────────┬───────────+
                │                                                 │
                │                                                 │ REST API / Static JSON
                v                                                 v
    +─────────────────────────────────────────────────────────────────────────+
    |                     Embedded C Web Server / API                         |
    |                         (server.c / micro-http)                         |
    +────────────────────────────────────┬────────────────────────────────────+
                                         │
                                         v
    +─────────────────────────────────────────────────────────────────────────+
    |                       Local Storage (~/.agentstat/)                     |
    |                         agentstat.db (SQLite)                           |
    +─────────────────────────────────────────────────────────────────────────+
```

---

### 2.2 数据采集与 Hook 机制设计 (Data Collection & Hooking)

系统支持 **三种数据采集模式**：
1. **IDE Hook / HTTP API 上报**：IDE 插件在接受代码后，调用 `http://localhost:8080/api/record` 或后台静默执行 `agentstat record`。
2. **Log Parser (日志解析)**：后端扫描 `~/.antigravity/logs` 或 `~/.cursor/logs` 自动提取 Token 消耗与 Diff 数据。
3. **CLI 命令行输入**：脚本或手动执行。

---

### 2.3 Web 架构与 C 内置 Http 服务设计 (Web Architecture & Server)

为保证全平台**零外部重型依赖**（无需安装 Node.js/Python），Web 服务采用以下机制：

1. **C 内置 Web Server (`server.c`)**：
   - 使用 POSIX Socket (或 `mongoose` / `microhttpd` 模式) 实现百行级 C 极轻量 HTTP 服务。
   - 监听本地端口（默认 `8080`），暴露核心 REST API：
     - `GET /api/summary`: 返回整体汇总数据 (JSON)。
     - `GET /api/records`: 返回历史明细记录 (JSON)。
     - `POST /api/record`: 接收 IDE/Hook 的实时数据上报。
2. **Web 静态资源内嵌机制 (Static Embedding)**：
   - 使用 C 语言 `xxd -i` 或内联 HTML 将 Single-Page App (`index.html`) 内嵌在可执行二进制文件中。
   - 用户只需运行一个独立的 `agentstat` 二进制可执行文件，即可直接拉起完整的 Web 控制台！

---

### 2.4 关键算法与计算逻辑 (Algorithms & Calculation Engines)

#### A. Diff 相似度与代码采纳计算
在 Agent 提交代码建议时，记录 `suggested_code`（建议代码），用户编辑/保存后读取 `accepted_code`（保留代码）。
- 使用 Myers Diff / 行匹配算法计算两者相似度。
- 完全匹配则 `lines_accepted = lines_suggested`。
- 部分修改精准计算保留的行数 $L_{\text{accepted}}$。

#### B. 动态模型价格算盘矩阵 (Model Price Matrix Engine)
根据输入/输出 Token 动态计算美元开销：

| 模型 Key | 输入 Token 价格 ($/1M) | 输出 Token 价格 ($/1M) |
|---|---|---|
| `claude-3-5-sonnet` | $3.00 | $15.00 |
| `claude-3-opus` | $15.00 | $75.00 |
| `gpt-4o` | $2.50 | $10.00 |
| `gemini-1.5-pro` | $1.25 | $5.00 |
| `gemini-1.5-flash` | $0.075 | $0.30 |
| `deepseek-coder` | $0.14 | $0.28 |

---

### 2.5 C 语言数据结构定义 (Core C Data Structures)

`include/agentstat.h` 内部核心数据结构：

```c
#ifndef AGENTSTAT_H
#define AGENTSTAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define MAX_RECORDS 10000
#define MAX_STR_LEN 128
#define DEFAULT_DB_FILENAME "agentstat.db"

// 单次 Agent 交互/Session 记录结构体
typedef struct {
    char session_id[MAX_STR_LEN];     // 唯一 Session ID (如 sess_20260806_001)
    char timestamp[MAX_STR_LEN];      // ISO8601 时间戳
    char project[MAX_STR_LEN];        // 项目名称
    char language[MAX_STR_LEN];       // 编程语言 (C, Python, Go, TS)
    char model[MAX_STR_LEN];          // 模型类型 (claude-3-5-sonnet, gpt-4o 等)
    
    long input_tokens;                // Prompt Token 消耗
    long output_tokens;               // Completion Token 消耗
    double estimated_cost_usd;        // 预估花费 ($ USD)
    
    int lines_suggested;              // Agent 推荐代码行数
    int lines_accepted;               // 用户实际采纳代码行数
    int snippets_suggested;           // 推荐代码块总数
    int snippets_accepted;            // 采纳代码块总数
    
    double duration_seconds;          // 会话响应/交互耗时
} AgentRecord;

// 全局/阶段汇总统计结构体
typedef struct {
    long total_sessions;              // 总 Session 数
    long total_input_tokens;          // 累计 Prompt Tokens
    long total_output_tokens;         // 累计 Completion Tokens
    long total_tokens;                // 累计总 Tokens
    double total_cost_usd;            // 累计预估总费用 ($ USD)
    
    long total_lines_suggested;       // 累计推荐行数
    long total_lines_accepted;        // 累计采纳行数
    double line_acceptance_rate;      // 行级采纳率百分比 (0.0 - 100.0)
    
    long total_snippets_suggested;    // 累计推荐代码块数
    long total_snippets_accepted;     // 累计采纳代码块数
    double snippet_acceptance_rate;   // 代码块采纳率百分比 (0.0 - 100.0)
    
    double avg_tokens_per_session;    // 平均单次 Session 消耗 Tokens
} AgentSummaryStats;

#endif // AGENTSTAT_H
```

---

### 2.6 持久化存储规范 (Storage Schema)

使用嵌入式 SQLite 存储，默认路径为 `~/.agentstat/agentstat.db`。程序首次启动时自动建表和索引；如果检测到旧版 `~/.agentstat/records.csv`，会在事务中执行一次兼容迁移并保留原文件。当前兼容记录结构如下：

真实事件采集另外使用 `sessions`、`model_usage_events`、`tool_calls` 和 `code_changes` 表。`code_changes` 只保存文件路径、分类及 patch 增删行数，不保存源码内容。该表反映 Agent 已成功应用的变更；最终代码采纳率需要后续结合 Git commit/diff 做归因，不能直接用 patch 行数代替。

Git 提交侧使用 `git_repositories`、`git_commits` 和 `git_commit_files` 表，保存提交元数据和 numstat。两侧数据目前保持独立：Agent patch 是候选贡献，Git commit 是最终提交基线。后续归因层使用不可逆行指纹匹配，不持久化源码正文。

归因层使用 `agent_line_fingerprints` 与 `git_line_fingerprints` 保存新增非空行的 SHA-256。匹配条件为同仓库、同文件、同指纹，并要求 Git 提交时间不早于 Agent patch；重复内容按出现次数上限匹配，生成文件不进入采纳率分母。该口径只覆盖原样保留代码，属于精确但保守的下界。

```csv
session_id,timestamp,project,language,model,input_tokens,output_tokens,cost_usd,lines_suggested,lines_accepted,snippets_suggested,snippets_accepted,duration_sec
sess_20260806_001,2026-08-06 09:15:00,agent-cli,C,claude-3-5-sonnet,8500,2100,0.057000,140,128,5,5,14.20
sess_20260806_002,2026-08-06 11:30:00,agent-cli,C,gemini-1.5-flash,14200,3900,0.002000,210,185,8,7,8.50
sess_20260806_003,2026-08-06 14:05:00,web-dashboard,TypeScript,gpt-4o,18900,4200,0.089000,95,80,4,3,21.00
```

---

## 3. 集成与扩展计划 (Integration Roadmap)

1. **Phase 1: CLI 基础框架 (Core CLI & Local Storage)**
   - 实现 `agentstat record`, `summary`, `list`, `chart`, `seed` 等基础 CLI 指令。
2. **Phase 2: Embedded Web Server & Dashboard**
   - 实现内嵌 C Web Server (`server.c`) 支撑 REST API。
   - 构建基于 HTML5/Chart.js 的 Web 分析大屏，支持 `agentstat web` 启动浏览。
3. **Phase 3: IDE 插件与 Git Hook 协同 (Hooks & Auto Collector)**
   - 提供 VSCode / Cursor 插件与 Git Pre-commit Hook，实现代码采纳率零感知自动测量。
