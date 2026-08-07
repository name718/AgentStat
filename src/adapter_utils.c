#include "adapter_utils.h"
#include "storage.h"
#include "sha256.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// 从 JSONL 文件读取一行数据，支持动态分配和扩展内存，直到遇到换行符或文件结束
char *adapter_read_jsonl_line(FILE *fp) {
    size_t capacity = 4096, length = 0;
    char *line = malloc(capacity);
    if (!line) return NULL;
    int ch = EOF;
    while ((ch = fgetc(fp)) != EOF) {
        if (length + 1 >= capacity) {
            char *resized = realloc(line, capacity * 2);
            if (!resized) { free(line); return NULL; }
            line = resized;
            capacity *= 2;
        }
        line[length++] = (char)ch;
        if (ch == '\n') break;
    }
    if (length == 0 && ch == EOF) { free(line); return NULL; }
    line[length] = '\0';
    return line;
}

// 使用 SQLite 内存数据库从给定的 JSON 字符串中提取指定路径的文本数据
bool adapter_json_text(sqlite3 *db, const char *json, const char *path, char *out, size_t size) {
    sqlite3_stmt *stmt = NULL;
    bool ok = sqlite3_prepare_v2(db, "SELECT COALESCE(json_extract(?1,?2),'') WHERE json_valid(?1)", -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
        ok = sqlite3_step(stmt) == SQLITE_ROW;
        if (ok) snprintf(out, size, "%s", sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return ok;
}

// 使用 SQLite 内存数据库从给定的 JSON 字符串中提取指定路径的整数值
sqlite3_int64 adapter_json_int(sqlite3 *db, const char *json, const char *path) {
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

// 执行一个简单的 SQLite SQL 语句，不返回结果集
bool adapter_execute(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

// 初始化存储并打开 SQLite 数据库连接，同时设置外键约束和超时时间
bool adapter_open_database(sqlite3 **db) {
    if (!initialize_storage()) return false;
    char path[512];
    get_db_file_path(path, sizeof(path));
    if (sqlite3_open(path, db) != SQLITE_OK) return false;
    sqlite3_busy_timeout(*db, 5000);
    return adapter_execute(*db, "PRAGMA foreign_keys=ON");
}

// 获取给定文件路径的规范绝对路径，若无法解析则原样复制
const char *adapter_canonical_file(const char *path, char *output, size_t size) {
    if (!path || !*path) return "";
    if (realpath(path, output)) return output;
    snprintf(output, size, "%s", path);
    return output;
}

// 辅助函数：判断文件路径中是否包含特定子串
static bool contains(const char *path, const char *part) { return strstr(path, part) != NULL; }

// 根据文件路径及其扩展名进行文件分类（如：生成文件、测试代码、文档、业务代码等）
const char *adapter_classify_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (contains(path,"/node_modules/") || contains(path,"/vendor/") || contains(path,"/dist/") ||
        contains(path,"/build/") || contains(path,"/generated/") || contains(path,"/Pods/") ||
        (ext && (strcmp(ext,".lock")==0 || strcmp(ext,".min.js")==0))) return "generated";
    if (contains(path,"/test/") || contains(path,"/tests/") || contains(path,"/__tests__/") ||
        contains(path,"/spec/") || strstr(path,"_test.") || strstr(path,".test.") || strstr(path,".spec.")) return "test";
    if ((ext && (!strcmp(ext,".md") || !strcmp(ext,".mdx") || !strcmp(ext,".rst") || !strcmp(ext,".txt"))) ||
        contains(path,"/docs/") || contains(path,"/doc/")) return "documentation";
    if (ext) {
        const char *extensions[] = {".c",".h",".cc",".cpp",".hpp",".m",".mm",".swift",".go",".rs",".java",".kt",".kts",".py",".rb",".php",".js",".jsx",".ts",".tsx",".vue",".svelte",".css",".scss",".less",".html",".sql",".sh",".zsh"};
        for (size_t i=0; i<sizeof(extensions)/sizeof(extensions[0]); i++) if (!strcmp(ext,extensions[i])) return "business";
    }
    return "other";
}

// 插入或更新会话 (session) 记录，如果记录已经存在则更新有效字段
bool adapter_upsert_session(sqlite3 *db, const char *session_id, const char *source,
                            const char *source_path, const char *cwd, const char *started_at,
                            const char *provider, bool *inserted) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO sessions(session_id,source,source_path,cwd,started_at,model_provider) "
        "VALUES(?1,?2,?3,?4,?5,?6) ON CONFLICT(session_id) DO UPDATE SET "
        "source_path=excluded.source_path,cwd=CASE WHEN excluded.cwd<>'' THEN excluded.cwd ELSE sessions.cwd END," 
        "started_at=CASE WHEN sessions.started_at IS NULL OR sessions.started_at='' THEN excluded.started_at ELSE sessions.started_at END," 
        "model_provider=CASE WHEN excluded.model_provider<>'' THEN excluded.model_provider ELSE sessions.model_provider END";
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt,1,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,source,-1,SQLITE_STATIC);
        sqlite3_bind_text(stmt,3,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,cwd,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,5,started_at,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,6,provider,-1,SQLITE_TRANSIENT);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (inserted) *inserted = ok && sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

// 记录工具调用 (tool call) 事件至数据库，包含调用类型与是否为 MCP 工具
bool adapter_insert_tool(sqlite3 *db, const char *source_path, long event_number,
                         const char *session_id, const char *timestamp, const char *name,
                         const char *call_type, bool is_mcp, bool *inserted) {
    sqlite3_stmt *stmt = NULL;
    bool ok = sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO tool_calls(source_path,line_number,session_id,timestamp,tool_name,call_type,is_mcp) VALUES(?1,?2,?3,?4,?5,?6,?7)",-1,&stmt,NULL)==SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(stmt,2,event_number);
        sqlite3_bind_text(stmt,3,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,timestamp,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,5,name,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,6,call_type,-1,SQLITE_STATIC);
        sqlite3_bind_int(stmt,7,is_mcp); ok=sqlite3_step(stmt)==SQLITE_DONE;
    }
    if (inserted) *inserted=ok && sqlite3_changes(db)>0;
    sqlite3_finalize(stmt);
    return ok;
}

// 记录模型选择和切换 (model selection) 事件到数据库
bool adapter_insert_model_selection(sqlite3 *db, const char *source_path, long event_number,
                                    const char *session_id, const char *timestamp,
                                    const char *model, bool *inserted) {
    sqlite3_stmt *stmt = NULL;
    bool ok = sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO model_selection_events(source_path,line_number,session_id,timestamp,model) VALUES(?1,?2,?3,?4,?5)",
        -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt,2,event_number);
        sqlite3_bind_text(stmt,3,session_id,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,4,timestamp,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,5,model,-1,SQLITE_TRANSIENT);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (inserted) *inserted = ok && sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

// 表示文本中一行的结构，包含起始指针、长度以及匹配状态标记
typedef struct { const char *start; size_t length; bool matched; } TextLine;

// 将多行字符串分割为 TextLine 结构体数组，便于按行进行增删差异比较
static TextLine *split_lines(const char *text, size_t *count) {
    *count = 0;
    if (!text || !*text) return NULL;
    size_t capacity=16;
    TextLine *lines=calloc(capacity,sizeof(*lines));
    if (!lines) return NULL;
    const char *start=text;
    while (*start) {
        const char *end=strchr(start,'\n');
        size_t length=end?(size_t)(end-start):strlen(start);
        if (length && start[length-1]=='\r') length--;
        if (*count==capacity) { capacity*=2; TextLine *next=realloc(lines,capacity*sizeof(*lines)); if(!next){free(lines);return NULL;} lines=next; }
        lines[*count]=(TextLine){start,length,false}; (*count)++;
        if (!end) break;
        start=end+1;
    }
    return lines;
}

// 判断两行文本内容是否完全相同
static bool same_line(const TextLine *a,const TextLine *b) {
    return a->length==b->length && memcmp(a->start,b->start,a->length)==0;
}

// 比较新旧文本以计算添加和删除的行数，并将代码修改记录和行级别的 SHA256 指纹记录到数据库中
bool adapter_record_text_change(sqlite3 *db, const char *source_path, long event_number,
                                const char *session_id, const char *turn_id,
                                const char *timestamp, const char *file_path,
                                const char *change_type, const char *old_text,
                                const char *new_text, bool *inserted) {
    size_t old_count=0,new_count=0;
    TextLine *old_lines=split_lines(old_text,&old_count), *new_lines=split_lines(new_text,&new_count);
    for (size_t i=0;i<new_count;i++) for(size_t j=0;j<old_count;j++)
        if(!old_lines[j].matched && same_line(&new_lines[i],&old_lines[j])) { new_lines[i].matched=true; old_lines[j].matched=true; break; }
    long added=0,deleted=0;
    for(size_t i=0;i<new_count;i++) if(!new_lines[i].matched) added++;
    for(size_t i=0;i<old_count;i++) if(!old_lines[i].matched) deleted++;

    char canonical[PATH_MAX];
    const char *resolved=adapter_canonical_file(file_path,canonical,sizeof(canonical));
    const char *category=adapter_classify_path(resolved);
    sqlite3_stmt *stmt=NULL;
    bool ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO code_changes(source_path,line_number,session_id,turn_id,timestamp,file_path,change_type,category,lines_added,lines_deleted) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)",-1,&stmt,NULL)==SQLITE_OK;
    if(ok){
        sqlite3_bind_text(stmt,1,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(stmt,2,event_number); sqlite3_bind_text(stmt,3,session_id,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,4,turn_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,5,timestamp,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,6,resolved,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,7,change_type,-1,SQLITE_STATIC); sqlite3_bind_text(stmt,8,category,-1,SQLITE_STATIC); sqlite3_bind_int64(stmt,9,added); sqlite3_bind_int64(stmt,10,deleted);
        ok=sqlite3_step(stmt)==SQLITE_DONE;
    }
    bool was_inserted=ok && sqlite3_changes(db)>0;
    sqlite3_finalize(stmt);
    if(inserted) *inserted=was_inserted;

    if(ok) for(size_t i=0;i<new_count;i++) if(!new_lines[i].matched && new_lines[i].length>0) {
        char hash[65]; sha256_hex((const unsigned char*)new_lines[i].start,new_lines[i].length,hash);
        sqlite3_stmt *fingerprint=NULL;
        ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO agent_line_fingerprints(source_path,line_number,file_path,line_ordinal,session_id,timestamp,category,fingerprint) VALUES(?1,?2,?3,?4,?5,?6,?7,?8)",-1,&fingerprint,NULL)==SQLITE_OK;
        if(ok){ sqlite3_bind_text(fingerprint,1,source_path,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(fingerprint,2,event_number); sqlite3_bind_text(fingerprint,3,resolved,-1,SQLITE_TRANSIENT); sqlite3_bind_int64(fingerprint,4,(sqlite3_int64)i); sqlite3_bind_text(fingerprint,5,session_id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(fingerprint,6,timestamp,-1,SQLITE_TRANSIENT); sqlite3_bind_text(fingerprint,7,category,-1,SQLITE_STATIC); sqlite3_bind_text(fingerprint,8,hash,-1,SQLITE_TRANSIENT); ok=sqlite3_step(fingerprint)==SQLITE_DONE; }
        sqlite3_finalize(fingerprint);
    }
    free(old_lines); free(new_lines);
    return ok;
}
