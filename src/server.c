#include "server.h"
#include "agentstat.h"
#include "storage.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 65536
#define MAX_CONCURRENT_CLIENTS 64
#define SOCKET_TIMEOUT_SEC 5

// 全局服务运行状态与套接字，供信号捕获优雅退出
static volatile sig_atomic_t g_server_running = 1;
static int g_server_fd = -1;

// 当前活跃客户端线程计数与锁
static pthread_mutex_t g_client_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_active_clients = 0;

// 客户端请求任务参数结构体
typedef struct {
  int client_fd;
  struct sockaddr_in client_addr;
} ClientTask;

// 信号处理函数：捕获 SIGINT / SIGTERM 触发优雅退出
static void handle_signal(int sig) {
  (void)sig;
  g_server_running = 0;
  if (g_server_fd >= 0) {
    shutdown(g_server_fd, SHUT_RDWR);
    close(g_server_fd);
    g_server_fd = -1;
  }
}

// 转义 JSON 字符串
static void json_escape(const char *input, char *output, size_t size) {
  size_t used = 0;
  if (!input)
    input = "";
  for (const unsigned char *p = (const unsigned char *)input;
       *p && used + 2 < size; p++) {
    if (*p == '"' || *p == '\\') {
      output[used++] = '\\';
      output[used++] = (char)*p;
    } else if (*p == '\n' || *p == '\r' || *p == '\t') {
      output[used++] = ' ';
    } else if (*p >= 0x20) {
      output[used++] = (char)*p;
    }
  }
  output[used] = '\0';
}

// 根据文件扩展名返回标准的 MIME Content-Type
static const char *get_mime_type(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot)
    return "application/octet-stream";

  if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0)
    return "text/html; charset=utf-8";
  if (strcasecmp(dot, ".css") == 0)
    return "text/css; charset=utf-8";
  if (strcasecmp(dot, ".js") == 0 || strcasecmp(dot, ".mjs") == 0)
    return "application/javascript; charset=utf-8";
  if (strcasecmp(dot, ".json") == 0)
    return "application/json; charset=utf-8";
  if (strcasecmp(dot, ".svg") == 0)
    return "image/svg+xml";
  if (strcasecmp(dot, ".png") == 0)
    return "image/png";
  if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
    return "image/jpeg";
  if (strcasecmp(dot, ".gif") == 0)
    return "image/gif";
  if (strcasecmp(dot, ".ico") == 0)
    return "image/x-icon";
  if (strcasecmp(dot, ".woff2") == 0)
    return "font/woff2";
  if (strcasecmp(dot, ".woff") == 0)
    return "font/woff";
  if (strcasecmp(dot, ".ttf") == 0)
    return "font/ttf";
  if (strcasecmp(dot, ".txt") == 0)
    return "text/plain; charset=utf-8";

  return "application/octet-stream";
}

// 发送完整的 HTTP 响应报文
static void send_http_response(int client_fd, const char *status,
                               const char *content_type, const char *body,
                               size_t body_len, const char *extra_headers,
                               bool is_head) {
  char header[2048];
  int header_len = snprintf(
      header, sizeof(header),
      "HTTP/1.1 %s\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %zu\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, POST, OPTIONS, HEAD\r\n"
      "Access-Control-Allow-Headers: Content-Type, Authorization, Accept, X-Requested-With\r\n"
      "%s"
      "Connection: close\r\n\r\n",
      status, content_type, body_len, extra_headers ? extra_headers : "");

  if (header_len > 0) {
    send(client_fd, header, (size_t)header_len, 0);
  }
  if (!is_head && body && body_len > 0) {
    send(client_fd, body, body_len, 0);
  }
}

// 统一的 JSON 响应发送函数（禁用缓存，保证统计数据实时性）
static void send_json_response(int client_fd, const char *status,
                               const char *json_body, bool is_head) {
  size_t len = json_body ? strlen(json_body) : 0;
  send_http_response(client_fd, status, "application/json; charset=utf-8",
                     json_body, len,
                     "Cache-Control: no-cache, no-store, must-revalidate\r\n",
                     is_head);
}

// 统一的 304 Not Modified 响应
static void send_304_not_modified(int client_fd, const char *etag) {
  char headers[512];
  snprintf(headers, sizeof(headers),
           "ETag: %s\r\n"
           "Cache-Control: public, max-age=3600\r\n",
           etag);
  send_http_response(client_fd, "304 Not Modified", "text/html", NULL, 0,
                     headers, true);
}

// 统一的错误响应
static void send_error_response(int client_fd, int code, const char *msg,
                                bool is_head) {
  char status[64];
  char json[256];
  if (code == 404) {
    strcpy(status, "404 Not Found");
  } else if (code == 400) {
    strcpy(status, "400 Bad Request");
  } else if (code == 403) {
    strcpy(status, "403 Forbidden");
  } else {
    strcpy(status, "500 Internal Server Error");
  }
  snprintf(json, sizeof(json), "{\"error\":\"%s\"}", msg ? msg : "unknown");
  send_json_response(client_fd, status, json, is_head);
}

// 处理 CORS Preflight 预检请求
static void handle_options(int client_fd) {
  const char *extra = "Access-Control-Max-Age: 86400\r\n";
  send_http_response(client_fd, "204 No Content", "text/plain", NULL, 0, extra,
                     true);
}

// 处理 /api/summary
static void handle_api_summary(int client_fd, bool is_head) {
  AgentUsageStats usage;
  AgentCodeStats code;
  AgentAttributionStats attribution;
  if (!load_usage_stats(&usage) || !load_code_stats(&code) ||
      !load_attribution_stats(&attribution)) {
    send_error_response(client_fd, 500, "failed to load real analytics",
                        is_head);
    return;
  }
  AgentModelStats models[256];
  int model_count = load_model_stats(models, 256);
  long priced_calls = 0, total_calls = 0;
  double cost = 0;
  if (model_count < 0) {
    send_error_response(client_fd, 500, "failed to load model pricing", is_head);
    return;
  }
  for (int i = 0; i < model_count; i++) {
    total_calls += models[i].model_calls;
    if (models[i].pricing_configured) {
      priced_calls += models[i].model_calls;
      cost += models[i].estimated_cost_usd;
    }
  }
  double coverage =
      total_calls > 0 ? (double)priced_calls * 100.0 / (double)total_calls : 0;
  char json_buf[4096];
  snprintf(
      json_buf, sizeof(json_buf),
      "{"
      "\"total_sessions\":%ld,"
      "\"total_input_tokens\":%ld,"
      "\"total_output_tokens\":%ld,"
      "\"total_tokens\":%ld,"
      "\"total_lines_suggested\":%ld,"
      "\"total_lines_accepted\":%ld,"
      "\"line_acceptance_rate\":%.2f,"
      "\"tool_calls\":%ld,\"code_changes\":%ld,"
      "\"metric_scope\":\"real_imported_events\",\"estimated_cost_usd\":%.8f,"
      "\"priced_model_calls\":%ld,\"total_model_calls\":%ld,\"cost_coverage\":%"
      ".2f"
      "}",
      usage.total_sessions, usage.input_tokens, usage.output_tokens,
      usage.input_tokens + usage.output_tokens, attribution.candidate_lines,
      attribution.accepted_lines, attribution.acceptance_rate, usage.tool_calls,
      code.change_events, cost, priced_calls, total_calls, coverage);

  send_json_response(client_fd, "200 OK", json_buf, is_head);
}

// 处理 /api/sessions 和 /api/records
static void handle_api_records(int client_fd, bool is_head) {
  AgentSessionStats records[100];
  int count = load_recent_session_stats(records, 100);
  if (count < 0) {
    send_error_response(client_fd, 500, "failed to load sessions", is_head);
    return;
  }
  size_t buf_size = (size_t)(count > 0 ? count : 1) * 2048 + 1024;
  char *json_buf = (char *)malloc(buf_size);
  if (!json_buf) {
    send_error_response(client_fd, 500, "out of memory", is_head);
    return;
  }

  strcpy(json_buf, "[");
  for (int i = 0; i < count; i++) {
    char item[2048], session_id[512], source[64], cwd[1200], started_at[256],
        models[700];
    json_escape(records[i].session_id, session_id, sizeof(session_id));
    json_escape(records[i].source, source, sizeof(source));
    json_escape(records[i].cwd, cwd, sizeof(cwd));
    json_escape(records[i].started_at, started_at, sizeof(started_at));
    json_escape(records[i].models, models, sizeof(models));
    snprintf(item, sizeof(item),
             "{"
             "\"session_id\":\"%s\","
             "\"source\":\"%s\",\"cwd\":\"%s\",\"started_at\":\"%s\","
             "\"models\":\"%s\","
             "\"input_tokens\":%ld,"
             "\"output_tokens\":%ld,"
             "\"tool_calls\":%ld,\"code_changes\":%ld"
             "}%s",
             session_id, source, cwd, started_at, models,
             records[i].input_tokens, records[i].output_tokens,
             records[i].tool_calls, records[i].code_changes,
             (i == count - 1) ? "" : ",");
    strcat(json_buf, item);
  }
  strcat(json_buf, "]");

  send_json_response(client_fd, "200 OK", json_buf, is_head);
  free(json_buf);
}

// 处理 /api/models
static void handle_api_models(int client_fd, bool is_head) {
  AgentModelStats rows[128];
  int count = load_model_stats(rows, 128);
  if (count < 0) {
    send_error_response(client_fd, 500, "failed to load models", is_head);
    return;
  }
  char *json = malloc((size_t)(count > 0 ? count : 1) * 768 + 16);
  if (!json) {
    send_error_response(client_fd, 500, "out of memory", is_head);
    return;
  }
  strcpy(json, "[");
  for (int i = 0; i < count; i++) {
    char source[64], model[256], item[768];
    json_escape(rows[i].source, source, sizeof(source));
    json_escape(rows[i].model, model, sizeof(model));
    snprintf(
        item, sizeof(item),
        "%s{\"source\":\"%s\",\"model\":\"%s\",\"model_calls\":%ld,"
        "\"selections\":%ld,\"input_tokens\":%ld,\"cached_input_tokens\":%ld,"
        "\"cache_write_input_tokens\":%ld,\"output_tokens\":%ld,\"pricing_"
        "configured\":%s,\"input_rate\":%.8f,\"cache_read_rate\":%.8f,\"cache_"
        "write_rate\":%.8f,\"output_rate\":%.8f,\"estimated_cost_usd\":%.8f}",
        i ? "," : "", source, model, rows[i].model_calls, rows[i].selections,
        rows[i].input_tokens, rows[i].cached_input_tokens,
        rows[i].cache_write_input_tokens, rows[i].output_tokens,
        rows[i].pricing_configured ? "true" : "false", rows[i].input_rate,
        rows[i].cache_read_rate, rows[i].cache_write_rate, rows[i].output_rate,
        rows[i].estimated_cost_usd);
    strcat(json, item);
  }
  strcat(json, "]");
  send_json_response(client_fd, "200 OK", json, is_head);
  free(json);
}

// 处理 /api/tools
static void handle_api_tools(int client_fd, bool is_head) {
  AgentToolStats rows[100];
  int count = load_tool_stats(rows, 100);
  if (count < 0) {
    send_error_response(client_fd, 500, "failed to load tools", is_head);
    return;
  }
  char *json = malloc((size_t)(count > 0 ? count : 1) * 700 + 16);
  if (!json) {
    send_error_response(client_fd, 500, "out of memory", is_head);
    return;
  }
  strcpy(json, "[");
  for (int i = 0; i < count; i++) {
    char name[300], detail[300], item[700];
    json_escape(rows[i].tool_name, name, sizeof(name));
    json_escape(rows[i].detail_name, detail, sizeof(detail));
    snprintf(item, sizeof(item),
             "%s{\"tool_name\":\"%s\",\"detail_name\":\"%s\",\"calls\":%ld,"
             "\"mcp_calls\":%ld}",
             i ? "," : "", name, detail, rows[i].calls, rows[i].mcp_calls);
    strcat(json, item);
  }
  strcat(json, "]");
  send_json_response(client_fd, "200 OK", json, is_head);
  free(json);
}

// 处理 /api/projects
static void handle_api_projects(int client_fd, bool is_head) {
  AgentProjectStats rows[128];
  int count = load_project_stats(rows, 128);
  if (count < 0) {
    send_error_response(client_fd, 500, "failed to load projects", is_head);
    return;
  }
  char *json = malloc((size_t)(count > 0 ? count : 1) * 3000 + 16);
  if (!json) {
    send_error_response(client_fd, 500, "out of memory", is_head);
    return;
  }
  strcpy(json, "[");
  for (int i = 0; i < count; i++) {
    char name[600], path[2100], item[3000];
    json_escape(rows[i].project, name, sizeof(name));
    json_escape(rows[i].project_path, path, sizeof(path));
    snprintf(item, sizeof(item),
             "%s{\"project\":\"%s\",\"project_path\":\"%s\",\"sessions\":%ld,"
             "\"sources\":%ld,\"input_tokens\":%ld,\"output_tokens\":%ld,"
             "\"tool_calls\":%ld,\"code_changes\":%ld}",
             i ? "," : "", name, path, rows[i].sessions, rows[i].sources,
             rows[i].input_tokens, rows[i].output_tokens, rows[i].tool_calls,
             rows[i].code_changes);
    strcat(json, item);
  }
  strcat(json, "]");
  send_json_response(client_fd, "200 OK", json, is_head);
  free(json);
}

// 处理 /api/mcp 与 /api/skills
static void handle_api_capabilities(int client_fd, bool skills, bool is_head) {
  AgentCapabilityStats rows[128];
  int count = skills ? load_skill_stats(rows, 128) : load_mcp_stats(rows, 128);
  if (count < 0) {
    send_error_response(client_fd, 500, "failed to load capabilities", is_head);
    return;
  }
  char *json = malloc((size_t)(count > 0 ? count : 1) * 1200 + 16);
  if (!json) {
    send_error_response(client_fd, 500, "out of memory", is_head);
    return;
  }
  strcpy(json, "[");
  for (int i = 0; i < count; i++) {
    char name[520], detail[520], source[80], item[1200];
    json_escape(rows[i].name, name, sizeof(name));
    json_escape(rows[i].detail, detail, sizeof(detail));
    json_escape(rows[i].source, source, sizeof(source));
    snprintf(
        item, sizeof(item),
        "%s{\"name\":\"%s\",\"detail\":\"%s\",\"source\":\"%s\",\"calls\":%ld}",
        i ? "," : "", name, detail, source, rows[i].calls);
    strcat(json, item);
  }
  strcat(json, "]");
  send_json_response(client_fd, "200 OK", json, is_head);
  free(json);
}

// 处理 /api/timeseries
static void handle_api_timeseries(int client_fd, const char *period,
                                  bool is_head) {
  if (!period ||
      (strcmp(period, "day") != 0 && strcmp(period, "week") != 0 &&
       strcmp(period, "month") != 0)) {
    period = "day";
  }
  int limit = strcmp(period, "day") == 0 ? 30 : 12;
  AgentPeriodStats rows[30];
  int count = load_period_stats(rows, limit, period);
  if (count < 0) {
    send_error_response(client_fd, 400, "failed to load time series", is_head);
    return;
  }
  char *json = malloc((size_t)(count > 0 ? count : 1) * 700 + 128);
  if (!json) {
    send_error_response(client_fd, 500, "out of memory", is_head);
    return;
  }
  int written =
      snprintf(json, (size_t)(count > 0 ? count : 1) * 700 + 128,
               "{\"period\":\"%s\",\"timezone\":\"local\",\"rows\":[", period);
  size_t capacity = (size_t)(count > 0 ? count : 1) * 700 + 128;
  for (int i = 0; i < count && written > 0 && (size_t)written < capacity; i++) {
    written += snprintf(
        json + written, capacity - (size_t)written,
        "%s{\"period_start\":\"%s\",\"sessions\":%ld,\"model_calls\":%ld,"
        "\"input_tokens\":%ld,"
        "\"cached_input_tokens\":%ld,\"cache_write_input_tokens\":%ld,\"output_"
        "tokens\":%ld,"
        "\"tool_calls\":%ld,\"mcp_calls\":%ld,\"code_changes\":%ld,\"lines_"
        "added\":%ld,"
        "\"lines_deleted\":%ld,\"priced_model_calls\":%ld,\"estimated_cost_"
        "usd\":%.8f}",
        i ? "," : "", rows[i].period_start, rows[i].sessions,
        rows[i].model_calls, rows[i].input_tokens, rows[i].cached_input_tokens,
        rows[i].cache_write_input_tokens, rows[i].output_tokens,
        rows[i].tool_calls, rows[i].mcp_calls, rows[i].code_changes,
        rows[i].lines_added, rows[i].lines_deleted, rows[i].priced_model_calls,
        rows[i].estimated_cost_usd);
  }
  if (written > 0 && (size_t)written < capacity - 3)
    snprintf(json + written, capacity - (size_t)written, "]}");
  send_json_response(client_fd, "200 OK", json, is_head);
  free(json);
}

// 处理 /api/usage
static void handle_api_usage(int client_fd, bool is_head) {
  AgentUsageStats stats;
  if (!load_usage_stats(&stats)) {
    send_error_response(client_fd, 500, "failed to load usage stats", is_head);
    return;
  }
  char json_buf[8192];
  int written = snprintf(
      json_buf, sizeof(json_buf),
      "{\"total_sessions\":%ld,\"model_calls\":%ld,\"input_tokens\":%ld,"
      "\"cached_input_tokens\":%ld,\"cache_write_input_tokens\":%ld,"
      "\"output_tokens\":%ld,\"reasoning_output_tokens\":%ld,"
      "\"tool_calls\":%ld,\"mcp_calls\":%ld,\"distinct_tools\":%ld,"
      "\"cache_hit_rate\":%.2f,\"sources\":[",
      stats.total_sessions, stats.model_calls, stats.input_tokens,
      stats.cached_input_tokens, stats.cache_write_input_tokens,
      stats.output_tokens, stats.reasoning_output_tokens, stats.tool_calls,
      stats.mcp_calls, stats.distinct_tools, stats.cache_hit_rate);
  AgentSourceStats sources[MAX_AGENT_SOURCES];
  int source_count = load_source_stats(sources, MAX_AGENT_SOURCES);
  for (int i = 0;
       i < source_count && written > 0 && (size_t)written < sizeof(json_buf);
       i++) {
    written +=
        snprintf(json_buf + written, sizeof(json_buf) - (size_t)written,
                 "%s{\"source\":\"%s\",\"sessions\":%ld,\"model_calls\":%ld,"
                 "\"input_tokens\":%ld,"
                 "\"cached_input_tokens\":%ld,\"output_tokens\":%ld,\"tool_"
                 "calls\":%ld,\"code_changes\":%ld}",
                 i ? "," : "", sources[i].source, sources[i].sessions,
                 sources[i].model_calls, sources[i].input_tokens,
                 sources[i].cached_input_tokens, sources[i].output_tokens,
                 sources[i].tool_calls, sources[i].code_changes);
  }
  if (written > 0 && (size_t)written < sizeof(json_buf) - 2)
    snprintf(json_buf + written, sizeof(json_buf) - (size_t)written, "]}");
  send_json_response(client_fd, "200 OK", json_buf, is_head);
}

// 处理 /api/code
static void handle_api_code(int client_fd, bool is_head) {
  AgentCodeStats stats;
  if (!load_code_stats(&stats)) {
    send_error_response(client_fd, 500, "failed to load code stats", is_head);
    return;
  }
  char json_buf[2048];
  snprintf(json_buf, sizeof(json_buf),
           "{\"change_events\":%ld,\"files_changed\":%ld,"
           "\"lines_added\":%ld,\"lines_deleted\":%ld,"
           "\"business_lines_added\":%ld,\"test_lines_added\":%ld,"
           "\"documentation_lines_added\":%ld,\"generated_lines_added\":%ld,"
           "\"other_lines_added\":%ld,\"business_code_share\":%.2f,"
           "\"metric_scope\":\"agent_applied_changes\"}",
           stats.change_events, stats.files_changed, stats.lines_added,
           stats.lines_deleted, stats.business_lines_added,
           stats.test_lines_added, stats.documentation_lines_added,
           stats.generated_lines_added, stats.other_lines_added,
           stats.business_code_share);
  send_json_response(client_fd, "200 OK", json_buf, is_head);
}

// 处理 /api/git
static void handle_api_git(int client_fd, bool is_head) {
  AgentGitStats stats;
  if (!load_git_stats(&stats)) {
    send_error_response(client_fd, 500, "failed to load git stats", is_head);
    return;
  }
  char json_buf[2048];
  snprintf(json_buf, sizeof(json_buf),
           "{\"repositories\":%ld,\"commits\":%ld,\"files_changed\":%ld,"
           "\"lines_added\":%ld,\"lines_deleted\":%ld,"
           "\"business_lines_added\":%ld,\"test_lines_added\":%ld,"
           "\"documentation_lines_added\":%ld,\"generated_lines_added\":%ld,"
           "\"other_lines_added\":%ld}",
           stats.repositories, stats.commits, stats.files_changed,
           stats.lines_added, stats.lines_deleted, stats.business_lines_added,
           stats.test_lines_added, stats.documentation_lines_added,
           stats.generated_lines_added, stats.other_lines_added);
  send_json_response(client_fd, "200 OK", json_buf, is_head);
}

// 处理 /api/attribution
static void handle_api_attribution(int client_fd, bool is_head) {
  AgentAttributionStats stats;
  if (!load_attribution_stats(&stats)) {
    send_error_response(client_fd, 500, "failed to load attribution stats",
                        is_head);
    return;
  }
  char json_buf[2048];
  snprintf(json_buf, sizeof(json_buf),
           "{\"candidate_lines\":%ld,\"accepted_lines\":%ld,"
           "\"business_candidate_lines\":%ld,\"business_accepted_lines\":%ld,"
           "\"test_candidate_lines\":%ld,\"test_accepted_lines\":%ld,"
           "\"documentation_candidate_lines\":%ld,\"documentation_accepted_"
           "lines\":%ld,"
           "\"acceptance_rate\":%.2f,\"business_acceptance_rate\":%.2f,"
           "\"method\":\"exact_sha256_line_match\",\"generated_files_"
           "excluded\":true}",
           stats.candidate_lines, stats.accepted_lines,
           stats.business_candidate_lines, stats.business_accepted_lines,
           stats.test_candidate_lines, stats.test_accepted_lines,
           stats.documentation_candidate_lines,
           stats.documentation_accepted_lines, stats.acceptance_rate,
           stats.business_acceptance_rate);
  send_json_response(client_fd, "200 OK", json_buf, is_head);
}

// 安全的分发静态文件（支持 MIME 识别、路径防穿越、ETag 协商缓存）
static void handle_serve_static(int client_fd, const char *raw_path,
                                const char *if_none_match, bool is_head) {
  // 路径安全防御：禁止包含 ".." 越权访问上层目录
  if (strstr(raw_path, "..") != NULL) {
    send_error_response(client_fd, 403, "Access denied", is_head);
    return;
  }

  const char *subpath = raw_path;
  if (strcmp(subpath, "/") == 0) {
    subpath = "/index.html";
  } else if (strncmp(subpath, "/web/", 5) == 0) {
    subpath = raw_path + 4; // 保留前导斜杠
  }

  // 候选搜索根目录（支持项目根目录、bin/ 目录执行、环境变量重定向及系统安装目录）
  const char *custom_web = getenv("AGENTSTAT_WEB_DIR");
  const char *candidate_dirs[] = {
      custom_web,
      "web",
      "../web",
      "/usr/local/share/agentstat/web",
      NULL};

  char file_path[512] = {0};
  struct stat st;
  bool found = false;

  for (int i = 0; candidate_dirs[i] != NULL; i++) {
    if (!candidate_dirs[i] || candidate_dirs[i][0] == '\0')
      continue;
    snprintf(file_path, sizeof(file_path), "%s%s", candidate_dirs[i], subpath);
    if (stat(file_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
      found = true;
      break;
    }
  }

  if (!found) {
    // 如果请求的是根路径且找不到 index.html，返回极简后备页
    if (strcmp(subpath, "/index.html") == 0) {
      const char *fallback_html =
          "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta "
          "charset=\"UTF-8\"><title>AgentStat</title></head><body><h1>AgentStat "
          "服务已启动</h1><p>未找到 "
          "web/index.html，请确保项目 web 目录完整。</p></body></html>";
      send_http_response(client_fd, "200 OK", "text/html; charset=utf-8",
                         fallback_html, strlen(fallback_html), NULL, is_head);
      return;
    }
    send_error_response(client_fd, 404, "File not found", is_head);
    return;
  }

  // 生成 ETag 指纹（基于修改时间与文件尺寸）
  char etag[64];
  snprintf(etag, sizeof(etag), "\"%lx-%lx\"", (unsigned long)st.st_mtime,
           (unsigned long)st.st_size);

  // 协商缓存：客户端 ETag 匹配时直接返回 304
  if (if_none_match && strstr(if_none_match, etag) != NULL) {
    send_304_not_modified(client_fd, etag);
    return;
  }

  FILE *fp = fopen(file_path, "rb");
  if (!fp) {
    send_error_response(client_fd, 500, "Unable to read file", is_head);
    return;
  }

  char *content = malloc((size_t)st.st_size + 1);
  if (!content) {
    fclose(fp);
    send_error_response(client_fd, 500, "Out of memory", is_head);
    return;
  }

  size_t read_bytes = fread(content, 1, (size_t)st.st_size, fp);
  fclose(fp);
  content[read_bytes] = '\0';

  const char *mime = get_mime_type(file_path);
  char extra_headers[256];
  snprintf(extra_headers, sizeof(extra_headers),
           "ETag: %s\r\n"
           "Cache-Control: public, max-age=3600\r\n",
           etag);

  send_http_response(client_fd, "200 OK", mime, content, read_bytes,
                     extra_headers, is_head);
  free(content);
}

// 解析单个 HTTP 请求并在线程中执行处理
static void process_client_request(int client_fd) {
  // 设置套接字超时时间（5秒），防止客户端挂起导致线程长期占用
  struct timeval tv;
  tv.tv_sec = SOCKET_TIMEOUT_SEC;
  tv.tv_usec = 0;
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

  char buffer[BUFFER_SIZE];
  ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (received <= 0) {
    return;
  }
  buffer[received] = '\0';

  // 解析请求行：METHOD URI HTTP/VERSION
  char method[16] = {0};
  char raw_uri[1024] = {0};
  char version[32] = {0};

  if (sscanf(buffer, "%15s %1023s %31s", method, raw_uri, version) < 2) {
    send_error_response(client_fd, 400, "Invalid HTTP Request", false);
    return;
  }

  bool is_options = (strcasecmp(method, "OPTIONS") == 0);
  bool is_head = (strcasecmp(method, "HEAD") == 0);
  bool is_get = (strcasecmp(method, "GET") == 0);

  if (is_options) {
    handle_options(client_fd);
    return;
  }

  if (!is_get && !is_head) {
    send_error_response(client_fd, 400, "Method Not Allowed", false);
    return;
  }

  // 提取 Path 与 Query String
  char path[1024] = {0};
  char query[1024] = {0};
  char *question_mark = strchr(raw_uri, '?');
  if (question_mark) {
    size_t path_len = (size_t)(question_mark - raw_uri);
    if (path_len >= sizeof(path))
      path_len = sizeof(path) - 1;
    strncpy(path, raw_uri, path_len);
    path[path_len] = '\0';
    strncpy(query, question_mark + 1, sizeof(query) - 1);
  } else {
    strncpy(path, raw_uri, sizeof(path) - 1);
  }

  // 提取 If-None-Match 请求头
  char if_none_match[256] = {0};
  const char *inm_ptr = strstr(buffer, "If-None-Match:");
  if (inm_ptr) {
    inm_ptr += 14;
    while (*inm_ptr == ' ' || *inm_ptr == '\t')
      inm_ptr++;
    const char *eol = strstr(inm_ptr, "\r\n");
    if (eol) {
      size_t val_len = (size_t)(eol - inm_ptr);
      if (val_len >= sizeof(if_none_match))
        val_len = sizeof(if_none_match) - 1;
      strncpy(if_none_match, inm_ptr, val_len);
      if_none_match[val_len] = '\0';
    }
  }

  // API 路由分发
  if (strcmp(path, "/api/summary") == 0) {
    handle_api_summary(client_fd, is_head);
  } else if (strcmp(path, "/api/usage") == 0) {
    handle_api_usage(client_fd, is_head);
  } else if (strcmp(path, "/api/code") == 0) {
    handle_api_code(client_fd, is_head);
  } else if (strcmp(path, "/api/git") == 0) {
    handle_api_git(client_fd, is_head);
  } else if (strcmp(path, "/api/attribution") == 0) {
    handle_api_attribution(client_fd, is_head);
  } else if (strcmp(path, "/api/models") == 0) {
    handle_api_models(client_fd, is_head);
  } else if (strcmp(path, "/api/tools") == 0) {
    handle_api_tools(client_fd, is_head);
  } else if (strcmp(path, "/api/projects") == 0) {
    handle_api_projects(client_fd, is_head);
  } else if (strcmp(path, "/api/mcp") == 0) {
    handle_api_capabilities(client_fd, false, is_head);
  } else if (strcmp(path, "/api/skills") == 0) {
    handle_api_capabilities(client_fd, true, is_head);
  } else if (strcmp(path, "/api/timeseries") == 0) {
    const char *period = "day";
    if (strstr(query, "period=week")) {
      period = "week";
    } else if (strstr(query, "period=month")) {
      period = "month";
    } else if (strstr(query, "period=day")) {
      period = "day";
    }
    handle_api_timeseries(client_fd, period, is_head);
  } else if (strcmp(path, "/api/sessions") == 0 ||
             strcmp(path, "/api/records") == 0) {
    handle_api_records(client_fd, is_head);
  } else {
    // 静态资源文件服务
    handle_serve_static(client_fd, path,
                        if_none_match[0] ? if_none_match : NULL, is_head);
  }
}

// 客户端处理线程函数
static void *client_thread_routine(void *arg) {
  ClientTask *task = (ClientTask *)arg;
  if (!task)
    return NULL;

  process_client_request(task->client_fd);

  close(task->client_fd);
  free(task);

  pthread_mutex_lock(&g_client_count_mutex);
  g_active_clients--;
  pthread_mutex_unlock(&g_client_count_mutex);

  return NULL;
}

// 启动嵌入式多线程 HTTP Web 服务器
int start_web_server(int port) {
  struct sockaddr_in address;
  int opt = 1;
  socklen_t addrlen = sizeof(address);

  // 注册信号处理以支持优雅停机
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  signal(SIGPIPE, SIG_IGN); // 忽略向已关闭 socket 发送数据引起的 SIGPIPE

  if ((g_server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Socket creation failed");
    return 1;
  }

  if (setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    perror("setsockopt SO_REUSEADDR failed");
    close(g_server_fd);
    return 1;
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons((uint16_t)port);

  if (bind(g_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Bind failed");
    close(g_server_fd);
    return 1;
  }

  if (listen(g_server_fd, 64) < 0) {
    perror("Listen failed");
    close(g_server_fd);
    return 1;
  }

  printf("====================================================================="
         "===\n");
  printf("   AgentStat 高性能多线程看板服务已启动: http://localhost:%d\n",
         port);
  printf("   支持并发请求、静态文件缓存(ETag)与优雅退出 (按 Ctrl+C 停止)\n");
  printf("====================================================================="
         "===\n\n");

  // 主循环接受并分发客户端连接
  while (g_server_running) {
    int client_fd = accept(g_server_fd, (struct sockaddr *)&address, &addrlen);
    if (client_fd < 0) {
      if (!g_server_running)
        break;
      if (errno == EINTR || errno == EAGAIN)
        continue;
      break;
    }

    pthread_mutex_lock(&g_client_count_mutex);
    if (g_active_clients >= MAX_CONCURRENT_CLIENTS) {
      pthread_mutex_unlock(&g_client_count_mutex);
      // 达到最大并发限制，快速返回 503
      send_error_response(client_fd, 503, "Server too busy", false);
      close(client_fd);
      continue;
    }
    g_active_clients++;
    pthread_mutex_unlock(&g_client_count_mutex);

    ClientTask *task = (ClientTask *)malloc(sizeof(ClientTask));
    if (!task) {
      pthread_mutex_lock(&g_client_count_mutex);
      g_active_clients--;
      pthread_mutex_unlock(&g_client_count_mutex);
      close(client_fd);
      continue;
    }

    task->client_fd = client_fd;
    task->client_addr = address;

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, client_thread_routine, task) != 0) {
      pthread_mutex_lock(&g_client_count_mutex);
      g_active_clients--;
      pthread_mutex_unlock(&g_client_count_mutex);
      free(task);
      close(client_fd);
      continue;
    }
    // 分离线程，避免线程句柄泄漏
    pthread_detach(thread_id);
  }

  if (g_server_fd >= 0) {
    close(g_server_fd);
    g_server_fd = -1;
  }

  printf("\n正在停止 AgentStat 看板服务...\n");
  // 等待剩余活跃请求完成（最多等待 2 秒）
  for (int i = 0; i < 20; i++) {
    pthread_mutex_lock(&g_client_count_mutex);
    int active = g_active_clients;
    pthread_mutex_unlock(&g_client_count_mutex);
    if (active == 0)
      break;
    usleep(100000);
  }

  printf("AgentStat 看板服务已安全停止。\n");
  return 0;
}
