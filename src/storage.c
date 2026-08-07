/**
 * @file storage.c
 * @brief 数据存储与统计模块实现
 * 
 * 负责管理 SQLite 数据库的初始化、数据结构的创建（表与索引）、旧数据的迁移、
 * 以及提供各维度的读取与写入接口，如使用量统计、会话记录、模型统计和代码归因分析等。
 */
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sqlite3.h>

/**
 * @brief 获取存储目录的路径
 * 
 * 优先检查环境变量 AGENTSTAT_DATA_DIR，如果存在则使用该环境变量指定的路径。
 * 否则默认使用用户主目录 (HOME) 下的 DEFAULT_STORAGE_DIR。
 * 
 * @param out_path 输出路径的缓冲区
 * @param max_len 缓冲区的最大长度
 */
static void get_storage_dir_path(char *out_path, size_t max_len) {
    const char *override = getenv("AGENTSTAT_DATA_DIR");
    if (override && override[0] != '\0') {
        snprintf(out_path, max_len, "%s", override);
        return;
    }

    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(out_path, max_len, "%s/%s", home, DEFAULT_STORAGE_DIR);
}

/**
 * @brief 获取数据库文件的完整路径
 * 
 * 结合存储目录路径和默认的数据库文件名，生成数据库文件的绝对路径。
 * 
 * @param out_path 输出路径的缓冲区
 * @param max_len 缓冲区的最大长度
 */
void get_db_file_path(char *out_path, size_t max_len) {
    char dir_path[512];
    get_storage_dir_path(dir_path, sizeof(dir_path));
    snprintf(out_path, max_len, "%s/%s", dir_path, DEFAULT_DB_FILENAME);
}

/**
 * @brief 确保存储目录存在
 * 
 * 检查存储目录是否存在，如果不存在则尝试创建该目录。
 * 兼容 Windows 和类 Unix 系统。
 * 
 * @return true 目录存在或创建成功
 * @return false 目录创建失败
 */
bool ensure_storage_dir_exists(void) {
    char dir_path[512];
    get_storage_dir_path(dir_path, sizeof(dir_path));
    
    struct stat st = {0};
    if (stat(dir_path, &st) == -1) {
#if defined(_WIN32)
        if (mkdir(dir_path) != 0) return false;
#else
        if (mkdir(dir_path, 0755) != 0) return false;
#endif
    }
    return true;
}

/**
 * @brief 执行 SQL 语句的通用辅助函数
 * 
 * 调用 sqlite3_exec 执行传入的 SQL 语句。如果执行失败，会打印错误信息到标准错误流。
 * 
 * @param db SQLite 数据库连接句柄
 * @param sql 待执行的 SQL 语句字符串
 * @return true 执行成功
 * @return false 执行失败
 */
static bool execute_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &error) != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s\n", error ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        return false;
    }
    return true;
}

static bool table_has_column(sqlite3 *db, const char *table, const char *column) {
    char sql[256];
    sqlite3_stmt *stmt = NULL;
    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name && strcmp(name, column) == 0) { found = true; break; }
    }
    sqlite3_finalize(stmt);
    return found;
}

/**
 * @brief 将 AgentRecord 结构体的数据绑定到 SQL 插入语句的参数中
 * 
 * 按照 agent_records 表的字段顺序，依次将结构体中的对应数据绑定到 sqlite3_stmt 对象。
 * 
 * @param stmt SQLite 预处理语句对象
 * @param record 待绑定的 AgentRecord 结构体指针
 * @return true 绑定成功
 * @return false 绑定失败
 */
static bool bind_record(sqlite3_stmt *stmt, const AgentRecord *record) {
    return sqlite3_bind_text(stmt, 1, record->session_id, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_text(stmt, 2, record->timestamp, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_text(stmt, 3, record->project, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_text(stmt, 4, record->language, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_text(stmt, 5, record->model, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_int64(stmt, 6, record->input_tokens) == SQLITE_OK &&
           sqlite3_bind_int64(stmt, 7, record->output_tokens) == SQLITE_OK &&
           sqlite3_bind_double(stmt, 8, record->estimated_cost_usd) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 9, record->lines_suggested) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 10, record->lines_accepted) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 11, record->snippets_suggested) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 12, record->snippets_accepted) == SQLITE_OK &&
           sqlite3_bind_double(stmt, 13, record->duration_seconds) == SQLITE_OK;
}

/**
 * @brief 导入旧版 CSV 格式的数据到 SQLite 数据库中
 * 
 * 检查 storage_metadata 表判断是否已导入过。如果未导入，
 * 则读取旧版的 CSV 文件，解析每一行数据并插入到 agent_records 表中。
 * 操作在事务中进行，确保数据一致性。导入成功后记录 metadata 标记。
 * 
 * @param db SQLite 数据库连接句柄
 * @return true 导入成功或已经导入过
 * @return false 导入失败
 */
static bool import_legacy_csv(sqlite3 *db) {
    sqlite3_stmt *check_stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT 1 FROM storage_metadata WHERE key = 'legacy_csv_imported'",
            -1, &check_stmt, NULL) != SQLITE_OK) return false;
    bool already_imported = sqlite3_step(check_stmt) == SQLITE_ROW;
    sqlite3_finalize(check_stmt);
    if (already_imported) return true;

    char dir_path[512];
    char csv_path[1024];
    get_storage_dir_path(dir_path, sizeof(dir_path));
    snprintf(csv_path, sizeof(csv_path), "%s/%s", dir_path, LEGACY_CSV_FILENAME);
    FILE *fp = fopen(csv_path, "r");

    if (!execute_sql(db, "BEGIN IMMEDIATE TRANSACTION")) return false;
    bool ok = true;
    if (fp) {
        char line[2048];
        sqlite3_stmt *insert_stmt = NULL;
        const char *insert_sql =
            "INSERT OR IGNORE INTO agent_records ("
            "session_id,timestamp,project,language,model,input_tokens,output_tokens,"
            "cost_usd,lines_suggested,lines_accepted,snippets_suggested,"
            "snippets_accepted,duration_sec) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";
        if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, NULL) != SQLITE_OK) {
            ok = false;
        } else {
            (void)fgets(line, sizeof(line), fp);
            while (ok && fgets(line, sizeof(line), fp)) {
                AgentRecord record = {0};
                int parsed = sscanf(line,
                    "%127[^,],%127[^,],%127[^,],%127[^,],%127[^,],%ld,%ld,%lf,%d,%d,%d,%d,%lf",
                    record.session_id, record.timestamp, record.project, record.language,
                    record.model, &record.input_tokens, &record.output_tokens,
                    &record.estimated_cost_usd, &record.lines_suggested,
                    &record.lines_accepted, &record.snippets_suggested,
                    &record.snippets_accepted, &record.duration_seconds);
                if (parsed < 12) continue;
                if (!bind_record(insert_stmt, &record) || sqlite3_step(insert_stmt) != SQLITE_DONE) ok = false;
                sqlite3_reset(insert_stmt);
                sqlite3_clear_bindings(insert_stmt);
            }
        }
        sqlite3_finalize(insert_stmt);
        fclose(fp);
    }

    if (ok) {
        ok = execute_sql(db,
            "INSERT OR REPLACE INTO storage_metadata(key,value) "
            "VALUES('legacy_csv_imported',datetime('now'))");
    }
    if (ok) return execute_sql(db, "COMMIT");
    execute_sql(db, "ROLLBACK");
    return false;
}

/**
 * @brief 打开 SQLite 数据库并初始化所有的表结构
 * 
 * 1. 确保目录存在并打开数据库文件。
 * 2. 设置 PRAGMA 选项（WAL模式、外键支持等）。
 * 3. 创建所有必需的表结构（如 agent_records, sessions, model_usage_events 等）和索引。
 * 4. 尝试导入旧版的 CSV 数据。
 * 
 * @param db 指向 SQLite 数据库句柄指针的指针，用于返回打开的数据库连接
 * @return true 数据库打开和初始化成功
 * @return false 数据库打开或初始化失败
 */
static bool open_database(sqlite3 **db) {
    if (!ensure_storage_dir_exists()) return false;
    char db_path[512];
    get_db_file_path(db_path, sizeof(db_path));
    if (sqlite3_open(db_path, db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open SQLite database: %s\n", sqlite3_errmsg(*db));
        sqlite3_close(*db);
        *db = NULL;
        return false;
    }
    sqlite3_busy_timeout(*db, 5000);

    const char *schema =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS agent_records ("
        "session_id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, project TEXT NOT NULL,"
        "language TEXT NOT NULL, model TEXT NOT NULL, input_tokens INTEGER NOT NULL DEFAULT 0,"
        "output_tokens INTEGER NOT NULL DEFAULT 0, cost_usd REAL NOT NULL DEFAULT 0,"
        "lines_suggested INTEGER NOT NULL DEFAULT 0, lines_accepted INTEGER NOT NULL DEFAULT 0,"
        "snippets_suggested INTEGER NOT NULL DEFAULT 0, snippets_accepted INTEGER NOT NULL DEFAULT 0,"
        "duration_sec REAL NOT NULL DEFAULT 0, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_agent_records_timestamp ON agent_records(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_agent_records_project ON agent_records(project);"
        "CREATE INDEX IF NOT EXISTS idx_agent_records_model ON agent_records(model);"
        "CREATE TABLE IF NOT EXISTS storage_metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);";
    const char *event_schema =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "session_id TEXT PRIMARY KEY, source TEXT NOT NULL, source_path TEXT NOT NULL,"
        "cwd TEXT, started_at TEXT, model_provider TEXT, imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS model_usage_events ("
        "source_path TEXT NOT NULL, line_number INTEGER NOT NULL, session_id TEXT NOT NULL,"
        "timestamp TEXT, model TEXT, input_tokens INTEGER NOT NULL DEFAULT 0,"
        "cached_input_tokens INTEGER NOT NULL DEFAULT 0, cache_write_input_tokens INTEGER NOT NULL DEFAULT 0,"
        "output_tokens INTEGER NOT NULL DEFAULT 0, reasoning_output_tokens INTEGER NOT NULL DEFAULT 0,"
        "total_tokens INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source_path,line_number),"
        "FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_usage_session ON model_usage_events(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_usage_timestamp ON model_usage_events(timestamp);"
        "CREATE TABLE IF NOT EXISTS tool_calls ("
        "source_path TEXT NOT NULL, line_number INTEGER NOT NULL, session_id TEXT NOT NULL,"
        "timestamp TEXT, tool_name TEXT NOT NULL, call_type TEXT NOT NULL, is_mcp INTEGER NOT NULL DEFAULT 0,"
        "detail_name TEXT NOT NULL DEFAULT '',"
        "PRIMARY KEY(source_path,line_number), FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_tool_session ON tool_calls(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_tool_name ON tool_calls(tool_name);"
        "CREATE TABLE IF NOT EXISTS model_selection_events ("
        "source_path TEXT NOT NULL,line_number INTEGER NOT NULL,session_id TEXT NOT NULL,"
        "timestamp TEXT,model TEXT NOT NULL,PRIMARY KEY(source_path,line_number),"
        "FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_model_selection_session ON model_selection_events(session_id);";
    const char *pricing_schema =
        "CREATE TABLE IF NOT EXISTS model_pricing ("
        "source TEXT NOT NULL,model TEXT NOT NULL,input_rate REAL NOT NULL,"
        "cache_read_rate REAL NOT NULL,cache_write_rate REAL NOT NULL,output_rate REAL NOT NULL,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(source,model)"
        ");";
    const char *code_schema =
        "CREATE TABLE IF NOT EXISTS code_changes ("
        "source_path TEXT NOT NULL, line_number INTEGER NOT NULL, session_id TEXT NOT NULL,"
        "turn_id TEXT, timestamp TEXT, file_path TEXT NOT NULL, change_type TEXT NOT NULL,"
        "category TEXT NOT NULL, lines_added INTEGER NOT NULL DEFAULT 0,"
        "lines_deleted INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source_path,line_number,file_path),"
        "FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_code_session ON code_changes(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_code_timestamp ON code_changes(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_code_category ON code_changes(category);";
    const char *git_schema =
        "CREATE TABLE IF NOT EXISTS git_repositories ("
        "repo_path TEXT PRIMARY KEY, synced_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS git_commits ("
        "repo_path TEXT NOT NULL, commit_hash TEXT NOT NULL, authored_at TEXT NOT NULL,"
        "author_name TEXT, PRIMARY KEY(repo_path,commit_hash),"
        "FOREIGN KEY(repo_path) REFERENCES git_repositories(repo_path) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_git_commits_time ON git_commits(authored_at);"
        "CREATE TABLE IF NOT EXISTS git_commit_files ("
        "repo_path TEXT NOT NULL, commit_hash TEXT NOT NULL, file_path TEXT NOT NULL,"
        "category TEXT NOT NULL, lines_added INTEGER NOT NULL DEFAULT 0,"
        "lines_deleted INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(repo_path,commit_hash,file_path),"
        "FOREIGN KEY(repo_path,commit_hash) REFERENCES git_commits(repo_path,commit_hash) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_git_files_category ON git_commit_files(category);";
    const char *attribution_schema =
        "CREATE TABLE IF NOT EXISTS agent_line_fingerprints ("
        "source_path TEXT NOT NULL, line_number INTEGER NOT NULL, file_path TEXT NOT NULL,"
        "line_ordinal INTEGER NOT NULL, session_id TEXT NOT NULL, timestamp TEXT,"
        "category TEXT NOT NULL, fingerprint TEXT NOT NULL,"
        "PRIMARY KEY(source_path,line_number,file_path,line_ordinal),"
        "FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_agent_fingerprint ON agent_line_fingerprints(file_path,fingerprint);"
        "CREATE TABLE IF NOT EXISTS git_line_fingerprints ("
        "repo_path TEXT NOT NULL, commit_hash TEXT NOT NULL, file_path TEXT NOT NULL,"
        "line_ordinal INTEGER NOT NULL, category TEXT NOT NULL, fingerprint TEXT NOT NULL,"
        "PRIMARY KEY(repo_path,commit_hash,file_path,line_ordinal),"
        "FOREIGN KEY(repo_path,commit_hash) REFERENCES git_commits(repo_path,commit_hash) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_git_fingerprint ON git_line_fingerprints(repo_path,file_path,fingerprint);";
    if (!execute_sql(*db, schema) || !execute_sql(*db, event_schema) ||
        !execute_sql(*db, code_schema) || !execute_sql(*db, git_schema) ||
        !execute_sql(*db, attribution_schema) || !execute_sql(*db, pricing_schema) ||
        !import_legacy_csv(*db)) {
        sqlite3_close(*db);
        *db = NULL;
        return false;
    }
    if (!table_has_column(*db, "tool_calls", "detail_name") &&
        !execute_sql(*db, "ALTER TABLE tool_calls ADD COLUMN detail_name TEXT NOT NULL DEFAULT ''")) {
        sqlite3_close(*db);
        *db = NULL;
        return false;
    }
    return true;
}

/**
 * @brief 初始化存储系统
 * 
 * 封装了打开数据库（并触发建表和初始化逻辑）及关闭数据库的过程，用于系统启动时的自检和初始化。
 * 
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initialize_storage(void) {
    sqlite3 *db = NULL;
    if (!open_database(&db)) return false;
    sqlite3_close(db);
    return true;
}

/**
 * @brief 加载全局的 Agent 使用情况统计数据
 * 
 * 从 model_usage_events 和 tool_calls 等表中汇总查询，计算总会话数、模型调用次数、Token 消耗、工具调用等全局统计信息。
 * 不统计标记为 '<synthetic>' 的虚拟模型数据。
 * 
 * @param stats 用于输出统计结果的 AgentUsageStats 结构体指针
 * @return true 加载成功
 * @return false 加载失败
 */
bool load_usage_stats(AgentUsageStats *stats) {
    if (!stats) return false;
    memset(stats, 0, sizeof(*stats));
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return false;
    const char *sql =
        "SELECT (SELECT COUNT(*) FROM sessions),"
        "COUNT(*),COALESCE(SUM(input_tokens),0),COALESCE(SUM(cached_input_tokens),0),"
        "COALESCE(SUM(cache_write_input_tokens),0),COALESCE(SUM(output_tokens),0),"
        "COALESCE(SUM(reasoning_output_tokens),0),"
        "(SELECT COUNT(*) FROM tool_calls),(SELECT COUNT(*) FROM tool_calls WHERE is_mcp=1),"
        "(SELECT COUNT(DISTINCT tool_name) FROM tool_calls) FROM model_usage_events WHERE model<>'<synthetic>'";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW;
    if (ok) {
        stats->total_sessions = (long)sqlite3_column_int64(stmt, 0);
        stats->model_calls = (long)sqlite3_column_int64(stmt, 1);
        stats->input_tokens = (long)sqlite3_column_int64(stmt, 2);
        stats->cached_input_tokens = (long)sqlite3_column_int64(stmt, 3);
        stats->cache_write_input_tokens = (long)sqlite3_column_int64(stmt, 4);
        stats->output_tokens = (long)sqlite3_column_int64(stmt, 5);
        stats->reasoning_output_tokens = (long)sqlite3_column_int64(stmt, 6);
        stats->tool_calls = (long)sqlite3_column_int64(stmt, 7);
        stats->mcp_calls = (long)sqlite3_column_int64(stmt, 8);
        stats->distinct_tools = (long)sqlite3_column_int64(stmt, 9);
        if (stats->input_tokens > 0)
            stats->cache_hit_rate = (double)stats->cached_input_tokens * 100.0 / (double)stats->input_tokens;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

/**
 * @brief 加载按数据来源 (source) 分组的使用统计数据
 * 
 * 统计每种来源的会话数量、模型调用次数、Token 使用量、工具调用量以及代码修改情况。
 * 
 * @param stats 输出统计结果的数组
 * @param max_sources 数组的最大容量，防止越界
 * @return int 成功返回加载的来源记录数量，失败返回 -1
 */
int load_source_stats(AgentSourceStats stats[], int max_sources) {
    if (!stats || max_sources <= 0) return -1;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return -1;
    const char *sql =
        "SELECT s.source,COUNT(*),"
        "(SELECT COUNT(*) FROM model_usage_events u JOIN sessions us ON us.session_id=u.session_id WHERE us.source=s.source AND u.model<>'<synthetic>'),"
        "(SELECT COALESCE(SUM(u.input_tokens),0) FROM model_usage_events u JOIN sessions us ON us.session_id=u.session_id WHERE us.source=s.source AND u.model<>'<synthetic>'),"
        "(SELECT COALESCE(SUM(u.cached_input_tokens),0) FROM model_usage_events u JOIN sessions us ON us.session_id=u.session_id WHERE us.source=s.source AND u.model<>'<synthetic>'),"
        "(SELECT COALESCE(SUM(u.output_tokens),0) FROM model_usage_events u JOIN sessions us ON us.session_id=u.session_id WHERE us.source=s.source AND u.model<>'<synthetic>'),"
        "(SELECT COUNT(*) FROM tool_calls t JOIN sessions ts ON ts.session_id=t.session_id WHERE ts.source=s.source),"
        "(SELECT COUNT(DISTINCT c.source_path || ':' || c.line_number) FROM code_changes c JOIN sessions cs ON cs.session_id=c.session_id WHERE cs.source=s.source) "
        "FROM sessions s GROUP BY s.source ORDER BY s.source";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    int count = 0;
    while (count < max_sources && sqlite3_step(stmt) == SQLITE_ROW) {
        memset(&stats[count], 0, sizeof(stats[count]));
        snprintf(stats[count].source, sizeof(stats[count].source), "%s", sqlite3_column_text(stmt, 0));
        stats[count].sessions = (long)sqlite3_column_int64(stmt, 1);
        stats[count].model_calls = (long)sqlite3_column_int64(stmt, 2);
        stats[count].input_tokens = (long)sqlite3_column_int64(stmt, 3);
        stats[count].cached_input_tokens = (long)sqlite3_column_int64(stmt, 4);
        stats[count].output_tokens = (long)sqlite3_column_int64(stmt, 5);
        stats[count].tool_calls = (long)sqlite3_column_int64(stmt, 6);
        stats[count].code_changes = (long)sqlite3_column_int64(stmt, 7);
        count++;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

/**
 * @brief 加载各个模型的使用情况统计数据
 * 
 * 从 model_usage_events 和 model_selection_events 表中提取每个来源和模型的使用信息，
 * 统计总调用次数、选择次数和各类 Token 消耗量，并按照使用量倒序排列。
 * 
 * @param stats 输出统计结果的数组
 * @param max_models 数组的最大容量
 * @return int 成功返回加载的模型记录数量，失败返回 -1
 */
int load_model_stats(AgentModelStats stats[], int max_models) {
    if (!stats || max_models <= 0) return -1;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return -1;
    const char *sql =
        "WITH raw AS ("
        " SELECT s.source,u.model,COUNT(*) model_calls,0 selections,"
        " SUM(u.input_tokens) input_tokens,SUM(u.cached_input_tokens) cached_input_tokens,"
        " SUM(u.cache_write_input_tokens) cache_write_input_tokens,SUM(u.output_tokens) output_tokens"
        " FROM model_usage_events u JOIN sessions s ON s.session_id=u.session_id"
        " WHERE u.model<>'' AND u.model<>'<synthetic>' GROUP BY s.source,u.model"
        " UNION ALL"
        " SELECT s.source,m.model,0,COUNT(*),0,0,0,0 FROM model_selection_events m"
        " JOIN sessions s ON s.session_id=m.session_id WHERE m.model<>'' GROUP BY s.source,m.model"
        "), totals AS (SELECT source,model,SUM(model_calls) model_calls,SUM(selections) selections,"
        "SUM(input_tokens) input_tokens,SUM(cached_input_tokens) cached_input_tokens,"
        "SUM(cache_write_input_tokens) cache_write_input_tokens,SUM(output_tokens) output_tokens "
        "FROM raw GROUP BY source,model) "
        "SELECT t.source,t.model,t.model_calls,t.selections,t.input_tokens,t.cached_input_tokens,"
        "t.cache_write_input_tokens,t.output_tokens,p.input_rate,p.cache_read_rate,p.cache_write_rate,p.output_rate "
        "FROM totals t LEFT JOIN model_pricing p ON p.source=t.source AND p.model=t.model"
        " ORDER BY t.input_tokens+t.output_tokens DESC,t.selections DESC,t.source,t.model";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) { sqlite3_close(db); return -1; }
    int count = 0;
    while (count < max_models && sqlite3_step(stmt) == SQLITE_ROW) {
        AgentModelStats *row = &stats[count++];
        memset(row, 0, sizeof(*row));
        snprintf(row->source, sizeof(row->source), "%s", sqlite3_column_text(stmt,0));
        snprintf(row->model, sizeof(row->model), "%s", sqlite3_column_text(stmt,1));
        row->model_calls = (long)sqlite3_column_int64(stmt,2);
        row->selections = (long)sqlite3_column_int64(stmt,3);
        row->input_tokens = (long)sqlite3_column_int64(stmt,4);
        row->cached_input_tokens = (long)sqlite3_column_int64(stmt,5);
        row->cache_write_input_tokens = (long)sqlite3_column_int64(stmt,6);
        row->output_tokens = (long)sqlite3_column_int64(stmt,7);
        row->pricing_configured = sqlite3_column_type(stmt,8) != SQLITE_NULL;
        if (row->pricing_configured) {
            row->input_rate=sqlite3_column_double(stmt,8);
            row->cache_read_rate=sqlite3_column_double(stmt,9);
            row->cache_write_rate=sqlite3_column_double(stmt,10);
            row->output_rate=sqlite3_column_double(stmt,11);
            long normal_input=row->input_tokens-row->cached_input_tokens-row->cache_write_input_tokens;
            if(normal_input<0) normal_input=0;
            row->estimated_cost_usd=((double)normal_input*row->input_rate+
                (double)row->cached_input_tokens*row->cache_read_rate+
                (double)row->cache_write_input_tokens*row->cache_write_rate+
                (double)row->output_tokens*row->output_rate)/1000000.0;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

/**
 * @brief 加载最近的会话统计数据
 * 
 * 查询最新的若干个会话记录，并关联查询每个会话使用过的模型列表、输入输出 Token 总量、
 * 工具调用次数以及代码修改行数。按会话开始时间降序排列。
 * 
 * @param stats 输出统计结果的数组
 * @param max_sessions 期望查询的最大会话数
 * @return int 成功返回加载的会话记录数量，失败返回 -1
 */
int load_recent_session_stats(AgentSessionStats stats[], int max_sessions) {
    if (!stats || max_sessions <= 0) return -1;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return -1;
    const char *sql =
        "SELECT s.session_id,s.source,COALESCE(s.cwd,''),COALESCE(s.started_at,''),"
        "COALESCE((SELECT GROUP_CONCAT(model,', ') FROM ("
        " SELECT DISTINCT u.model model FROM model_usage_events u WHERE u.session_id=s.session_id AND u.model<>'' AND u.model<>'<synthetic>'"
        " UNION SELECT DISTINCT m.model FROM model_selection_events m WHERE m.session_id=s.session_id AND m.model<>'')),''),"
        "COALESCE((SELECT SUM(input_tokens) FROM model_usage_events u WHERE u.session_id=s.session_id),0),"
        "COALESCE((SELECT SUM(output_tokens) FROM model_usage_events u WHERE u.session_id=s.session_id),0),"
        "(SELECT COUNT(*) FROM tool_calls t WHERE t.session_id=s.session_id),"
        "(SELECT COUNT(DISTINCT c.source_path || ':' || c.line_number) FROM code_changes c WHERE c.session_id=s.session_id)"
        " FROM sessions s ORDER BY datetime(s.started_at) DESC,s.imported_at DESC LIMIT ?1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) { sqlite3_close(db); return -1; }
    sqlite3_bind_int(stmt,1,max_sessions);
    int count = 0;
    while (count < max_sessions && sqlite3_step(stmt) == SQLITE_ROW) {
        AgentSessionStats *row = &stats[count++];
        memset(row, 0, sizeof(*row));
        snprintf(row->session_id,sizeof(row->session_id),"%s",sqlite3_column_text(stmt,0));
        snprintf(row->source,sizeof(row->source),"%s",sqlite3_column_text(stmt,1));
        snprintf(row->cwd,sizeof(row->cwd),"%s",sqlite3_column_text(stmt,2));
        snprintf(row->started_at,sizeof(row->started_at),"%s",sqlite3_column_text(stmt,3));
        snprintf(row->models,sizeof(row->models),"%s",sqlite3_column_text(stmt,4));
        row->input_tokens=(long)sqlite3_column_int64(stmt,5);
        row->output_tokens=(long)sqlite3_column_int64(stmt,6);
        row->tool_calls=(long)sqlite3_column_int64(stmt,7);
        row->code_changes=(long)sqlite3_column_int64(stmt,8);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

/**
 * @brief 加载各类工具的调用统计数据
 * 
 * 从 tool_calls 表中按照工具名称分组统计，计算每种工具的总调用次数和属于 MCP（Model Context Protocol）的调用次数。
 * 按调用次数降序排列。
 * 
 * @param stats 输出统计结果的数组
 * @param max_tools 数组的最大容量
 * @return int 成功返回加载的工具记录数量，失败返回 -1
 */
int load_tool_stats(AgentToolStats stats[], int max_tools) {
    if (!stats || max_tools <= 0) return -1;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return -1;
    const char *sql = "SELECT tool_name,COALESCE(detail_name,''),COUNT(*),SUM(is_mcp) FROM tool_calls GROUP BY tool_name,detail_name ORDER BY COUNT(*) DESC LIMIT ?1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) { sqlite3_close(db); return -1; }
    sqlite3_bind_int(stmt,1,max_tools);
    int count = 0;
    while (count < max_tools && sqlite3_step(stmt) == SQLITE_ROW) {
        AgentToolStats *row=&stats[count++];
        memset(row,0,sizeof(*row));
        snprintf(row->tool_name,sizeof(row->tool_name),"%s",sqlite3_column_text(stmt,0));
        snprintf(row->detail_name,sizeof(row->detail_name),"%s",sqlite3_column_text(stmt,1));
        row->calls=(long)sqlite3_column_int64(stmt,2);
        row->mcp_calls=(long)sqlite3_column_int64(stmt,3);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

static bool path_is_dir(const char *path) {
    struct stat info;
    return path && path[0] && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static void path_parent(char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
    char *slash = strrchr(path, '/');
    if (!slash) { snprintf(path, 2, "."); return; }
    if (slash == path) path[1] = '\0'; else *slash = '\0';
}

static bool has_project_marker(const char *path) {
    static const char *markers[] = {
        ".git", "package.json", "Makefile", "go.mod", "Cargo.toml",
        "pyproject.toml", "pom.xml", "build.gradle", "composer.json"
    };
    char candidate[1400];
    struct stat info;
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        snprintf(candidate, sizeof(candidate), "%s/%s", path, markers[i]);
        if (stat(candidate, &info) == 0) return true;
    }
    return false;
}

static bool extract_workspace_root(const char *path, const char *anchor, char *out, size_t size);

static void resolve_project_path(const char *cwd, const char *file_path, char *out, size_t size) {
    bool unreliable_cwd = cwd && (strstr(cwd, "/.gemini/antigravity/brain/") ||
                                  strstr(cwd, "/.gemini/antigravity-cli/brain/"));
    const char *source = (!unreliable_cwd && cwd && cwd[0]) ? cwd : file_path;
    bool from_file = source == file_path;
    if (!source || !source[0] || strstr(source,"/.gemini/antigravity-cli/brain/")) { out[0] = '\0'; return; }
    snprintf(out, size, "%s", source);
    if (from_file || !path_is_dir(out)) path_parent(out);
    char fallback[1024];
    snprintf(fallback, sizeof(fallback), "%s", out);
    while (out[0]) {
        if (has_project_marker(out)) return;
        if (strcmp(out, "/") == 0 || strcmp(out, ".") == 0) break;
        path_parent(out);
    }
    if(from_file&&(extract_workspace_root(fallback,"/Desktop/workplace/",out,size)||
        extract_workspace_root(fallback,"/Desktop/my-project/",out,size)||
        extract_workspace_root(fallback,"/Downloads/",out,size))) return;
    const char *home=getenv("HOME");
    if(strcmp(fallback,"/")==0||(home&&strcmp(fallback,home)==0)){out[0]='\0';return;}
    snprintf(out, size, "%s", fallback);
}

static const char *path_basename(const char *path) {
    if (!path || !path[0]) return "未识别项目";
    const char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

static bool extract_workspace_root(const char *path, const char *anchor, char *out, size_t size) {
    const char *start=strstr(path,anchor);if(!start)return false;start+=strlen(anchor);const char *end=strchr(start,'/');
    if(!*start)return false;size_t prefix=(size_t)(start-path),name_len=end?(size_t)(end-start):strlen(start);
    if(prefix+name_len>=size)return false;snprintf(out,size,"%.*s",(int)(prefix+name_len),path);return true;
}

int load_project_stats(AgentProjectStats stats[], int max_projects) {
    if (!stats || max_projects <= 0) return -1;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return -1;
    const char *sql =
        "SELECT s.source,COALESCE(s.cwd,''),COALESCE((SELECT c.file_path FROM code_changes c "
        "WHERE c.session_id=s.session_id AND c.file_path<>'' ORDER BY CASE WHEN c.file_path LIKE '%/.gemini/antigravity-cli/brain/%' THEN 1 ELSE 0 END,c.rowid LIMIT 1),''),"
        "COALESCE((SELECT SUM(input_tokens) FROM model_usage_events u WHERE u.session_id=s.session_id AND u.model<>'<synthetic>'),0),"
        "COALESCE((SELECT SUM(output_tokens) FROM model_usage_events u WHERE u.session_id=s.session_id AND u.model<>'<synthetic>'),0),"
        "(SELECT COUNT(*) FROM tool_calls t WHERE t.session_id=s.session_id),"
        "(SELECT COUNT(DISTINCT c.source_path||':'||c.line_number) FROM code_changes c WHERE c.session_id=s.session_id) "
        "FROM sessions s";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) { sqlite3_close(db); return -1; }
    typedef struct { AgentProjectStats row; char source_names[256]; } ProjectAccumulator;
    ProjectAccumulator *acc = calloc((size_t)max_projects, sizeof(*acc));
    if (!acc) { sqlite3_finalize(stmt); sqlite3_close(db); return -1; }
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *source=(const char *)sqlite3_column_text(stmt,0);
        const char *cwd=(const char *)sqlite3_column_text(stmt,1);
        const char *file=(const char *)sqlite3_column_text(stmt,2);
        char project_path[1024]; resolve_project_path(cwd,file,project_path,sizeof(project_path));
        const char *key=project_path[0]?project_path:"未识别项目";
        int index=-1;
        for(int i=0;i<count;i++) if(strcmp(acc[i].row.project_path,key)==0){index=i;break;}
        if(index<0){
            if(count>=max_projects) continue;
            index=count++;
            snprintf(acc[index].row.project_path,sizeof(acc[index].row.project_path),"%s",key);
            snprintf(acc[index].row.project,sizeof(acc[index].row.project),"%s",path_basename(project_path));
        }
        AgentProjectStats *row=&acc[index].row;
        row->sessions++;
        row->input_tokens+=(long)sqlite3_column_int64(stmt,3);
        row->output_tokens+=(long)sqlite3_column_int64(stmt,4);
        row->tool_calls+=(long)sqlite3_column_int64(stmt,5);
        row->code_changes+=(long)sqlite3_column_int64(stmt,6);
        char token[48]; snprintf(token,sizeof(token),"|%s|",source?source:"");
        if(!strstr(acc[index].source_names,token)){
            strncat(acc[index].source_names,token,sizeof(acc[index].source_names)-strlen(acc[index].source_names)-1);
            row->sources++;
        }
    }
    for(int i=0;i<count;i++) stats[i]=acc[i].row;
    for(int i=0;i<count;i++) for(int j=i+1;j<count;j++) if(stats[j].sessions>stats[i].sessions){AgentProjectStats t=stats[i];stats[i]=stats[j];stats[j]=t;}
    free(acc); sqlite3_finalize(stmt); sqlite3_close(db); return count;
}

int load_mcp_stats(AgentCapabilityStats stats[], int max_rows) {
    if (!stats || max_rows <= 0) return -1;
    sqlite3 *db=NULL; sqlite3_stmt *stmt=NULL;
    if(!open_database(&db)) return -1;
    const char *sql="SELECT t.tool_name,s.source,COUNT(*) FROM tool_calls t JOIN sessions s ON s.session_id=t.session_id WHERE t.is_mcp=1 GROUP BY t.tool_name,s.source ORDER BY COUNT(*) DESC LIMIT ?1";
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)!=SQLITE_OK){sqlite3_close(db);return -1;}
    sqlite3_bind_int(stmt,1,max_rows); int count=0;
    while(count<max_rows&&sqlite3_step(stmt)==SQLITE_ROW){
        AgentCapabilityStats *row=&stats[count++]; memset(row,0,sizeof(*row));
        const char *tool=(const char *)sqlite3_column_text(stmt,0); const char *source=(const char *)sqlite3_column_text(stmt,1);
        const char *body=tool&&strncmp(tool,"mcp__",5)==0?tool+5:NULL; const char *sep=body?strstr(body,"__"):NULL;
        if(sep){snprintf(row->name,sizeof(row->name),"%.*s",(int)(sep-body),body);snprintf(row->detail,sizeof(row->detail),"%s",sep+2);}
        else {snprintf(row->name,sizeof(row->name),"未识别 Server");snprintf(row->detail,sizeof(row->detail),"%s",tool?tool:"");}
        snprintf(row->source,sizeof(row->source),"%s",source?source:""); row->calls=(long)sqlite3_column_int64(stmt,2);
    }
    sqlite3_finalize(stmt);sqlite3_close(db);return count;
}

int load_skill_stats(AgentCapabilityStats stats[], int max_rows) {
    if (!stats || max_rows <= 0) return -1;
    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;if(!open_database(&db))return -1;
    const char *sql="SELECT COALESCE(NULLIF(t.detail_name,''),'未识别 Skill'),s.source,COUNT(*) FROM tool_calls t JOIN sessions s ON s.session_id=t.session_id WHERE lower(t.tool_name)='skill' GROUP BY COALESCE(NULLIF(t.detail_name,''),'未识别 Skill'),s.source ORDER BY COUNT(*) DESC LIMIT ?1";
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)!=SQLITE_OK){sqlite3_close(db);return -1;}
    sqlite3_bind_int(stmt,1,max_rows);int count=0;while(count<max_rows&&sqlite3_step(stmt)==SQLITE_ROW){AgentCapabilityStats *row=&stats[count++];memset(row,0,sizeof(*row));snprintf(row->name,sizeof(row->name),"%s",sqlite3_column_text(stmt,0));snprintf(row->source,sizeof(row->source),"%s",sqlite3_column_text(stmt,1));row->calls=(long)sqlite3_column_int64(stmt,2);}sqlite3_finalize(stmt);sqlite3_close(db);return count;
}

bool set_model_pricing(const char *source, const char *model, double input_rate,
                       double cache_read_rate, double cache_write_rate, double output_rate) {
    if(!source||!source[0]||!model||!model[0]||input_rate<0||cache_read_rate<0||cache_write_rate<0||output_rate<0)return false;
    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;if(!open_database(&db))return false;
    const char *sql="INSERT INTO model_pricing(source,model,input_rate,cache_read_rate,cache_write_rate,output_rate,updated_at) VALUES(?1,?2,?3,?4,?5,?6,CURRENT_TIMESTAMP) ON CONFLICT(source,model) DO UPDATE SET input_rate=excluded.input_rate,cache_read_rate=excluded.cache_read_rate,cache_write_rate=excluded.cache_write_rate,output_rate=excluded.output_rate,updated_at=CURRENT_TIMESTAMP";
    bool ok=sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(stmt,1,source,-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,model,-1,SQLITE_TRANSIENT);sqlite3_bind_double(stmt,3,input_rate);sqlite3_bind_double(stmt,4,cache_read_rate);sqlite3_bind_double(stmt,5,cache_write_rate);sqlite3_bind_double(stmt,6,output_rate);ok=sqlite3_step(stmt)==SQLITE_DONE;}
    sqlite3_finalize(stmt);sqlite3_close(db);return ok;
}

int load_model_pricing(AgentModelStats stats[], int max_models) {
    if(!stats||max_models<=0)return -1;sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;if(!open_database(&db))return -1;
    const char *sql="SELECT source,model,input_rate,cache_read_rate,cache_write_rate,output_rate FROM model_pricing ORDER BY source,model";
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)!=SQLITE_OK){sqlite3_close(db);return -1;}int count=0;
    while(count<max_models&&sqlite3_step(stmt)==SQLITE_ROW){AgentModelStats *row=&stats[count++];memset(row,0,sizeof(*row));snprintf(row->source,sizeof(row->source),"%s",sqlite3_column_text(stmt,0));snprintf(row->model,sizeof(row->model),"%s",sqlite3_column_text(stmt,1));row->pricing_configured=true;row->input_rate=sqlite3_column_double(stmt,2);row->cache_read_rate=sqlite3_column_double(stmt,3);row->cache_write_rate=sqlite3_column_double(stmt,4);row->output_rate=sqlite3_column_double(stmt,5);}sqlite3_finalize(stmt);sqlite3_close(db);return count;
}

int load_period_stats(AgentPeriodStats stats[], int max_rows, const char *period) {
    if(!stats||max_rows<=0||!period)return -1;
    const char *bucket_expr=NULL;
    const char *calendar_start=NULL,*calendar_step=NULL;
    if(strcmp(period,"day")==0){bucket_expr="date(%s,'localtime')";calendar_start="date('now','localtime')";calendar_step="-1 day";}
    else if(strcmp(period,"week")==0){bucket_expr="date(%s,'localtime','-' || ((CAST(strftime('%%w',%s,'localtime') AS INTEGER)+6) %% 7) || ' days')";calendar_start="date('now','localtime','-' || ((CAST(strftime('%w','now','localtime') AS INTEGER)+6) % 7) || ' days')";calendar_step="-7 days";}
    else if(strcmp(period,"month")==0){bucket_expr="strftime('%%Y-%%m-01',%s,'localtime')";calendar_start="strftime('%Y-%m-01','now','localtime')";calendar_step="-1 month";}
    else return -1;

    char session_bucket[256],usage_bucket[256],tool_bucket[256],code_bucket[256];
    if(strcmp(period,"week")==0){
        snprintf(session_bucket,sizeof(session_bucket),bucket_expr,"s.started_at","s.started_at");
        snprintf(usage_bucket,sizeof(usage_bucket),bucket_expr,"u.timestamp","u.timestamp");
        snprintf(tool_bucket,sizeof(tool_bucket),bucket_expr,"t.timestamp","t.timestamp");
        snprintf(code_bucket,sizeof(code_bucket),bucket_expr,"c.timestamp","c.timestamp");
    }else{
        snprintf(session_bucket,sizeof(session_bucket),bucket_expr,"s.started_at");
        snprintf(usage_bucket,sizeof(usage_bucket),bucket_expr,"u.timestamp");
        snprintf(tool_bucket,sizeof(tool_bucket),bucket_expr,"t.timestamp");
        snprintf(code_bucket,sizeof(code_bucket),bucket_expr,"c.timestamp");
    }

    char sql[8192];
    int written=snprintf(sql,sizeof(sql),
        "WITH RECURSIVE calendar(bucket,n) AS (SELECT %s,0 UNION ALL SELECT date(bucket,'%s'),n+1 FROM calendar WHERE n+1<?1),"
        "session_stats AS (SELECT %s bucket,COUNT(*) sessions FROM sessions s WHERE s.started_at<>'' GROUP BY bucket),"
        "usage_stats AS (SELECT %s bucket,COUNT(*) model_calls,COALESCE(SUM(u.input_tokens),0) input_tokens,"
        "COALESCE(SUM(u.cached_input_tokens),0) cached_input_tokens,COALESCE(SUM(u.cache_write_input_tokens),0) cache_write_input_tokens,"
        "COALESCE(SUM(u.output_tokens),0) output_tokens,COALESCE(SUM(CASE WHEN p.model IS NOT NULL THEN 1 ELSE 0 END),0) priced_calls,"
        "COALESCE(SUM(CASE WHEN p.model IS NOT NULL THEN ("
        "MAX(u.input_tokens-u.cached_input_tokens-u.cache_write_input_tokens,0)*p.input_rate+"
        "u.cached_input_tokens*p.cache_read_rate+u.cache_write_input_tokens*p.cache_write_rate+u.output_tokens*p.output_rate)/1000000.0 ELSE 0 END),0) cost "
        "FROM model_usage_events u JOIN sessions s ON s.session_id=u.session_id LEFT JOIN model_pricing p ON p.source=s.source AND p.model=u.model "
        "WHERE u.timestamp<>'' AND u.model<>'<synthetic>' GROUP BY bucket),"
        "tool_stats AS (SELECT %s bucket,COUNT(*) tool_calls,COALESCE(SUM(t.is_mcp),0) mcp_calls FROM tool_calls t WHERE t.timestamp<>'' GROUP BY bucket),"
        "code_stats AS (SELECT %s bucket,COUNT(DISTINCT c.source_path||':'||c.line_number) code_changes,"
        "COALESCE(SUM(c.lines_added),0) lines_added,COALESCE(SUM(c.lines_deleted),0) lines_deleted FROM code_changes c WHERE c.timestamp<>'' GROUP BY bucket) "
        "SELECT b.bucket,COALESCE(s.sessions,0),COALESCE(u.model_calls,0),COALESCE(u.input_tokens,0),COALESCE(u.cached_input_tokens,0),"
        "COALESCE(u.cache_write_input_tokens,0),COALESCE(u.output_tokens,0),COALESCE(t.tool_calls,0),COALESCE(t.mcp_calls,0),"
        "COALESCE(c.code_changes,0),COALESCE(c.lines_added,0),COALESCE(c.lines_deleted,0),COALESCE(u.priced_calls,0),COALESCE(u.cost,0) "
        "FROM calendar b LEFT JOIN session_stats s USING(bucket) LEFT JOIN usage_stats u USING(bucket) LEFT JOIN tool_stats t USING(bucket) LEFT JOIN code_stats c USING(bucket) "
        "ORDER BY b.bucket DESC",
        calendar_start,calendar_step,session_bucket,usage_bucket,tool_bucket,code_bucket);
    if(written<0||(size_t)written>=sizeof(sql))return -1;

    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;if(!open_database(&db))return -1;
    if(sqlite3_prepare_v2(db,sql,-1,&stmt,NULL)!=SQLITE_OK){fprintf(stderr,"Period stats SQL error: %s\n",sqlite3_errmsg(db));sqlite3_close(db);return -1;}
    sqlite3_bind_int(stmt,1,max_rows);int count=0;
    while(count<max_rows&&sqlite3_step(stmt)==SQLITE_ROW){
        AgentPeriodStats *row=&stats[count++];memset(row,0,sizeof(*row));
        snprintf(row->period_start,sizeof(row->period_start),"%s",sqlite3_column_text(stmt,0));
        row->sessions=(long)sqlite3_column_int64(stmt,1);row->model_calls=(long)sqlite3_column_int64(stmt,2);
        row->input_tokens=(long)sqlite3_column_int64(stmt,3);row->cached_input_tokens=(long)sqlite3_column_int64(stmt,4);
        row->cache_write_input_tokens=(long)sqlite3_column_int64(stmt,5);row->output_tokens=(long)sqlite3_column_int64(stmt,6);
        row->tool_calls=(long)sqlite3_column_int64(stmt,7);row->mcp_calls=(long)sqlite3_column_int64(stmt,8);
        row->code_changes=(long)sqlite3_column_int64(stmt,9);row->lines_added=(long)sqlite3_column_int64(stmt,10);
        row->lines_deleted=(long)sqlite3_column_int64(stmt,11);row->priced_model_calls=(long)sqlite3_column_int64(stmt,12);
        row->estimated_cost_usd=sqlite3_column_double(stmt,13);
    }
    sqlite3_finalize(stmt);sqlite3_close(db);return count;
}

/**
 * @brief 加载代码修改的总体统计数据
 * 
 * 统计涉及修改的文件数、总共的添加/删除行数，以及按照业务、测试、文档等分类归总的修改代码行数。
 * 并计算业务代码在已分类代码中所占的百分比。
 * 
 * @param stats 用于输出统计结果的 AgentCodeStats 结构体指针
 * @return true 加载成功
 * @return false 加载失败
 */
bool load_code_stats(AgentCodeStats *stats) {
    if (!stats) return false;
    memset(stats, 0, sizeof(*stats));
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return false;
    const char *sql =
        "SELECT COUNT(DISTINCT source_path || ':' || line_number),COUNT(DISTINCT file_path),"
        "COALESCE(SUM(lines_added),0),COALESCE(SUM(lines_deleted),0),"
        "COALESCE(SUM(CASE WHEN category='business' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='test' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='documentation' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='generated' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='other' THEN lines_added ELSE 0 END),0) FROM code_changes";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW;
    if (ok) {
        stats->change_events = (long)sqlite3_column_int64(stmt, 0);
        stats->files_changed = (long)sqlite3_column_int64(stmt, 1);
        stats->lines_added = (long)sqlite3_column_int64(stmt, 2);
        stats->lines_deleted = (long)sqlite3_column_int64(stmt, 3);
        stats->business_lines_added = (long)sqlite3_column_int64(stmt, 4);
        stats->test_lines_added = (long)sqlite3_column_int64(stmt, 5);
        stats->documentation_lines_added = (long)sqlite3_column_int64(stmt, 6);
        stats->generated_lines_added = (long)sqlite3_column_int64(stmt, 7);
        stats->other_lines_added = (long)sqlite3_column_int64(stmt, 8);
        long classified = stats->business_lines_added + stats->test_lines_added +
            stats->documentation_lines_added + stats->other_lines_added;
        if (classified > 0)
            stats->business_code_share = (double)stats->business_lines_added * 100.0 / (double)classified;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

/**
 * @brief 加载 Git 仓库及代码提交的统计数据
 * 
 * 查询同步的 Git 仓库数、提交记录总数，并从 git_commit_files 表中统计各个代码分类下的添加/删除行数。
 * 
 * @param stats 用于输出统计结果的 AgentGitStats 结构体指针
 * @return true 加载成功
 * @return false 加载失败
 */
bool load_git_stats(AgentGitStats *stats) {
    if (!stats) return false;
    memset(stats, 0, sizeof(*stats));
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return false;
    const char *sql =
        "SELECT (SELECT COUNT(*) FROM git_repositories),(SELECT COUNT(*) FROM git_commits),"
        "COUNT(*),COALESCE(SUM(lines_added),0),COALESCE(SUM(lines_deleted),0),"
        "COALESCE(SUM(CASE WHEN category='business' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='test' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='documentation' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='generated' THEN lines_added ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN category='other' THEN lines_added ELSE 0 END),0) FROM git_commit_files";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW;
    if (ok) {
        stats->repositories = (long)sqlite3_column_int64(stmt, 0);
        stats->commits = (long)sqlite3_column_int64(stmt, 1);
        stats->files_changed = (long)sqlite3_column_int64(stmt, 2);
        stats->lines_added = (long)sqlite3_column_int64(stmt, 3);
        stats->lines_deleted = (long)sqlite3_column_int64(stmt, 4);
        stats->business_lines_added = (long)sqlite3_column_int64(stmt, 5);
        stats->test_lines_added = (long)sqlite3_column_int64(stmt, 6);
        stats->documentation_lines_added = (long)sqlite3_column_int64(stmt, 7);
        stats->generated_lines_added = (long)sqlite3_column_int64(stmt, 8);
        stats->other_lines_added = (long)sqlite3_column_int64(stmt, 9);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

/**
 * @brief 加载代码采纳/归因 (Attribution) 的统计数据
 * 
 * 通过对比 Agent 生成的代码指纹 (agent_line_fingerprints) 和实际提交到 Git 仓库的代码指纹 (git_line_fingerprints)，
 * 计算模型生成的代码有多少行最终被采纳提交到了代码库。
 * 同时也对不同类别（业务逻辑、测试、文档）的代码分别统计采纳率。
 * 
 * @param stats 用于输出统计结果的 AgentAttributionStats 结构体指针
 * @return true 加载成功
 * @return false 加载失败
 */
bool load_attribution_stats(AgentAttributionStats *stats) {
    if (!stats) return false;
    memset(stats, 0, sizeof(*stats));
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return false;
    const char *sql =
        "WITH agent_groups AS ("
        " SELECT g.repo_path,a.file_path,a.fingerprint,a.category,COUNT(*) AS candidate_count,"
        " MIN(datetime(a.timestamp)) AS first_seen"
        " FROM agent_line_fingerprints a JOIN git_repositories g"
        " ON substr(a.file_path,1,length(g.repo_path)+1)=g.repo_path || '/'"
        " WHERE a.category<>'generated'"
        " GROUP BY g.repo_path,a.file_path,a.fingerprint,a.category"
        "), matched AS ("
        " SELECT a.category,a.candidate_count,"
        " MIN(a.candidate_count,COUNT(CASE WHEN datetime(c.authored_at)>=a.first_seen THEN 1 END)) accepted"
        " FROM agent_groups a LEFT JOIN git_line_fingerprints l ON l.repo_path=a.repo_path"
        " AND l.repo_path || '/' || l.file_path=a.file_path AND l.fingerprint=a.fingerprint"
        " LEFT JOIN git_commits c ON c.repo_path=l.repo_path AND c.commit_hash=l.commit_hash"
        " GROUP BY a.repo_path,a.file_path,a.fingerprint,a.category,a.candidate_count"
        ") SELECT COALESCE(SUM(candidate_count),0),COALESCE(SUM(accepted),0),"
        " COALESCE(SUM(CASE WHEN category='business' THEN candidate_count ELSE 0 END),0),"
        " COALESCE(SUM(CASE WHEN category='business' THEN accepted ELSE 0 END),0),"
        " COALESCE(SUM(CASE WHEN category='test' THEN candidate_count ELSE 0 END),0),"
        " COALESCE(SUM(CASE WHEN category='test' THEN accepted ELSE 0 END),0),"
        " COALESCE(SUM(CASE WHEN category='documentation' THEN candidate_count ELSE 0 END),0),"
        " COALESCE(SUM(CASE WHEN category='documentation' THEN accepted ELSE 0 END),0) FROM matched";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW;
    if (ok) {
        stats->candidate_lines = (long)sqlite3_column_int64(stmt,0);
        stats->accepted_lines = (long)sqlite3_column_int64(stmt,1);
        stats->business_candidate_lines = (long)sqlite3_column_int64(stmt,2);
        stats->business_accepted_lines = (long)sqlite3_column_int64(stmt,3);
        stats->test_candidate_lines = (long)sqlite3_column_int64(stmt,4);
        stats->test_accepted_lines = (long)sqlite3_column_int64(stmt,5);
        stats->documentation_candidate_lines = (long)sqlite3_column_int64(stmt,6);
        stats->documentation_accepted_lines = (long)sqlite3_column_int64(stmt,7);
        if (stats->candidate_lines > 0) stats->acceptance_rate = (double)stats->accepted_lines * 100.0 / stats->candidate_lines;
        if (stats->business_candidate_lines > 0) stats->business_acceptance_rate = (double)stats->business_accepted_lines * 100.0 / stats->business_candidate_lines;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

/**
 * @brief 保存单条 AgentRecord 数据到数据库中
 * 
 * 使用预处理 SQL 语句将传入的记录插入 agent_records 表，适用于旧版或兼容性的数据存储。
 * 
 * @param record 待保存的 AgentRecord 结构体指针
 * @return true 保存成功
 * @return false 保存失败
 */
bool save_record(const AgentRecord *record) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return false;
    const char *sql =
        "INSERT INTO agent_records (session_id,timestamp,project,language,model,"
        "input_tokens,output_tokens,cost_usd,lines_suggested,lines_accepted,"
        "snippets_suggested,snippets_accepted,duration_sec) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK &&
              bind_record(stmt, record) && sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) fprintf(stderr, "Failed to save record: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

/**
 * @brief 加载 agent_records 表中的所有旧版/基础记录
 * 
 * 按时间升序从 agent_records 表中读取最多 max_records 条数据记录，
 * 将其填充到提供的 AgentRecord 数组中。
 * 
 * @param records 输出记录的数组
 * @param max_records 数组的最大容量
 * @return int 成功返回加载的记录数量，失败返回 0
 */
int load_all_records(AgentRecord records[], int max_records) {
    if (!records || max_records <= 0) return 0;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    if (!open_database(&db)) return 0;
    const char *sql =
        "SELECT session_id,timestamp,project,language,model,input_tokens,output_tokens,"
        "cost_usd,lines_suggested,lines_accepted,snippets_suggested,snippets_accepted,duration_sec "
        "FROM agent_records ORDER BY timestamp ASC, rowid ASC LIMIT ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 0;
    }
    sqlite3_bind_int(stmt, 1, max_records);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_records) {
        AgentRecord *r = &records[count];
        memset(r, 0, sizeof(AgentRecord));
        snprintf(r->session_id, sizeof(r->session_id), "%s", sqlite3_column_text(stmt, 0));
        snprintf(r->timestamp, sizeof(r->timestamp), "%s", sqlite3_column_text(stmt, 1));
        snprintf(r->project, sizeof(r->project), "%s", sqlite3_column_text(stmt, 2));
        snprintf(r->language, sizeof(r->language), "%s", sqlite3_column_text(stmt, 3));
        snprintf(r->model, sizeof(r->model), "%s", sqlite3_column_text(stmt, 4));
        r->input_tokens = (long)sqlite3_column_int64(stmt, 5);
        r->output_tokens = (long)sqlite3_column_int64(stmt, 6);
        r->estimated_cost_usd = sqlite3_column_double(stmt, 7);
        r->lines_suggested = sqlite3_column_int(stmt, 8);
        r->lines_accepted = sqlite3_column_int(stmt, 9);
        r->snippets_suggested = sqlite3_column_int(stmt, 10);
        r->snippets_accepted = sqlite3_column_int(stmt, 11);
        r->duration_seconds = sqlite3_column_double(stmt, 12);
        count++;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

/**
 * @brief 生成并插入模拟的测试数据
 * 
 * 检查数据库是否已有数据，如果没有，则插入一组硬编码的 AgentRecord 模拟数据，
 * 方便在开发和测试环境下快速查看应用效果。
 */
void seed_mock_data(void) {
    AgentRecord existing[1];
    if (load_all_records(existing, 1) > 0) return;
    
    AgentRecord mock_records[] = {
        {"sess_20260806_001", "2026-08-06 09:15:00", "agent-cli", "C", "claude-3-5-sonnet", 8500, 2100, 0.057, 140, 128, 5, 5, 14.2},
        {"sess_20260806_002", "2026-08-06 11:30:00", "agent-cli", "C", "gemini-1.5-flash", 14200, 3900, 0.002, 210, 185, 8, 7, 8.5},
        {"sess_20260806_003", "2026-08-06 14:05:00", "web-dashboard", "TypeScript", "gpt-4o", 18900, 4200, 0.089, 95, 80, 4, 3, 21.0},
        {"sess_20260806_004", "2026-08-06 16:45:00", "core-service", "Go", "claude-3-5-sonnet", 11000, 2800, 0.075, 160, 152, 6, 6, 12.0},
        {"sess_20260806_005", "2026-08-06 19:20:00", "agent-cli", "C", "deepseek-coder", 22000, 5600, 0.005, 310, 275, 10, 9, 16.8}
    };
    
    int num_mocks = sizeof(mock_records) / sizeof(mock_records[0]);
    for (int i = 0; i < num_mocks; i++) {
        save_record(&mock_records[i]);
    }
}
