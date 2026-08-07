#ifndef ADAPTER_UTILS_H
#define ADAPTER_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sqlite3.h>

/**
 * @brief 从文件指针中读取一行 JSONL 数据
 * @param fp 文件指针
 * @return 成功读取返回动态分配的字符串，需要调用者释放内存；失败返回 NULL
 */
char *adapter_read_jsonl_line(FILE *fp);

/**
 * @brief 从 JSON 字符串中提取指定路径的文本数据，利用 SQLite JSON 函数解析
 * @param db SQLite 数据库连接
 * @param json 要解析的 JSON 字符串
 * @param path JSON 路径表达式
 * @param out 输出缓冲区，用于存放提取到的文本
 * @param size 输出缓冲区的大小
 * @return 成功提取返回 true，否则返回 false
 */
bool adapter_json_text(sqlite3 *db, const char *json, const char *path, char *out, size_t size);

/**
 * @brief 从 JSON 字符串中提取指定路径的整数数据
 * @param db SQLite 数据库连接
 * @param json 要解析的 JSON 字符串
 * @param path JSON 路径表达式
 * @return 提取到的整数值，如果提取失败返回 0
 */
sqlite3_int64 adapter_json_int(sqlite3 *db, const char *json, const char *path);
/**
 * @brief 在给定的 SQLite 数据库上执行 SQL 语句
 * @param db SQLite 数据库连接
 * @param sql 要执行的 SQL 语句
 * @return 成功执行返回 true，否则返回 false
 */
bool adapter_execute(sqlite3 *db, const char *sql);

/**
 * @brief 打开并初始化用于存储数据的 SQLite 数据库
 * @param db 用于保存打开的 SQLite 数据库连接指针的地址
 * @return 成功打开返回 true，否则返回 false
 */
bool adapter_open_database(sqlite3 **db);

/**
 * @brief 获取规范化的文件绝对路径
 * @param path 原始路径
 * @param output 用于保存规范化路径的输出缓冲区
 * @param size 输出缓冲区的大小
 * @return 规范化后的路径指针
 */
const char *adapter_canonical_file(const char *path, char *output, size_t size);

/**
 * @brief 对给定路径进行分类，确定文件属于哪种项目/来源
 * @param path 文件路径
 * @return 分类字符串，例如 "claude", "antigravity" 等
 */
const char *adapter_classify_path(const char *path);
/**
 * @brief 插入或更新会话记录
 * @param db SQLite 数据库连接
 * @param session_id 会话的唯一标识符
 * @param source 数据来源 (例如 Codex, Claude)
 * @param source_path 来源文件路径
 * @param cwd 当前工作目录
 * @param started_at 会话开始的时间戳
 * @param provider LLM 供应商名称
 * @param inserted 用于返回是否发生了实际插入操作的标志指针
 * @return 操作成功返回 true，否则返回 false
 */
bool adapter_upsert_session(sqlite3 *db, const char *session_id, const char *source,
                            const char *source_path, const char *cwd, const char *started_at,
                            const char *provider, bool *inserted);

/**
 * @brief 插入工具调用记录
 * @param db SQLite 数据库连接
 * @param source_path 来源文件路径
 * @param event_number 事件序号
 * @param session_id 关联的会话标识符
 * @param timestamp 调用发生的时间戳
 * @param name 工具名称
 * @param call_type 工具调用类型 (例如 function)
 * @param is_mcp 是否为 MCP 工具调用
 * @param inserted 用于返回是否成功插入的标志指针
 * @return 操作成功返回 true，否则返回 false
 */
bool adapter_insert_tool(sqlite3 *db, const char *source_path, long event_number,
                         const char *session_id, const char *timestamp, const char *name,
                         const char *call_type, bool is_mcp, bool *inserted);

/**
 * @brief 插入模型选择记录
 * @param db SQLite 数据库连接
 * @param source_path 来源文件路径
 * @param event_number 事件序号
 * @param session_id 关联的会话标识符
 * @param timestamp 选择发生的时间戳
 * @param model 选中的模型名称
 * @param inserted 用于返回是否成功插入的标志指针
 * @return 操作成功返回 true，否则返回 false
 */
bool adapter_insert_model_selection(sqlite3 *db, const char *source_path, long event_number,
                                    const char *session_id, const char *timestamp,
                                    const char *model, bool *inserted);

/**
 * @brief 记录文件代码变更内容
 * @param db SQLite 数据库连接
 * @param source_path 来源文件路径
 * @param event_number 事件序号
 * @param session_id 关联的会话标识符
 * @param turn_id 会话轮次标识符
 * @param timestamp 变更发生的时间戳
 * @param file_path 发生变更的文件路径
 * @param change_type 变更类型 (如 add, modify, delete)
 * @param old_text 变更前的旧文本
 * @param new_text 变更后的新文本
 * @param inserted 用于返回是否成功插入的标志指针
 * @return 操作成功返回 true，否则返回 false
 */
bool adapter_record_text_change(sqlite3 *db, const char *source_path, long event_number,
                                const char *session_id, const char *turn_id,
                                const char *timestamp, const char *file_path,
                                const char *change_type, const char *old_text,
                                const char *new_text, bool *inserted);

#endif
