#ifndef STORAGE_H
#define STORAGE_H

#include "agentstat.h"

// 获取数据库文件的完整路径
void get_db_file_path(char *out_path, size_t max_len);

// 确保用于存储数据的目录存在，如果不存在则创建
bool ensure_storage_dir_exists(void);

// 保存一条代理执行记录到数据库或文件中
bool save_record(const AgentRecord *record);

// 从存储中加载所有记录到数组中，返回加载的记录数量
int load_all_records(AgentRecord records[], int max_records);

// 生成并插入模拟数据，用于测试和演示目的
void seed_mock_data(void);

// 初始化存储系统（如创建表结构等），返回是否初始化成功
bool initialize_storage(void);

// 加载总体使用情况的统计数据
bool load_usage_stats(AgentUsageStats *stats);

// 加载按来源分类的统计数据，返回加载的来源数量
int load_source_stats(AgentSourceStats stats[], int max_sources);

// 加载按模型分类的统计数据，返回加载的模型数量
int load_model_stats(AgentModelStats stats[], int max_models);

// 加载最近会话的统计数据，返回加载的会话数量
int load_recent_session_stats(AgentSessionStats stats[], int max_sessions);

// 加载工具使用的统计数据，返回加载的工具数量
int load_tool_stats(AgentToolStats stats[], int max_tools);

int load_project_stats(AgentProjectStats stats[], int max_projects);
int load_mcp_stats(AgentCapabilityStats stats[], int max_rows);
int load_skill_stats(AgentCapabilityStats stats[], int max_rows);
int load_period_stats(AgentPeriodStats stats[], int max_rows,
                      const char *period);

bool set_model_pricing(const char *source, const char *model, double input_rate,
                       double cache_read_rate, double cache_write_rate,
                       double output_rate);
int load_model_pricing(AgentModelStats stats[], int max_models);

// 加载代码变更的统计数据
bool load_code_stats(AgentCodeStats *stats);

// 加载与Git相关的统计数据
bool load_git_stats(AgentGitStats *stats);

// 加载代码采纳率等归属相关的统计数据
bool load_attribution_stats(AgentAttributionStats *stats);

#endif // STORAGE_H
