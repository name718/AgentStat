#include "git_importer.h"
#include "sha256.h"
#include "storage.h"
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// 启动 Git 进程并构建管道用于读取其标准输出信息
static FILE *open_git_stream(const char *repo_path,
                             const char *const arguments[], pid_t *child_pid) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0)
    return NULL;
  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return NULL;
  }
  if (pid == 0) {
    close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) < 0)
      _exit(127);
    close(pipe_fds[1]);
    char *argv[16];
    int index = 0;
    argv[index++] = "git";
    argv[index++] = "-C";
    argv[index++] = (char *)repo_path;
    for (int i = 0; arguments[i] && index < 15; i++)
      argv[index++] = (char *)arguments[i];
    argv[index] = NULL;
    execvp("git", argv);
    _exit(127);
  }
  close(pipe_fds[1]);
  FILE *stream = fdopen(pipe_fds[0], "r");
  if (!stream) {
    close(pipe_fds[0]);
    waitpid(pid, NULL, 0);
    return NULL;
  }
  *child_pid = pid;
  return stream;
}

// 关闭文件流管道并回收等待 Git 子进程正常退出
static bool close_git_stream(FILE *stream, pid_t child_pid) {
  fclose(stream);
  int status = 0;
  return waitpid(child_pid, &status, 0) == child_pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0;
}

// 通过 Git 命令解析出当前路径所属的 Git 仓库顶级根目录
static bool resolve_repository_root(const char *path, char *root, size_t size) {
  const char *arguments[] = {"rev-parse", "--show-toplevel", NULL};
  pid_t child_pid;
  FILE *stream = open_git_stream(path, arguments, &child_pid);
  if (!stream)
    return false;
  bool read_ok = fgets(root, (int)size, stream) != NULL;
  bool command_ok = close_git_stream(stream, child_pid);
  if (!read_ok || !command_ok)
    return false;
  root[strcspn(root, "\r\n")] = '\0';
  return root[0] != '\0';
}

// 辅助函数：判断文件路径中是否包含指定字符序列
static bool contains(const char *path, const char *needle) {
  return strstr(path, needle) != NULL;
}

// 将文件路径字符串分类，区分系统生成文件、测试、文档或常规业务代码
static const char *classify_path(const char *path) {
  const char *extension = strrchr(path, '.');
  if (contains(path, "node_modules/") || contains(path, "vendor/") ||
      contains(path, "dist/") || contains(path, "build/") ||
      contains(path, "generated/") || contains(path, "Pods/") ||
      (extension && strcmp(extension, ".lock") == 0))
    return "generated";
  if (contains(path, "test/") || contains(path, "tests/") ||
      contains(path, "__tests__/") || contains(path, "spec/") ||
      strstr(path, "_test.") || strstr(path, ".test.") ||
      strstr(path, ".spec."))
    return "test";
  if ((extension &&
       (strcmp(extension, ".md") == 0 || strcmp(extension, ".mdx") == 0 ||
        strcmp(extension, ".rst") == 0 || strcmp(extension, ".txt") == 0)) ||
      contains(path, "docs/") || contains(path, "doc/"))
    return "documentation";
  if (extension) {
    const char *code[] = {
        ".c",    ".h",    ".cc",   ".cpp", ".hpp", ".m",      ".mm",  ".swift",
        ".go",   ".rs",   ".java", ".kt",  ".kts", ".py",     ".rb",  ".php",
        ".js",   ".jsx",  ".ts",   ".tsx", ".vue", ".svelte", ".css", ".scss",
        ".less", ".html", ".sql",  ".sh",  ".zsh"};
    for (size_t i = 0; i < sizeof(code) / sizeof(code[0]); i++)
      if (strcmp(extension, code[i]) == 0)
        return "business";
  }
  return "other";
}

// 辅助函数：执行简单的 SQLite 无返回结果集查询
static bool execute(sqlite3 *db, const char *sql) {
  return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

// 将解析出的 Git 提交记录（包含 Hash、时间和作者）写入数据库
static bool insert_commit(sqlite3 *db, const char *repo, const char *hash,
                          const char *authored_at, const char *author,
                          bool *inserted) {
  sqlite3_stmt *stmt = NULL;
  const char *sql = "INSERT OR IGNORE INTO "
                    "git_commits(repo_path,commit_hash,authored_at,author_name)"
                    " VALUES(?1,?2,?3,?4)";
  bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
  if (ok) {
    sqlite3_bind_text(stmt, 1, repo, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, authored_at, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, author, -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
    *inserted = ok && sqlite3_changes(db) > 0;
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 记录某次 Git 提交关联的特定文件统计信息，包括增删行数
static bool insert_file(sqlite3 *db, const char *repo, const char *hash,
                        const char *file_path, long added, long deleted,
                        bool *inserted) {
  sqlite3_stmt *stmt = NULL;
  const char *sql = "INSERT OR IGNORE INTO git_commit_files"
                    "(repo_path,commit_hash,file_path,category,lines_added,"
                    "lines_deleted) VALUES(?1,?2,?3,?4,?5,?6)";
  bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
  if (ok) {
    sqlite3_bind_text(stmt, 1, repo, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, classify_path(file_path), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, added);
    sqlite3_bind_int64(stmt, 6, deleted);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
    *inserted = ok && sqlite3_changes(db) > 0;
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 对 Git 版本控制中添加的代码行内容计算 SHA256 指纹信息，并将其保存到数据库
static bool insert_git_fingerprint(sqlite3 *db, const char *repo,
                                   const char *hash, const char *file_path,
                                   long ordinal, const unsigned char *content,
                                   size_t content_length) {
  if (content_length == 0)
    return true;
  if (content[content_length - 1] == '\r')
    content_length--;
  if (content_length == 0)
    return true;
  char fingerprint[65];
  sha256_hex(content, content_length, fingerprint);
  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "INSERT OR IGNORE INTO git_line_fingerprints"
      "(repo_path,commit_hash,file_path,line_ordinal,category,fingerprint) "
      "VALUES(?1,?2,?3,?4,?5,?6)";
  bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK;
  if (ok) {
    sqlite3_bind_text(stmt, 1, repo, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, ordinal);
    sqlite3_bind_text(stmt, 5, classify_path(file_path), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, fingerprint, -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  sqlite3_finalize(stmt);
  return ok;
}

// 调用 git log 输出所有代码更新的 diff 内容，进而提取指纹数据进行归档追踪
static bool import_git_fingerprints(sqlite3 *db, const char *repo_root) {
  const char *arguments[] = {
      "log",          "--all",         "--reverse",
      "--no-renames", "--no-ext-diff", "--format=__AGENTSTAT_COMMIT__%H",
      "--patch",      "--unified=0",   NULL};
  pid_t child_pid;
  FILE *stream = open_git_stream(repo_root, arguments, &child_pid);
  if (!stream)
    return false;
  char line[65536];
  char current_hash[128] = "";
  char current_file[PATH_MAX] = "";
  long ordinal = 0;
  bool ok = true;
  while (ok && fgets(line, sizeof(line), stream)) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
      line[--length] = '\0';
    if (strncmp(line, "__AGENTSTAT_COMMIT__", 20) == 0) {
      snprintf(current_hash, sizeof(current_hash), "%s", line + 20);
      current_file[0] = '\0';
      ordinal = 0;
    } else if (strncmp(line, "+++ b/", 6) == 0) {
      snprintf(current_file, sizeof(current_file), "%s", line + 6);
      ordinal = 0;
    } else if (strncmp(line, "+++ /dev/null", 13) == 0) {
      current_file[0] = '\0';
    } else if (current_hash[0] && current_file[0] && line[0] == '+' &&
               strncmp(line, "+++", 3) != 0) {
      ok = insert_git_fingerprint(db, repo_root, current_hash, current_file,
                                  ordinal++, (const unsigned char *)(line + 1),
                                  length - 1);
    }
  }
  if (!close_git_stream(stream, child_pid))
    ok = false;
  return ok;
}

// 核心对外接口：扫描给定路径寻找 Git
// 仓库，导入包含日志、文件统计及指纹在内的全部变迁记录
bool sync_git_repository(const char *path, GitImportResult *result) {
  if (!path || !result)
    return false;
  memset(result, 0, sizeof(*result));
  char repo_root[PATH_MAX];
  if (!resolve_repository_root(path, repo_root, sizeof(repo_root)) ||
      !initialize_storage())
    return false;

  char db_path[512];
  get_db_file_path(db_path, sizeof(db_path));
  sqlite3 *db = NULL;
  if (sqlite3_open(db_path, &db) != SQLITE_OK)
    return false;
  sqlite3_busy_timeout(db, 5000);
  execute(db, "PRAGMA foreign_keys=ON");
  bool ok = execute(db, "BEGIN IMMEDIATE");
  sqlite3_stmt *repo_stmt = NULL;
  if (ok)
    ok = sqlite3_prepare_v2(
             db,
             "INSERT INTO git_repositories(repo_path) VALUES(?1) "
             "ON CONFLICT(repo_path) DO UPDATE SET synced_at=CURRENT_TIMESTAMP",
             -1, &repo_stmt, NULL) == SQLITE_OK;
  if (ok) {
    sqlite3_bind_text(repo_stmt, 1, repo_root, -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(repo_stmt) == SQLITE_DONE;
  }
  sqlite3_finalize(repo_stmt);

  const char *arguments[] = {"log",
                             "--all",
                             "--reverse",
                             "--no-renames",
                             "--format=__AGENTSTAT_COMMIT__%H%x09%aI%x09%an",
                             "--numstat",
                             NULL};
  pid_t child_pid;
  FILE *stream = ok ? open_git_stream(repo_root, arguments, &child_pid) : NULL;
  if (ok && !stream)
    ok = false;
  char line[16384];
  char current_hash[128] = "";
  while (ok && fgets(line, sizeof(line), stream)) {
    line[strcspn(line, "\r\n")] = '\0';
    if (strncmp(line, "__AGENTSTAT_COMMIT__", 20) == 0) {
      char *hash = line + 20;
      char *timestamp = strchr(hash, '\t');
      if (!timestamp) {
        ok = false;
        break;
      }
      *timestamp++ = '\0';
      char *author = strchr(timestamp, '\t');
      if (!author) {
        ok = false;
        break;
      }
      *author++ = '\0';
      snprintf(current_hash, sizeof(current_hash), "%s", hash);
      bool inserted = false;
      ok = insert_commit(db, repo_root, current_hash, timestamp, author,
                         &inserted);
      result->commits_scanned++;
      if (inserted)
        result->commits_imported++;
    } else if (current_hash[0] && line[0]) {
      char *added_text = line;
      char *deleted_text = strchr(added_text, '\t');
      if (!deleted_text)
        continue;
      *deleted_text++ = '\0';
      char *file_path = strchr(deleted_text, '\t');
      if (!file_path)
        continue;
      *file_path++ = '\0';
      long added =
          strcmp(added_text, "-") == 0 ? 0 : strtol(added_text, NULL, 10);
      long deleted =
          strcmp(deleted_text, "-") == 0 ? 0 : strtol(deleted_text, NULL, 10);
      bool inserted = false;
      ok = insert_file(db, repo_root, current_hash, file_path, added, deleted,
                       &inserted);
      if (inserted)
        result->files_imported++;
    }
  }
  if (stream && !close_git_stream(stream, child_pid))
    ok = false;
  if (ok)
    ok = import_git_fingerprints(db, repo_root);
  if (ok)
    ok = execute(db, "COMMIT");
  else
    execute(db, "ROLLBACK");
  sqlite3_close(db);
  return ok;
}
