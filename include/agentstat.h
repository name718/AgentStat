#ifndef AGENTSTAT_H
#define AGENTSTAT_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 最大记录数和字符串长度限制
#define MAX_RECORDS 10000
#define MAX_STR_LEN 128
// 默认存储目录和数据库文件名
#define DEFAULT_STORAGE_DIR ".agentstat"
#define DEFAULT_DB_FILENAME "agentstat.db"
// 传统CSV文件名（为了向后兼容）
#define LEGACY_CSV_FILENAME "records.csv"

// 代理执行单次会话的记录结构体
typedef struct {
  char session_id[MAX_STR_LEN]; // 会话唯一标识符
  char timestamp[MAX_STR_LEN];  // 记录的时间戳
  char project[MAX_STR_LEN];    // 项目名称
  char language[MAX_STR_LEN];   // 编程语言
  char model[MAX_STR_LEN];      // 使用的AI模型名称

  long input_tokens;         // 输入（提示）使用的token数量
  long output_tokens;        // 输出（生成）的token数量
  double estimated_cost_usd; // 估计的花费（美元）

  int lines_suggested;    // 建议的代码行数
  int lines_accepted;     // 实际采纳的代码行数
  int snippets_suggested; // 建议的代码片段数量
  int snippets_accepted;  // 实际采纳的代码片段数量

  double duration_seconds; // 执行耗时（秒）
} AgentRecord;

// 代理使用的总体统计数据结构体
typedef struct {
  long total_sessions;           // 总会话次数
  long model_calls;              // 模型调用总次数
  long input_tokens;             // 输入token总数
  long cached_input_tokens;      // 缓存命中部分的输入token数
  long cache_write_input_tokens; // 写入缓存的输入token数
  long output_tokens;            // 输出token总数
  long reasoning_output_tokens;  // 推理输出的token数
  long tool_calls;               // 工具调用总数
  long mcp_calls;                // MCP（Model Context Protocol）调用总数
  long distinct_tools;           // 调用的不同工具数量
  double cache_hit_rate;         // 缓存命中率
} AgentUsageStats;

// 最大支持统计的代理来源数量
#define MAX_AGENT_SOURCES 16

// 按代理来源分类的统计数据结构体
typedef struct {
  char source[32];          // 来源名称（如终端、编辑器等）
  long sessions;            // 会话数量
  long model_calls;         // 模型调用次数
  long input_tokens;        // 输入token数
  long cached_input_tokens; // 缓存命中的输入token数
  long output_tokens;       // 输出token数
  long tool_calls;          // 工具调用次数
  long code_changes;        // 代码更改次数
} AgentSourceStats;

// 按模型分类的统计数据结构体
typedef struct {
  char source[32];               // 来源名称
  char model[MAX_STR_LEN];       // 模型名称
  long model_calls;              // 模型调用次数
  long selections;               // 选择次数
  long input_tokens;             // 输入token数
  long cached_input_tokens;      // 缓存命中的输入token数
  long cache_write_input_tokens; // 缓存写入token数
  long output_tokens;            // 输出token数
  bool pricing_configured;       // 是否有精确匹配的价格配置
  double input_rate;             // 普通输入每百万token价格
  double cache_read_rate;        // 缓存读取每百万token价格
  double cache_write_rate;       // 缓存写入每百万token价格
  double output_rate;            // 输出每百万token价格
  double estimated_cost_usd;     // 基于已配置价格计算的估算费用
} AgentModelStats;

// 单次会话的详细统计数据结构体
typedef struct {
  char session_id[256];         // 会话唯一标识符
  char source[32];              // 代理来源
  char cwd[1024];               // 当前工作目录
  char started_at[MAX_STR_LEN]; // 开始时间
  char models[512];             // 使用的模型列表
  long input_tokens;            // 输入token数
  long output_tokens;           // 输出token数
  long tool_calls;              // 工具调用次数
  long code_changes;            // 代码更改次数
} AgentSessionStats;

// 工具使用情况的统计数据结构体
typedef struct {
  char tool_name[256];   // 工具名称
  char detail_name[256]; // MCP server/tool 或 Skill 名称
  long calls;            // 调用总次数
  long mcp_calls;        // 通过MCP调用的次数
} AgentToolStats;

typedef struct {
  char project[256];
  char project_path[1024];
  long sessions;
  long sources;
  long input_tokens;
  long output_tokens;
  long tool_calls;
  long code_changes;
} AgentProjectStats;

typedef struct {
  char name[256];
  char detail[256];
  char source[32];
  long calls;
} AgentCapabilityStats;

typedef struct {
  char period_start[16];
  long sessions;
  long model_calls;
  long input_tokens;
  long cached_input_tokens;
  long cache_write_input_tokens;
  long output_tokens;
  long tool_calls;
  long mcp_calls;
  long code_changes;
  long lines_added;
  long lines_deleted;
  long priced_model_calls;
  double estimated_cost_usd;
} AgentPeriodStats;

// 代码变更相关的统计数据结构体
typedef struct {
  long change_events;             // 变更事件总数
  long files_changed;             // 变更的文件总数
  long lines_added;               // 新增的代码行数
  long lines_deleted;             // 删除的代码行数
  long business_lines_added;      // 新增的业务代码行数
  long test_lines_added;          // 新增的测试代码行数
  long documentation_lines_added; // 新增的文档代码行数
  long generated_lines_added;     // 自动生成的代码行数
  long other_lines_added;         // 其他新增行数
  double business_code_share;     // 业务代码所占比例
} AgentCodeStats;

// Git仓库相关的统计数据结构体
typedef struct {
  long repositories;              // 仓库数量
  long commits;                   // 提交数量
  long files_changed;             // 变更的文件数量
  long lines_added;               // 新增代码行数
  long lines_deleted;             // 删除代码行数
  long business_lines_added;      // 新增业务代码行数
  long test_lines_added;          // 新增测试代码行数
  long documentation_lines_added; // 新增文档代码行数
  long generated_lines_added;     // 自动生成代码行数
  long other_lines_added;         // 其他代码行数
} AgentGitStats;

// 代码贡献与归属的统计数据结构体
typedef struct {
  long candidate_lines;               // 候选（建议）代码行数
  long accepted_lines;                // 采纳的代码行数
  long business_candidate_lines;      // 业务候选代码行数
  long business_accepted_lines;       // 业务采纳代码行数
  long test_candidate_lines;          // 测试候选代码行数
  long test_accepted_lines;           // 测试采纳代码行数
  long documentation_candidate_lines; // 文档候选代码行数
  long documentation_accepted_lines;  // 文档采纳代码行数
  double acceptance_rate;             // 总体采纳率
  double business_acceptance_rate;    // 业务代码采纳率
} AgentAttributionStats;

#endif // AGENTSTAT_H
