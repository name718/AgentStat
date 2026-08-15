#include "antigravity_importer.h"
#include "adapter_utils.h"
#include <dirent.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// 根据路径包含的 'brain' 分段，自动提取 Antigravity 会话标识符
static void session_from_path(const char *path, char *out, size_t size) {
  const char *start = strstr(path, "/brain/");
  if (!start) {
    snprintf(out, size, "antigravity:%s", path);
    return;
  }
  start += 7;
  const char *end = strchr(start, '/');
  size_t length = end ? (size_t)(end - start) : strlen(start);
  snprintf(out, size, "antigravity:%.*s", (int)length, start);
}

// 使用 SQLite JSON 函数解码复杂或者转义的工具调用参数字符串
static bool decode_arg(sqlite3 *db, const char *args, const char *path,
                       char *out, size_t size) {
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT COALESCE(CASE WHEN json_type(?1,?2)='text' AND "
                    "json_valid(json_extract(?1,?2)) THEN "
                    "json_extract(json_extract(?1,?2),'$') ELSE "
                    "json_extract(?1,?2) END,'') WHERE json_valid(?1)";
  bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
  if (ok) {
    sqlite3_bind_text(stmt, 1, args, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if (ok)
      snprintf(out, size, "%s", sqlite3_column_text(stmt, 0));
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 保存单个文本操作记录，并将其引起的行更改（代码修改与指纹）插入到数据库中
static bool record_change(sqlite3 *db, const char *source_path,
                          long event_number, const char *session_id,
                          const char *timestamp, const char *tool_name,
                          const char *tool_id, const char *file,
                          const char *old_text, const char *new_text,
                          CodexImportResult *result) {
  bool inserted = false;
  bool ok = adapter_record_text_change(
      db, source_path, event_number, session_id, tool_id, timestamp, file,
      tool_name, old_text, new_text, &inserted);
  if (inserted)
    result->code_changes_imported++;
  return ok;
}

// 专门处理 'multi_replace_file_content' 多处替换工具的数据提取并记录其多次改动
static bool import_multi_replace(sqlite3 *db, const char *args,
                                 const char *source_path, long line_number,
                                 int tool_ordinal, const char *session_id,
                                 const char *timestamp, const char *file,
                                 const char *tool_id,
                                 CodexImportResult *result) {
  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "WITH chunks(value) AS (SELECT CASE WHEN "
      "json_type(?1,'$.ReplacementChunks')='text' AND "
      "json_valid(json_extract(?1,'$.ReplacementChunks')) THEN "
      "json_extract(?1,'$.ReplacementChunks') ELSE "
      "json_extract(?1,'$.ReplacementChunks') END) "
      "SELECT COALESCE(CASE WHEN "
      "json_type(item.value,'$.TargetContent')='text' AND "
      "json_valid(json_extract(item.value,'$.TargetContent')) THEN "
      "json_extract(json_extract(item.value,'$.TargetContent'),'$') ELSE "
      "json_extract(item.value,'$.TargetContent') END,''),"
      "COALESCE(CASE WHEN json_type(item.value,'$.ReplacementContent')='text' "
      "AND json_valid(json_extract(item.value,'$.ReplacementContent')) THEN "
      "json_extract(json_extract(item.value,'$.ReplacementContent'),'$') ELSE "
      "json_extract(item.value,'$.ReplacementContent') END,'') FROM "
      "chunks,json_each(chunks.value) item";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, args, -1, SQLITE_TRANSIENT);
  bool ok = true;
  int chunk = 0;
  while (ok && sqlite3_step(stmt) == SQLITE_ROW) {
    const char *old_text = (const char *)sqlite3_column_text(stmt, 0),
               *new_text = (const char *)sqlite3_column_text(stmt, 1);
    long event = -(line_number * 10000L + tool_ordinal * 100L + chunk + 1);
    ok = record_change(db, source_path, event, session_id, timestamp,
                       "multi_replace_file_content", tool_id, file, old_text,
                       new_text, result);
    chunk++;
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 分析工具执行的 planner
// 数据，将提取到的各工具使用事件以及潜在的代码改变动作保存到数据库中
static bool import_planner(sqlite3 *db, const char *json,
                           const char *source_path, long line_number,
                           const char *session_id, const char *timestamp,
                           bool record_code, CodexImportResult *result) {
  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "SELECT CAST(key AS "
      "INTEGER),COALESCE(json_extract(value,'$.name'),''),COALESCE(json_"
      "extract(value,'$.args'),'{}') FROM json_each(?1,'$.tool_calls') WHERE "
      "json_valid(?1) AND json_type(value)='object'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
  bool ok = true;
  while (ok && sqlite3_step(stmt) == SQLITE_ROW) {
    int ordinal = sqlite3_column_int(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1),
               *args = (const char *)sqlite3_column_text(stmt, 2);
    long tool_event = -(line_number * 10000L + ordinal * 100L);
    bool inserted = false;
    ok = adapter_insert_tool(db, source_path, tool_event, session_id, timestamp,
                             name, "tool_call", strstr(name, "mcp__") == name,
                             "", &inserted);
    if (inserted)
      result->tool_calls_imported++;
    if (!ok)
      break;
    char file[PATH_MAX] = "", old_text[1] = {0};
    char tool_id[128];
    snprintf(tool_id, sizeof(tool_id), "step-%ld-tool-%d", line_number,
             ordinal);
    if (record_code && !strcmp(name, "replace_file_content")) {
      decode_arg(db, args, "$.TargetFile", file, sizeof(file));
      sqlite3_stmt *content = NULL;
      const char *q =
          "SELECT COALESCE(CASE WHEN "
          "json_valid(json_extract(?1,'$.TargetContent')) THEN "
          "json_extract(json_extract(?1,'$.TargetContent'),'$') ELSE "
          "json_extract(?1,'$.TargetContent') END,''),COALESCE(CASE WHEN "
          "json_valid(json_extract(?1,'$.ReplacementContent')) THEN "
          "json_extract(json_extract(?1,'$.ReplacementContent'),'$') ELSE "
          "json_extract(?1,'$.ReplacementContent') END,'')";
      if (file[0] &&
          sqlite3_prepare_v2(db, q, -1, &content, NULL) == SQLITE_OK) {
        sqlite3_bind_text(content, 1, args, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(content) == SQLITE_ROW)
          ok = record_change(
              db, source_path, tool_event - 1, session_id, timestamp, name,
              tool_id, file, (const char *)sqlite3_column_text(content, 0),
              (const char *)sqlite3_column_text(content, 1), result);
      }
      sqlite3_finalize(content);
    } else if (record_code && !strcmp(name, "write_to_file")) {
      decode_arg(db, args, "$.TargetFile", file, sizeof(file));
      sqlite3_stmt *content = NULL;
      const char *q = "SELECT COALESCE(CASE WHEN "
                      "json_valid(json_extract(?1,'$.CodeContent')) THEN "
                      "json_extract(json_extract(?1,'$.CodeContent'),'$') ELSE "
                      "json_extract(?1,'$.CodeContent') END,'')";
      if (file[0] &&
          sqlite3_prepare_v2(db, q, -1, &content, NULL) == SQLITE_OK) {
        sqlite3_bind_text(content, 1, args, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(content) == SQLITE_ROW)
          ok = record_change(db, source_path, tool_event - 1, session_id,
                             timestamp, name, tool_id, file, old_text,
                             (const char *)sqlite3_column_text(content, 0),
                             result);
      }
      sqlite3_finalize(content);
    } else if (record_code && !strcmp(name, "multi_replace_file_content")) {
      decode_arg(db, args, "$.TargetFile", file, sizeof(file));
      if (file[0])
        ok = import_multi_replace(db, args, source_path, line_number, ordinal,
                                  session_id, timestamp, file, tool_id, result);
    }
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 检查当前的 planner 动作中是否包含了直接修改文件的操作
static bool planner_has_code_action(sqlite3 *db, const char *json) {
  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "SELECT COUNT(*) FROM json_each(?1,'$.tool_calls') WHERE json_valid(?1) "
      "AND json_type(value)='object' AND json_extract(value,'$.name') IN "
      "('replace_file_content','multi_replace_file_content','write_to_file')";
  bool found = false;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
      found = sqlite3_column_int(stmt, 0) > 0;
  }
  sqlite3_finalize(stmt);
  return found;
}

// 辅助函数：内存拷贝字符串
static char *copy_text(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy)
    memcpy(copy, text, length + 1);
  return copy;
}

// 解析消息文本，识别用户的模型切换行为，并导入选择记录
static bool import_model_selection(sqlite3 *db, const char *json,
                                   const char *source_path, long line_number,
                                   const char *session_id,
                                   const char *timestamp) {
  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "WITH content(value) AS (SELECT "
      "COALESCE(json_extract(?1,'$.content'),'')),"
      "change(value) AS (SELECT substr(value,instr(value,'changed setting "
      "`Model Selection`')) FROM content) "
      "SELECT trim(substr(value,instr(value,' to ')+4,instr(value,'. No "
      "need')-(instr(value,' to ')+4))) "
      "FROM change WHERE instr(value,'changed setting `Model Selection`')>0 "
      "AND instr(value,' to ')>0 AND instr(value,'. No need')>0";
  bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
  if (ok) {
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
      const char *model = (const char *)sqlite3_column_text(stmt, 0);
      bool inserted = false;
      if (model && *model)
        ok = adapter_insert_model_selection(db, source_path, line_number,
                                            session_id, timestamp, model,
                                            &inserted);
    } else if (step != SQLITE_DONE)
      ok = false;
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 核心函数：解析 Antigravity 运行产生的
// transcript.jsonl，导入会话信息、模型选择及各工具的代码变更活动
bool import_antigravity_jsonl(const char *path, CodexImportResult *result) {
  if (!path || !result)
    return false;
  memset(result, 0, sizeof(*result));
  char canonical[PATH_MAX];
  const char *source_path = realpath(path, canonical) ? canonical : path;
  FILE *fp = fopen(source_path, "r");
  if (!fp)
    return false;
  sqlite3 *db = NULL;
  if (!adapter_open_database(&db)) {
    fclose(fp);
    return false;
  }
  bool ok = adapter_execute(db, "BEGIN IMMEDIATE");
  char session_id[PATH_MAX + 32];
  session_from_path(source_path, session_id, sizeof(session_id));
  char started_at[128] = "";
  long line_number = 0;
  char *line = NULL;
  char *pending_json = NULL;
  long pending_line = 0;
  char pending_timestamp[128] = "";
  while (ok && (line = adapter_read_jsonl_line(fp))) {
    line_number++;
    result->lines_read++;
    char type[64] = "", timestamp[128] = "", status[64] = "";
    if (!adapter_json_text(db, line, "$.type", type, sizeof(type))) {
      free(line);
      line = NULL;
      continue;
    }
    adapter_json_text(db, line, "$.created_at", timestamp, sizeof(timestamp));
    adapter_json_text(db, line, "$.status", status, sizeof(status));
    if (!started_at[0] && timestamp[0])
      snprintf(started_at, sizeof(started_at), "%s", timestamp);
    bool changed = false;
    ok = adapter_upsert_session(db, session_id, "antigravity", source_path, "",
                                started_at, "google", &changed);
    result->session_imported = true;
    if (ok)
      ok = import_model_selection(db, line, source_path, line_number,
                                  session_id, timestamp);
    if (ok && !strcmp(type, "PLANNER_RESPONSE") &&
        (!status[0] || !strcmp(status, "DONE"))) {
      free(pending_json);
      pending_json = NULL;
      ok = import_planner(db, line, source_path, line_number, session_id,
                          timestamp, false, result);
      if (ok && planner_has_code_action(db, line)) {
        pending_json = copy_text(line);
        pending_line = line_number;
        snprintf(pending_timestamp, sizeof(pending_timestamp), "%s", timestamp);
        if (!pending_json)
          ok = false;
      }
    } else if (ok && pending_json && !strcmp(type, "CODE_ACTION")) {
      if (!strcmp(status, "DONE"))
        ok = import_planner(db, pending_json, source_path, pending_line,
                            session_id, pending_timestamp, true, result);
      free(pending_json);
      pending_json = NULL;
    } else if (pending_json && (!strcmp(type, "ERROR_MESSAGE") ||
                                !strcmp(type, "USER_INPUT"))) {
      free(pending_json);
      pending_json = NULL;
    }
    free(line);
    line = NULL;
  }
  free(line);
  free(pending_json);
  if (ok)
    ok = adapter_execute(db, "COMMIT");
  else
    adapter_execute(db, "ROLLBACK");
  sqlite3_close(db);
  fclose(fp);
  return ok;
}

// 递归遍历文件系统寻找所有的 transcript.jsonl，并同步其中记录
static bool sync_recursive(const char *path, CodexSyncResult *result) {
  DIR *dir = opendir(path);
  if (!dir)
    return false;
  bool ok = true;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    char child[PATH_MAX];
    if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
        (int)sizeof(child)) {
      result->files_failed++;
      ok = false;
      continue;
    }
    struct stat info;
    if (lstat(child, &info) != 0) {
      result->files_failed++;
      ok = false;
      continue;
    }
    if (S_ISDIR(info.st_mode)) {
      if (!sync_recursive(child, result))
        ok = false;
    } else if (S_ISREG(info.st_mode) &&
               !strcmp(entry->d_name, "transcript.jsonl")) {
      CodexImportResult one;
      result->files_scanned++;
      if (!import_antigravity_jsonl(child, &one)) {
        result->files_failed++;
        ok = false;
        continue;
      }
      if (one.session_imported)
        result->sessions_imported++;
      result->lines_read += one.lines_read;
      result->usage_events_imported += one.usage_events_imported;
      result->tool_calls_imported += one.tool_calls_imported;
      result->code_changes_imported += one.code_changes_imported;
    }
  }
  closedir(dir);
  return ok;
}

// 对数据格式及代码解析器的版本进行检查与可能的迁移更新，以防数据结构陈旧
static bool migrate_code_parser(void) {
  sqlite3 *db = NULL;
  if (!adapter_open_database(&db))
    return false;
  sqlite3_stmt *stmt = NULL;
  bool current = false;
  if (sqlite3_prepare_v2(db,
                         "SELECT value FROM storage_metadata WHERE "
                         "key='antigravity_code_parser_version'",
                         -1, &stmt, NULL) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_ROW)
    current = !strcmp((const char *)sqlite3_column_text(stmt, 0), "2");
  sqlite3_finalize(stmt);
  bool ok = true;
  if (!current) {
    ok = adapter_execute(db, "BEGIN IMMEDIATE") &&
         adapter_execute(
             db,
             "DELETE FROM agent_line_fingerprints WHERE session_id IN (SELECT "
             "session_id FROM sessions WHERE source='antigravity')") &&
         adapter_execute(
             db, "DELETE FROM code_changes WHERE session_id IN (SELECT "
                 "session_id FROM sessions WHERE source='antigravity')") &&
         adapter_execute(db, "INSERT INTO storage_metadata(key,value) "
                             "VALUES('antigravity_code_parser_version','2') ON "
                             "CONFLICT(key) DO UPDATE SET value='2'");
    if (ok)
      ok = adapter_execute(db, "COMMIT");
    else
      adapter_execute(db, "ROLLBACK");
  }
  sqlite3_close(db);
  return ok;
}

// 对外公开的 Antigravity
// 数据同步入口，先执行迁移准备，再递归处理所有目录下的日志
bool sync_antigravity_directory(const char *path, CodexSyncResult *result) {
  if (!path || !result)
    return false;
  memset(result, 0, sizeof(*result));
  if (!migrate_code_parser())
    return false;
  char canonical[PATH_MAX];
  return sync_recursive(realpath(path, canonical) ? canonical : path, result);
}
