#ifndef GIT_IMPORTER_H
#define GIT_IMPORTER_H

#include <stdbool.h>

/**
 * @brief Git导入结果结构体
 * 记录Git仓库同步过程中的统计信息
 */
typedef struct {
    long commits_scanned;  // 扫描到的Git提交总数
    long commits_imported; // 成功导入的Git提交数
    long files_imported;   // 成功导入的文件数
} GitImportResult;

/**
 * @brief 同步指定的 Git 仓库
 * @param path Git 仓库的路径
 * @param result 保存 Git 同步统计结果的指针
 * @return 同步成功返回 true，否则返回 false
 */
bool sync_git_repository(const char *path, GitImportResult *result);

#endif
