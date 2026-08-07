#ifndef IMPORTER_H
#define IMPORTER_H

#include <stdbool.h>

/**
 * @brief Codex导入结果结构体
 * 记录导入过程中的统计信息
 */
typedef struct {
    long lines_read;                     // 读取的行数
    long usage_events_imported;          // 成功导入的使用事件数
    long duplicate_usage_events_skipped; // 跳过的重复使用事件数
    long tool_calls_imported;            // 导入的工具调用数
    long code_changes_imported;          // 导入的代码变更数
    bool session_imported;               // 会话是否成功导入
} CodexImportResult;

/**
 * @brief Codex同步结果结构体
 * 记录同步目录时的统计信息
 */
typedef struct {
    long files_scanned;                  // 扫描的文件数
    long files_failed;                   // 处理失败的文件数
    long sessions_imported;              // 成功导入的会话数
    long lines_read;                     // 读取的总行数
    long usage_events_imported;          // 成功导入的使用事件数
    long duplicate_usage_events_skipped; // 跳过的重复使用事件数
    long tool_calls_imported;            // 导入的工具调用数
    long code_changes_imported;          // 导入的代码变更数
} CodexSyncResult;

/**
 * @brief 从指定的 JSONL 文件导入 Codex 数据
 * @param path JSONL 文件的路径
 * @param result 保存导入统计结果的指针
 * @return 导入成功返回 true，否则返回 false
 */
bool import_codex_jsonl(const char *path, CodexImportResult *result);

/**
 * @brief 同步指定目录下的所有 Codex 文件
 * @param path 包含 Codex 数据的目录路径
 * @param result 保存同步统计结果的指针
 * @return 同步成功返回 true，否则返回 false
 */
bool sync_codex_directory(const char *path, CodexSyncResult *result);

#endif
