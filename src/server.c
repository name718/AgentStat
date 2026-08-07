#include "server.h"
#include "agentstat.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 65536

// 构建并发送HTTP响应报文到客户端，包括状态码、Content-Type和响应体内容
static void send_response(int client_fd, const char *status, const char *content_type, const char *body) {
    char header[1024];
    int body_len = body ? strlen(body) : 0;
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n\r\n",
             status, content_type, body_len);
             
    send(client_fd, header, strlen(header), 0);
    if (body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

// 将字符串中的特殊字符(如双引号、斜杠、换行等)进行JSON转义，以确保生成的JSON格式合法且安全
static void json_escape(const char *input, char *output, size_t size) {
    size_t used = 0;
    if (!input) input = "";
    for (const unsigned char *p = (const unsigned char *)input; *p && used + 2 < size; p++) {
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

// 处理 /api/summary 接口请求，加载基础统计、代码统计和归因统计数据，并返回整体概况JSON
static void handle_api_summary(int client_fd) {
    AgentUsageStats usage;
    AgentCodeStats code;
    AgentAttributionStats attribution;
    if (!load_usage_stats(&usage) || !load_code_stats(&code) || !load_attribution_stats(&attribution)) {
        send_response(client_fd, "500 Internal Error", "application/json", "{\"error\":\"failed to load real analytics\"}");
        return;
    }
    char json_buf[4096];
    snprintf(json_buf, sizeof(json_buf),
             "{"
             "\"total_sessions\":%ld,"
             "\"total_input_tokens\":%ld,"
             "\"total_output_tokens\":%ld,"
             "\"total_tokens\":%ld,"
             "\"total_lines_suggested\":%ld,"
             "\"total_lines_accepted\":%ld,"
             "\"line_acceptance_rate\":%.2f,"
             "\"tool_calls\":%ld,\"code_changes\":%ld,"
             "\"metric_scope\":\"real_imported_events\",\"cost_available\":false"
             "}",
             usage.total_sessions, usage.input_tokens, usage.output_tokens,
             usage.input_tokens + usage.output_tokens,
             attribution.candidate_lines, attribution.accepted_lines,
             attribution.acceptance_rate, usage.tool_calls, code.change_events);
             
    send_response(client_fd, "200 OK", "application/json", json_buf);
}

// 处理会话记录查询接口请求，从存储中加载近期的会话数据并拼接为JSON数组响应
static void handle_api_records(int client_fd) {
    AgentSessionStats records[50];
    int count = load_recent_session_stats(records, 50);
    if (count < 0) { send_response(client_fd,"500 Internal Error","application/json","{\"error\":\"failed to load sessions\"}"); return; }
    size_t buf_size = (size_t)(count > 0 ? count : 1) * 2048 + 1024;
    char *json_buf = (char *)malloc(buf_size);
    if (!json_buf) {
        send_response(client_fd, "500 Internal Error", "text/plain", "Memory allocation error");
        return;
    }
    
    strcpy(json_buf, "[");
    for (int i = 0; i < count; i++) {
        char item[2048], session_id[512], source[64], cwd[1200], started_at[256], models[700];
        json_escape(records[i].session_id,session_id,sizeof(session_id));
        json_escape(records[i].source,source,sizeof(source));
        json_escape(records[i].cwd,cwd,sizeof(cwd));
        json_escape(records[i].started_at,started_at,sizeof(started_at));
        json_escape(records[i].models,models,sizeof(models));
        snprintf(item, sizeof(item),
                 "{"
                 "\"session_id\":\"%s\","
                 "\"source\":\"%s\",\"cwd\":\"%s\",\"started_at\":\"%s\","
                 "\"models\":\"%s\","
                 "\"input_tokens\":%ld,"
                 "\"output_tokens\":%ld,"
                 "\"tool_calls\":%ld,\"code_changes\":%ld"
                 "}%s",
                 session_id,source,cwd,started_at,models,
                 records[i].input_tokens,
                 records[i].output_tokens,
                 records[i].tool_calls,records[i].code_changes,
                 (i == count - 1) ? "" : ",");
        strcat(json_buf, item);
    }
    strcat(json_buf, "]");
    
    send_response(client_fd, "200 OK", "application/json", json_buf);
    free(json_buf);
}

// 处理模型调用统计接口请求，将各模型的调用次数、输入输出token等数据转为JSON数组返回
static void handle_api_models(int client_fd) {
    AgentModelStats rows[128];
    int count=load_model_stats(rows,128);
    if(count<0){send_response(client_fd,"500 Internal Error","application/json","{\"error\":\"failed to load models\"}");return;}
    char *json=malloc((size_t)(count>0?count:1)*512+16);
    if(!json){send_response(client_fd,"500 Internal Error","application/json","{\"error\":\"out of memory\"}");return;}
    strcpy(json,"[");
    for(int i=0;i<count;i++){
        char source[64],model[256],item[512];json_escape(rows[i].source,source,sizeof(source));json_escape(rows[i].model,model,sizeof(model));
        snprintf(item,sizeof(item),"%s{\"source\":\"%s\",\"model\":\"%s\",\"model_calls\":%ld,\"selections\":%ld,\"input_tokens\":%ld,\"cached_input_tokens\":%ld,\"output_tokens\":%ld}",i?",":"",source,model,rows[i].model_calls,rows[i].selections,rows[i].input_tokens,rows[i].cached_input_tokens,rows[i].output_tokens);
        strcat(json,item);
    }
    strcat(json,"]");send_response(client_fd,"200 OK","application/json",json);free(json);
}

// 处理工具调用统计接口请求，将各工具的使用次数等数据转为JSON数组返回
static void handle_api_tools(int client_fd) {
    AgentToolStats rows[50];int count=load_tool_stats(rows,50);
    if(count<0){send_response(client_fd,"500 Internal Error","application/json","{\"error\":\"failed to load tools\"}");return;}
    char *json=malloc((size_t)(count>0?count:1)*384+16);if(!json){send_response(client_fd,"500 Internal Error","application/json","{\"error\":\"out of memory\"}");return;}strcpy(json,"[");
    for(int i=0;i<count;i++){char name[300],item[384];json_escape(rows[i].tool_name,name,sizeof(name));snprintf(item,sizeof(item),"%s{\"tool_name\":\"%s\",\"calls\":%ld,\"mcp_calls\":%ld}",i?",":"",name,rows[i].calls,rows[i].mcp_calls);strcat(json,item);}strcat(json,"]");send_response(client_fd,"200 OK","application/json",json);free(json);
}

// 处理用法统计接口请求，组装整体资源使用情况和按来源细分的统计数据，并作为JSON返回
static void handle_api_usage(int client_fd) {
    AgentUsageStats stats;
    if (!load_usage_stats(&stats)) {
        send_response(client_fd, "500 Internal Error", "application/json", "{\"error\":\"failed to load usage stats\"}");
        return;
    }
    char json_buf[8192];
    int written = snprintf(json_buf, sizeof(json_buf),
             "{\"total_sessions\":%ld,\"model_calls\":%ld,\"input_tokens\":%ld,"
             "\"cached_input_tokens\":%ld,\"cache_write_input_tokens\":%ld,"
             "\"output_tokens\":%ld,\"reasoning_output_tokens\":%ld,"
             "\"tool_calls\":%ld,\"mcp_calls\":%ld,\"distinct_tools\":%ld,"
             "\"cache_hit_rate\":%.2f,\"sources\":[",
             stats.total_sessions, stats.model_calls, stats.input_tokens,
             stats.cached_input_tokens, stats.cache_write_input_tokens, stats.output_tokens,
             stats.reasoning_output_tokens, stats.tool_calls, stats.mcp_calls,
             stats.distinct_tools, stats.cache_hit_rate);
    AgentSourceStats sources[MAX_AGENT_SOURCES];
    int source_count = load_source_stats(sources, MAX_AGENT_SOURCES);
    for (int i = 0; i < source_count && written > 0 && (size_t)written < sizeof(json_buf); i++) {
        written += snprintf(json_buf + written, sizeof(json_buf) - (size_t)written,
            "%s{\"source\":\"%s\",\"sessions\":%ld,\"model_calls\":%ld,\"input_tokens\":%ld,"
            "\"cached_input_tokens\":%ld,\"output_tokens\":%ld,\"tool_calls\":%ld,\"code_changes\":%ld}",
            i ? "," : "", sources[i].source, sources[i].sessions, sources[i].model_calls,
            sources[i].input_tokens, sources[i].cached_input_tokens, sources[i].output_tokens,
            sources[i].tool_calls, sources[i].code_changes);
    }
    if (written > 0 && (size_t)written < sizeof(json_buf) - 2)
        snprintf(json_buf + written, sizeof(json_buf) - (size_t)written, "]}");
    send_response(client_fd, "200 OK", "application/json", json_buf);
}

// 处理代码统计接口请求，返回代理产生的各类代码(业务逻辑、测试、文档等)增删行数的JSON数据
static void handle_api_code(int client_fd) {
    AgentCodeStats stats;
    if (!load_code_stats(&stats)) {
        send_response(client_fd, "500 Internal Error", "application/json",
                      "{\"error\":\"failed to load code stats\"}");
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
             stats.change_events, stats.files_changed, stats.lines_added, stats.lines_deleted,
             stats.business_lines_added, stats.test_lines_added,
             stats.documentation_lines_added, stats.generated_lines_added,
             stats.other_lines_added, stats.business_code_share);
    send_response(client_fd, "200 OK", "application/json", json_buf);
}

// 处理Git统计接口请求，返回涉及的仓库数、提交数及其影响的文件和行数等JSON数据
static void handle_api_git(int client_fd) {
    AgentGitStats stats;
    if (!load_git_stats(&stats)) {
        send_response(client_fd, "500 Internal Error", "application/json",
                      "{\"error\":\"failed to load git stats\"}");
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
    send_response(client_fd, "200 OK", "application/json", json_buf);
}

// 处理归因统计接口请求，返回代码行采纳率及各类代码具体被采纳数量的JSON数据
static void handle_api_attribution(int client_fd) {
    AgentAttributionStats stats;
    if (!load_attribution_stats(&stats)) {
        send_response(client_fd, "500 Internal Error", "application/json",
                      "{\"error\":\"failed to load attribution stats\"}");
        return;
    }
    char json_buf[2048];
    snprintf(json_buf, sizeof(json_buf),
             "{\"candidate_lines\":%ld,\"accepted_lines\":%ld,"
             "\"business_candidate_lines\":%ld,\"business_accepted_lines\":%ld,"
             "\"test_candidate_lines\":%ld,\"test_accepted_lines\":%ld,"
             "\"documentation_candidate_lines\":%ld,\"documentation_accepted_lines\":%ld,"
             "\"acceptance_rate\":%.2f,\"business_acceptance_rate\":%.2f,"
             "\"method\":\"exact_sha256_line_match\",\"generated_files_excluded\":true}",
             stats.candidate_lines, stats.accepted_lines,
             stats.business_candidate_lines, stats.business_accepted_lines,
             stats.test_candidate_lines, stats.test_accepted_lines,
             stats.documentation_candidate_lines, stats.documentation_accepted_lines,
             stats.acceptance_rate, stats.business_acceptance_rate);
    send_response(client_fd, "200 OK", "application/json", json_buf);
}

// 处理非API请求，提供默认的前端HTML页面。若找不到对应的静态文件，则返回极简的错误提示页面
static void handle_serve_html(int client_fd) {
    FILE *fp = fopen("web/index.html", "r");
    if (!fp) {
        // 后备极简网页
        const char *fallback_html = "<html lang=\"zh-CN\"><body><h1>AgentStat 服务已启动</h1><p>未找到 web/index.html。</p></body></html>";
        send_response(client_fd, "200 OK", "text/html", fallback_html);
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *html = (char *)malloc(size + 1);
    if (html) {
        fread(html, 1, size, fp);
        html[size] = '\0';
        send_response(client_fd, "200 OK", "text/html", html);
        free(html);
    }
    fclose(fp);
}

// 启动简易Web服务器主循环，在指定的端口上监听TCP连接，并基于请求路由分发给相应的处理函数
int start_web_server(int port) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        return 1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    printf("========================================================================\n");
    printf("   AgentStat 中文数据看板：http://localhost:%d\n", port);
    printf("   按 Ctrl+C 停止服务\n");
    printf("========================================================================\n\n");
    
    char buffer[BUFFER_SIZE];
    
    // 无限循环接受并处理客户端连接
    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            continue;
        }
        
        memset(buffer, 0, sizeof(buffer));
        // 接收客户端发送的HTTP请求报文
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        // 简单的HTTP请求路由，通过匹配请求报文内容分发请求到对应的处理接口
        if (strstr(buffer, "GET /api/summary") != NULL) {
            handle_api_summary(client_fd);
        } else if (strstr(buffer, "GET /api/usage") != NULL) {
            handle_api_usage(client_fd);
        } else if (strstr(buffer, "GET /api/code") != NULL) {
            handle_api_code(client_fd);
        } else if (strstr(buffer, "GET /api/git") != NULL) {
            handle_api_git(client_fd);
        } else if (strstr(buffer, "GET /api/attribution") != NULL) {
            handle_api_attribution(client_fd);
        } else if (strstr(buffer, "GET /api/models") != NULL) {
            handle_api_models(client_fd);
        } else if (strstr(buffer, "GET /api/tools") != NULL) {
            handle_api_tools(client_fd);
        } else if (strstr(buffer, "GET /api/sessions") != NULL) {
            handle_api_records(client_fd);
        } else if (strstr(buffer, "GET /api/records") != NULL) {
            handle_api_records(client_fd);
        } else {
            handle_serve_html(client_fd);
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}
