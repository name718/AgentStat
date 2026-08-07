#include "importer.h"
#include "storage.h"
#include "sha256.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>

// 辅助函数：使用 SQLite 提取 JSON 文本数据
static bool query_text(sqlite3 *db, const char *json, const char *path, char *out, size_t size) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT COALESCE(json_extract(?1,?2),'') WHERE json_valid(?1)";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
        ok = sqlite3_step(stmt) == SQLITE_ROW;
        if (ok) snprintf(out, size, "%s", sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return ok;
}

// 辅助函数：使用 SQLite 提取 JSON 整数数据
static sqlite3_int64 query_int(sqlite3 *db, const char *json, const char *path) {
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 value = 0;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(json_extract(?1,?2),0) WHERE json_valid(?1)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

// 辅助函数：执行 SQLite 无结果集查询语句
static bool execute(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

// 从文件中读取一行 JSONL 数据，支持动态扩展内存容量
static char *read_jsonl_line(FILE *fp) {
    size_t capacity = 4096;
    size_t length = 0;
    char *line = malloc(capacity);
    if (!line) return NULL;

    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (length + 1 >= capacity) {
            size_t next_capacity = capacity * 2;
            char *resized = realloc(line, next_capacity);
            if (!resized) { free(line); return NULL; }
            line = resized;
            capacity = next_capacity;
        }
        line[length++] = (char)ch;
        if (ch == '\n') break;
    }
    if (length == 0 && ch == EOF) { free(line); return NULL; }
    line[length] = '\0';
    return line;
}

// 检查给定路径是否以 .jsonl 为后缀
static bool has_jsonl_extension(const char *path) {
    size_t length = strlen(path);
    return length >= 6 && strcmp(path + length - 6, ".jsonl") == 0;
}

// 检查路径中是否包含指定的文件系统片段
static bool path_contains_segment(const char *path, const char *segment) {
    return path && segment && strstr(path, segment) != NULL;
}

// 将文件路径标准化，尽可能解析为绝对路径，并处理父目录引用的情况
static const char *canonicalize_file_path(const char *path, char output[PATH_MAX]) {
    if (realpath(path, output)) return output;

    char ancestor[PATH_MAX];
    snprintf(ancestor, sizeof(ancestor), "%s", path);
    while (ancestor[0]) {
        char *separator = strrchr(ancestor, '/');
        if (!separator) break;
        if (separator == ancestor) ancestor[1] = '\0';
        else *separator = '\0';

        char resolved_ancestor[PATH_MAX];
        if (realpath(ancestor, resolved_ancestor)) {
            const char *suffix = path + strlen(ancestor);
            int written = snprintf(output, PATH_MAX, "%s%s", resolved_ancestor, suffix);
            return written >= 0 && written < PATH_MAX ? output : path;
        }
        if (strcmp(ancestor, "/") == 0) break;
    }
    return path;
}

// 根据文件路径及后缀名对其代码类型进行归类 (generated, test, documentation, business, other)
static const char *classify_code_path(const char *path) {
    const char *extension = strrchr(path, '.');
    if (path_contains_segment(path, "/node_modules/") ||
        path_contains_segment(path, "/vendor/") ||
        path_contains_segment(path, "/dist/") ||
        path_contains_segment(path, "/build/") ||
        path_contains_segment(path, "/generated/") ||
        path_contains_segment(path, "/Pods/") ||
        (extension && (strcmp(extension, ".lock") == 0 || strcmp(extension, ".min.js") == 0)))
        return "generated";

    if (path_contains_segment(path, "/test/") || path_contains_segment(path, "/tests/") ||
        path_contains_segment(path, "/__tests__/") || path_contains_segment(path, "/spec/") ||
        strstr(path, "_test.") || strstr(path, ".test.") || strstr(path, ".spec."))
        return "test";

    if ((extension && (strcmp(extension, ".md") == 0 || strcmp(extension, ".mdx") == 0 ||
                       strcmp(extension, ".rst") == 0 || strcmp(extension, ".txt") == 0)) ||
        path_contains_segment(path, "/docs/") || path_contains_segment(path, "/doc/"))
        return "documentation";

    if (extension) {
        const char *business_extensions[] = {
            ".c",".h",".cc",".cpp",".hpp",".m",".mm",".swift",".go",".rs",
            ".java",".kt",".kts",".py",".rb",".php",".js",".jsx",".ts",".tsx",
            ".vue",".svelte",".css",".scss",".less",".html",".sql",".sh",".zsh"
        };
        size_t count = sizeof(business_extensions) / sizeof(business_extensions[0]);
        for (size_t i = 0; i < count; i++)
            if (strcmp(extension, business_extensions[i]) == 0) return "business";
    }
    return "other";
}

// 分析统一差异 (unified diff) 格式的字符串，统计其中增加和删除的行数
static void count_diff_lines(const char *diff, long *added, long *deleted) {
    *added = 0;
    *deleted = 0;
    if (!diff) return;
    const char *line = diff;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > 0 && line[0] == '+' && !(length >= 3 && strncmp(line, "+++", 3) == 0)) (*added)++;
        if (length > 0 && line[0] == '-' && !(length >= 3 && strncmp(line, "---", 3) == 0)) (*deleted)++;
        if (!end) break;
        line = end + 1;
    }
}

// 将 diff 差异内容中新增行的代码计算出 SHA256 指纹，并将其存入代理指纹数据库中
static bool insert_agent_fingerprints(sqlite3 *db, const char *source_path, long line_number,
                                      const char *session_id, const char *timestamp,
                                      const char *file_path, const char *category, const char *diff) {
    const char *line = diff;
    long ordinal = 0;
    bool ok = true;
    while (ok && line && *line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > 1 && line[0] == '+' && !(length >= 3 && strncmp(line, "+++", 3) == 0)) {
            const unsigned char *content = (const unsigned char *)(line + 1);
            size_t content_length = length - 1;
            if (content_length > 0 && content[content_length - 1] == '\r') content_length--;
            if (content_length > 0) {
                char fingerprint[65];
                sha256_hex(content, content_length, fingerprint);
                sqlite3_stmt *stmt = NULL;
                const char *sql = "INSERT OR IGNORE INTO agent_line_fingerprints"
                    "(source_path,line_number,file_path,line_ordinal,session_id,timestamp,category,fingerprint)"
                    " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)";
                ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
                if (ok) {
                    sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt,2,line_number); sqlite3_bind_text(stmt,3,file_path,-1,SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt,4,ordinal); sqlite3_bind_text(stmt,5,session_id,-1,SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt,6,timestamp,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,7,category,-1,SQLITE_STATIC);
                    sqlite3_bind_text(stmt,8,fingerprint,-1,SQLITE_TRANSIENT); ok = sqlite3_step(stmt) == SQLITE_DONE;
                }
                sqlite3_finalize(stmt);
                ordinal++;
            }
        }
        if (!end) break;
        line = end + 1;
    }
    return ok;
}

// 解析 JSON 载荷内的代码变更详情，存入变更日志数据库，并更新相应的代码指纹
static bool import_code_changes(sqlite3 *db, const char *json, const char *source_path,
                                long line_number, const char *session_id, const char *timestamp,
                                CodexImportResult *result) {
    sqlite3_stmt *changes = NULL;
    const char *query =
        "SELECT key,COALESCE(json_extract(value,'$.type'),'update'),"
        "COALESCE(json_extract(value,'$.unified_diff'),'') "
        "FROM json_each(?1,'$.payload.changes') WHERE json_valid(?1)";
    if (sqlite3_prepare_v2(db, query, -1, &changes, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(changes, 1, json, -1, SQLITE_TRANSIENT);

    char turn_id[128] = "";
    query_text(db, json, "$.payload.turn_id", turn_id, sizeof(turn_id));
    bool ok = true;
    while (ok && sqlite3_step(changes) == SQLITE_ROW) {
        const char *raw_file_path = (const char *)sqlite3_column_text(changes, 0);
        char canonical_file_path[PATH_MAX];
        const char *file_path = canonicalize_file_path(raw_file_path, canonical_file_path);
        const char *change_type = (const char *)sqlite3_column_text(changes, 1);
        const char *diff = (const char *)sqlite3_column_text(changes, 2);
        long added, deleted;
        count_diff_lines(diff, &added, &deleted);
        const char *category = classify_code_path(file_path);

        sqlite3_stmt *insert = NULL;
        const char *sql =
            "INSERT OR IGNORE INTO code_changes"
            "(source_path,line_number,session_id,turn_id,timestamp,file_path,change_type,category,lines_added,lines_deleted) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)";
        ok = sqlite3_prepare_v2(db, sql, -1, &insert, NULL) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_text(insert,1,source_path,-1,SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert,2,line_number);
            sqlite3_bind_text(insert,3,session_id,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert,4,turn_id,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert,5,timestamp,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert,6,file_path,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert,7,change_type,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert,8,category,-1,SQLITE_STATIC);
            sqlite3_bind_int64(insert,9,added);
            sqlite3_bind_int64(insert,10,deleted);
            ok = sqlite3_step(insert) == SQLITE_DONE;
            if (ok && sqlite3_changes(db) > 0) result->code_changes_imported++;
        }
        sqlite3_finalize(insert);
        if (ok) ok = insert_agent_fingerprints(db, source_path, line_number, session_id, timestamp,
                                                file_path, category, diff);
    }
    sqlite3_finalize(changes);
    return ok;
}

// 核心函数：逐行解析 Codex 日志 (JSONL) 文件，提取并保存会话信息、Token 用量、工具调用及代码改动
bool import_codex_jsonl(const char *path, CodexImportResult *result) {
    if (!path || !result) return false;
    memset(result, 0, sizeof(*result));
    char canonical_path[PATH_MAX];
    const char *source_path = realpath(path, canonical_path) ? canonical_path : path;
    FILE *fp = fopen(source_path, "r");
    if (!fp) return false;
    if (!initialize_storage()) { fclose(fp); return false; }
    char db_path[512];
    get_db_file_path(db_path, sizeof(db_path));
    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) { fclose(fp); return false; }
    sqlite3_busy_timeout(db, 5000);
    execute(db, "PRAGMA foreign_keys=ON");

    char session_id[128] = "";
    char current_model[128] = "";
    sqlite3_int64 previous_total_usage[6] = {-1, -1, -1, -1, -1, -1};
    bool has_previous_total_usage = false;
    long line_number = 0;
    bool ok = execute(db, "BEGIN IMMEDIATE");
    char *line = NULL;
    while (ok && (line = read_jsonl_line(fp)) != NULL) {
        line_number++; result->lines_read++;
        char outer_type[64] = "", payload_type[64] = "", timestamp[128] = "";
        if (!query_text(db, line, "$.type", outer_type, sizeof(outer_type))) {
            free(line);
            line = NULL;
            continue;
        }
        query_text(db, line, "$.payload.type", payload_type, sizeof(payload_type));
        query_text(db, line, "$.timestamp", timestamp, sizeof(timestamp));

        if (strcmp(outer_type, "session_meta") == 0) {
            char cwd[1024] = "", provider[128] = "";
            query_text(db, line, "$.payload.id", session_id, sizeof(session_id));
            if (!session_id[0]) query_text(db, line, "$.payload.session_id", session_id, sizeof(session_id));
            query_text(db, line, "$.payload.cwd", cwd, sizeof(cwd));
            query_text(db, line, "$.payload.model_provider", provider, sizeof(provider));
            sqlite3_stmt *stmt = NULL;
            const char *sql = "INSERT INTO sessions(session_id,source,source_path,cwd,started_at,model_provider) "
                "VALUES(?1,'codex',?2,?3,?4,?5) ON CONFLICT(session_id) DO UPDATE SET "
                "source_path=excluded.source_path,cwd=excluded.cwd,model_provider=excluded.model_provider";
            ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_text(stmt,1,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,source_path,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt,3,cwd,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,timestamp,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt,5,provider,-1,SQLITE_TRANSIENT); ok = sqlite3_step(stmt) == SQLITE_DONE;
            }
            sqlite3_finalize(stmt); result->session_imported = ok;
        } else if (strcmp(outer_type, "turn_context") == 0) {
            query_text(db, line, "$.payload.model", current_model, sizeof(current_model));
        } else if (session_id[0] && strcmp(outer_type, "event_msg") == 0 && strcmp(payload_type, "token_count") == 0) {
            const char *names[] = {"input_tokens","cached_input_tokens","cache_write_input_tokens","output_tokens","reasoning_output_tokens","total_tokens"};
            sqlite3_int64 total_usage[6];
            sqlite3_int64 cumulative_total_tokens = query_int(db, line, "$.payload.info.total_token_usage.total_tokens");
            bool has_total_snapshot = cumulative_total_tokens > 0;
            bool duplicate_snapshot = has_total_snapshot && has_previous_total_usage;
            for (int i = 0; i < 6; i++) {
                char total_path[160];
                snprintf(total_path, sizeof(total_path), "$.payload.info.total_token_usage.%s", names[i]);
                total_usage[i] = query_int(db, line, total_path);
                if (!has_total_snapshot || !has_previous_total_usage || total_usage[i] != previous_total_usage[i]) duplicate_snapshot = false;
            }
            if (duplicate_snapshot) {
                result->duplicate_usage_events_skipped++;
                free(line);
                line = NULL;
                continue;
            }
            if (has_total_snapshot) {
                memcpy(previous_total_usage, total_usage, sizeof(total_usage));
                has_previous_total_usage = true;
            }

            sqlite3_stmt *stmt = NULL;
            const char *sql = "INSERT OR IGNORE INTO model_usage_events "
                "(source_path,line_number,session_id,timestamp,model,input_tokens,cached_input_tokens,"
                "cache_write_input_tokens,output_tokens,reasoning_output_tokens,total_tokens) "
                "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)";
            ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(stmt,2,line_number);
                sqlite3_bind_text(stmt,3,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,timestamp,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt,5,current_model,-1,SQLITE_TRANSIENT);
                const char *base = "$.payload.info.last_token_usage.";
                for (int i=0;i<6;i++) { char p[128]; snprintf(p,sizeof(p),"%s%s",base,names[i]); sqlite3_bind_int64(stmt,6+i,query_int(db,line,p)); }
                ok = sqlite3_step(stmt) == SQLITE_DONE; if (ok && sqlite3_changes(db)>0) result->usage_events_imported++;
            }
            sqlite3_finalize(stmt);
        } else if (session_id[0] && strcmp(outer_type, "response_item") == 0 &&
                  (strcmp(payload_type, "custom_tool_call") == 0 || strcmp(payload_type, "function_call") == 0 || strcmp(payload_type, "mcp_tool_call") == 0)) {
            char name[256] = ""; query_text(db,line,"$.payload.name",name,sizeof(name));
            if (!name[0]) query_text(db,line,"$.payload.tool",name,sizeof(name));
            int is_mcp = strcmp(payload_type,"mcp_tool_call") == 0 || strstr(name,"mcp__") == name;
            sqlite3_stmt *stmt = NULL;
            ok = sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO tool_calls "
                "(source_path,line_number,session_id,timestamp,tool_name,call_type,is_mcp,detail_name) "
                "VALUES(?1,?2,?3,?4,?5,?6,?7,'')", -1, &stmt, NULL) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(stmt,2,line_number);
                sqlite3_bind_text(stmt,3,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,timestamp,-1,SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt,5,name,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,6,payload_type,-1,SQLITE_TRANSIENT); sqlite3_bind_int(stmt,7,is_mcp);
                ok = sqlite3_step(stmt) == SQLITE_DONE; if (ok && sqlite3_changes(db)>0) result->tool_calls_imported++;
            }
            sqlite3_finalize(stmt);
        } else if (session_id[0] && strcmp(outer_type, "event_msg") == 0 &&
                   strcmp(payload_type, "patch_apply_end") == 0 &&
                   query_int(db, line, "$.payload.success") != 0) {
            ok = import_code_changes(db, line, source_path, line_number, session_id, timestamp, result);
        }
        free(line);
        line = NULL;
    }
    free(line);
    if (ok) ok = execute(db, "COMMIT"); else execute(db, "ROLLBACK");
    sqlite3_close(db); fclose(fp); return ok;
}

// 递归遍历指定目录，查找所有 .jsonl 文件并调用解析函数进行同步导入
static bool sync_directory_recursive(const char *path, CodexSyncResult *result) {
    DIR *directory = opendir(path);
    if (!directory) return false;
    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child_path[PATH_MAX];
        int written = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child_path)) { result->files_failed++; ok = false; continue; }
        struct stat info;
        if (lstat(child_path, &info) != 0) { result->files_failed++; ok = false; continue; }
        if (S_ISDIR(info.st_mode)) {
            if (!sync_directory_recursive(child_path, result)) ok = false;
        } else if (S_ISREG(info.st_mode) && has_jsonl_extension(child_path)) {
            CodexImportResult imported;
            result->files_scanned++;
            if (!import_codex_jsonl(child_path, &imported)) {
                result->files_failed++;
                ok = false;
                continue;
            }
            if (imported.session_imported) result->sessions_imported++;
            result->lines_read += imported.lines_read;
            result->usage_events_imported += imported.usage_events_imported;
            result->duplicate_usage_events_skipped += imported.duplicate_usage_events_skipped;
            result->tool_calls_imported += imported.tool_calls_imported;
            result->code_changes_imported += imported.code_changes_imported;
        }
    }
    closedir(directory);
    return ok;
}

// 公开接口：同步传入目录中的 Codex JSONL 日志文件
bool sync_codex_directory(const char *path, CodexSyncResult *result) {
    if (!path || !result) return false;
    memset(result, 0, sizeof(*result));
    char canonical_path[PATH_MAX];
    const char *source_path = realpath(path, canonical_path) ? canonical_path : path;
    return sync_directory_recursive(source_path, result);
}
