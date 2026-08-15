#ifndef CLAUDE_IMPORTER_H
#define CLAUDE_IMPORTER_H

#include "importer.h"
#include <stdbool.h>

/**
 * @brief 从指定的 JSONL 文件导入 Claude 数据
 * @param path Claude JSONL 文件的路径
 * @param result 保存导入统计结果的指针（复用 CodexImportResult）
 * @return 导入成功返回 true，否则返回 false
 */
bool import_claude_jsonl(const char *path, CodexImportResult *result);

/**
 * @brief 同步指定目录下的所有 Claude 文件
 * @param path 包含 Claude 数据的目录路径
 * @param result 保存同步统计结果的指针（复用 CodexSyncResult）
 * @return 同步成功返回 true，否则返回 false
 */
bool sync_claude_directory(const char *path, CodexSyncResult *result);

#endif
