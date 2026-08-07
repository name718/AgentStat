#include "cli.h"
#include "agentstat.h"
#include "stats.h"
#include "storage.h"
#include "ui.h"
#include "server.h"
#include "importer.h"
#include "claude_importer.h"
#include "antigravity_importer.h"
#include "git_importer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * 解析并获取目标数据目录路径。
 * 优先级逻辑：
 * 1. 若定义了特定环境变量（environment_name）且有值，则直接返回该值。
 * 2. 否则，获取系统的 HOME 环境变量。如果未设置，则默认使用当前目录 "."。
 * 3. 将基础目录与给定的后缀（suffix）进行拼接，存入 buffer，并返回该 buffer。
 */
static const char *source_path(const char *environment_name, const char *suffix,
                               char *buffer, size_t size) {
    const char *override = getenv(environment_name);
    if (override && *override) return override;
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buffer, size, "%s/%s", home, suffix);
    return buffer;
}

/**
 * 打印数据同步的结果摘要。
 * 显示同步来源（name）、导入的会话文件数、使用事件、工具调用、代码变更数以及扫描成功/失败的文件数。
 */
static void print_sync_result(const char *name, const CodexSyncResult *result) {
    printf("%-12s %ld session files, %ld usage events, %ld tool calls, %ld code changes (%ld files, %ld failed).\n",
           name, result->sessions_imported, result->usage_events_imported,
           result->tool_calls_imported, result->code_changes_imported,
           result->files_scanned, result->files_failed);
}

/**
 * 解析命令行参数，并执行对应的 CLI 核心子命令逻辑。
 * 这是该 CLI 程序的主路由入口。
 */
int parse_and_execute_cli(int argc, char *argv[]) {
    if (argc < 2) {
        render_help(argv[0]);
        return 0;
    }
    
    const char *subcommand = argv[1];
    
    // "help" 命令：查看使用说明
    if (strcmp(subcommand, "help") == 0 || strcmp(subcommand, "--help") == 0 || strcmp(subcommand, "-h") == 0) {
        render_help(argv[0]);
        return 0;
    }
    
    // "seed" 命令：向本地数据库或存储中填入模拟的测试数据
    if (strcmp(subcommand, "seed") == 0) {
        seed_mock_data();
        printf("%s✓ Successfully seeded sample records to storage.%s\n", COLOR_GREEN, COLOR_RESET);
        return 0;
    }

    // "import-codex" 命令：从指定的 JSONL 文件中导入 Codex 的历史记录数据
    if (strcmp(subcommand, "import-codex") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s import-codex <rollout.jsonl>\n", argv[0]);
            return 1;
        }
        CodexImportResult result;
        if (!import_codex_jsonl(argv[2], &result)) {
            fprintf(stderr, "Failed to import Codex JSONL: %s\n", argv[2]);
            return 1;
        }
        printf("Imported Codex session: %ld usage events, %ld tool calls, %ld code changes (%ld lines scanned).\n",
               result.usage_events_imported, result.tool_calls_imported,
               result.code_changes_imported, result.lines_read);
        if (result.duplicate_usage_events_skipped > 0)
            printf("Skipped %ld duplicate Token snapshots.\n", result.duplicate_usage_events_skipped);
        return 0;
    }

    // "sync-codex" 命令：扫描并同步指定目录下的 Codex 本地历史数据
    if (strcmp(subcommand, "sync-codex") == 0) {
        char default_path[1024];
        const char *path = argc >= 3 ? argv[2] : NULL;
        if (!path) path = source_path("AGENTSTAT_CODEX_DIR", ".codex/sessions", default_path, sizeof(default_path));
        CodexSyncResult result;
        bool ok = sync_codex_directory(path, &result);
        printf("Scanned %ld files and %ld lines; imported %ld usage events, %ld tool calls and %ld code changes.\n",
               result.files_scanned, result.lines_read, result.usage_events_imported,
               result.tool_calls_imported, result.code_changes_imported);
        printf("Skipped %ld duplicate Token snapshots; %ld files failed.\n",
               result.duplicate_usage_events_skipped, result.files_failed);
        return ok ? 0 : 1;
    }

    // "import-claude" 命令：导入 Claude 产生的 JSONL 格式历史数据
    if (strcmp(subcommand, "import-claude") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s import-claude <session.jsonl>\n", argv[0]); return 1; }
        CodexImportResult result;
        if (!import_claude_jsonl(argv[2], &result)) { fprintf(stderr, "Failed to import Claude JSONL: %s\n", argv[2]); return 1; }
        printf("Imported Claude session: %ld usage events, %ld tool calls, %ld code changes (%ld lines scanned; %ld duplicate usage fragments skipped).\n",
               result.usage_events_imported, result.tool_calls_imported, result.code_changes_imported,
               result.lines_read, result.duplicate_usage_events_skipped);
        return 0;
    }

    // "sync-claude" 命令：扫描并同步 Claude 客户端的默认或指定目录数据
    if (strcmp(subcommand, "sync-claude") == 0) {
        char default_path[1024];
        const char *path = argc >= 3 ? argv[2] : source_path("AGENTSTAT_CLAUDE_DIR", ".claude/projects", default_path, sizeof(default_path));
        CodexSyncResult result; bool ok = sync_claude_directory(path, &result); print_sync_result("Claude", &result); return ok ? 0 : 1;
    }

    // "import-antigravity" (或 "import-agy") 命令：导入 Antigravity 工具的 JSONL 会话记录
    if (strcmp(subcommand, "import-antigravity") == 0 || strcmp(subcommand, "import-agy") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s import-antigravity <transcript.jsonl>\n", argv[0]); return 1; }
        CodexImportResult result;
        if (!import_antigravity_jsonl(argv[2], &result)) { fprintf(stderr, "Failed to import Antigravity JSONL: %s\n", argv[2]); return 1; }
        printf("Imported Antigravity session: %ld tool calls and %ld code changes (%ld lines scanned).\n",
               result.tool_calls_imported, result.code_changes_imported, result.lines_read);
        return 0;
    }

    // "sync-antigravity" (或 "sync-agy") 命令：同步并解析 Antigravity 系统目录下所有的交互与代码修改数据
    if (strcmp(subcommand, "sync-antigravity") == 0 || strcmp(subcommand, "sync-agy") == 0) {
        char default_path[1024];
        const char *path = argc >= 3 ? argv[2] : source_path("AGENTSTAT_ANTIGRAVITY_DIR", ".gemini/antigravity-cli", default_path, sizeof(default_path));
        CodexSyncResult result; bool ok = sync_antigravity_directory(path, &result); print_sync_result("Antigravity", &result); return ok ? 0 : 1;
    }

    // "sync" 命令：综合同步命令，自动依次尝试同步已知的所有 Agent 平台数据（Codex, Claude, Antigravity）
    // 此外，如果检测到当前目录下存在 .git 仓库，也会自动进行 Git 提交历史同步
    if (strcmp(subcommand, "sync") == 0) {
        char codex_path[1024], claude_path[1024], agy_path[1024];
        const char *paths[] = {
            source_path("AGENTSTAT_CODEX_DIR", ".codex/sessions", codex_path, sizeof(codex_path)),
            source_path("AGENTSTAT_CLAUDE_DIR", ".claude/projects", claude_path, sizeof(claude_path)),
            source_path("AGENTSTAT_ANTIGRAVITY_DIR", ".gemini/antigravity-cli", agy_path, sizeof(agy_path))
        };
        const char *names[] = {"Codex", "Claude", "Antigravity"};
        bool (*syncers[])(const char *, CodexSyncResult *) = {sync_codex_directory, sync_claude_directory, sync_antigravity_directory};
        bool ok = true;
        for (int i = 0; i < 3; i++) {
            struct stat info;
            if (stat(paths[i], &info) != 0 || !S_ISDIR(info.st_mode)) { printf("%-12s not installed; skipped %s.\n", names[i], paths[i]); continue; }
            CodexSyncResult result; bool source_ok = syncers[i](paths[i], &result); print_sync_result(names[i], &result); if (!source_ok) ok = false;
        }
        struct stat git_dir;
        if (stat(".git", &git_dir) == 0 && S_ISDIR(git_dir.st_mode)) {
            GitImportResult git; bool git_ok = sync_git_repository(".", &git);
            printf("Git          %ld commits and %ld file changes imported.\n", git.commits_imported, git.files_imported);
            if (!git_ok) ok = false;
        } else printf("Git          current directory is not a repository; use sync-git <path>.\n");
        return ok ? 0 : 1;
    }

    // "usage" 命令：加载并展示汇总后的 Agent 交互和使用量（如各类 Token、工具调用和会话数量等）
    if (strcmp(subcommand, "usage") == 0) {
        AgentUsageStats stats;
        if (!load_usage_stats(&stats)) return 1;
        printf("Sessions:          %ld\n", stats.total_sessions);
        printf("Model calls:       %ld\n", stats.model_calls);
        printf("Input tokens:      %ld\n", stats.input_tokens);
        printf("Cached input:      %ld (%.1f%%)\n", stats.cached_input_tokens, stats.cache_hit_rate);
        printf("Cache write:       %ld\n", stats.cache_write_input_tokens);
        printf("Output tokens:     %ld\n", stats.output_tokens);
        printf("Reasoning tokens:  %ld\n", stats.reasoning_output_tokens);
        printf("Tool calls:        %ld across %ld tools\n", stats.tool_calls, stats.distinct_tools);
        printf("MCP calls:         %ld\n", stats.mcp_calls);
        AgentSourceStats sources[MAX_AGENT_SOURCES];
        int source_count = load_source_stats(sources, MAX_AGENT_SOURCES);
        if (source_count > 0) {
            printf("\nBy Agent:\n");
            for (int i = 0; i < source_count; i++)
                printf("  %-12s %ld sessions, %ld model calls, %ld tools, %ld code changes\n",
                       sources[i].source, sources[i].sessions, sources[i].model_calls,
                       sources[i].tool_calls, sources[i].code_changes);
        }
        return 0;
    }

    // "code" 命令：加载并展示由 AI 引发的代码变更统计数据（变动的文件、增删行数以及代码用途的分类）
    if (strcmp(subcommand, "code") == 0) {
        AgentCodeStats stats;
        if (!load_code_stats(&stats)) return 1;
        printf("Change events:       %ld\n", stats.change_events);
        printf("Files changed:       %ld\n", stats.files_changed);
        printf("Lines added/deleted: %ld / %ld\n", stats.lines_added, stats.lines_deleted);
        printf("Business additions: %ld (%.1f%% of classified code)\n",
               stats.business_lines_added, stats.business_code_share);
        printf("Test additions:     %ld\n", stats.test_lines_added);
        printf("Documentation:      %ld\n", stats.documentation_lines_added);
        printf("Generated:          %ld\n", stats.generated_lines_added);
        printf("Other:              %ld\n", stats.other_lines_added);
        return 0;
    }

    // "sync-git" 命令：扫描并分析指定路径 Git 仓库中的历史提交记录和代码变动
    if (strcmp(subcommand, "sync-git") == 0) {
        const char *path = argc >= 3 ? argv[2] : ".";
        GitImportResult result;
        if (!sync_git_repository(path, &result)) {
            fprintf(stderr, "Failed to sync Git repository: %s\n", path);
            return 1;
        }
        printf("Scanned %ld Git commits; imported %ld commits and %ld file changes.\n",
               result.commits_scanned, result.commits_imported, result.files_imported);
        return 0;
    }

    // "git-stats" 命令：从本地存储加载并显示已同步 Git 数据的情况，细化为各类代码的修改统计
    if (strcmp(subcommand, "git-stats") == 0) {
        AgentGitStats stats;
        if (!load_git_stats(&stats)) return 1;
        printf("Repositories:       %ld\n", stats.repositories);
        printf("Commits:            %ld\n", stats.commits);
        printf("Files changed:      %ld\n", stats.files_changed);
        printf("Lines added/deleted:%ld / %ld\n", stats.lines_added, stats.lines_deleted);
        printf("Business additions:%ld\n", stats.business_lines_added);
        printf("Test additions:    %ld\n", stats.test_lines_added);
        printf("Documentation:     %ld\n", stats.documentation_lines_added);
        printf("Generated:         %ld\n", stats.generated_lines_added);
        printf("Other:             %ld\n", stats.other_lines_added);
        return 0;
    }

    // "attribution" 命令：评估由 AI 工具推荐的代码段有多少被实际留存在代码库中（即代码采纳率）
    if (strcmp(subcommand, "attribution") == 0) {
        AgentAttributionStats stats;
        if (!load_attribution_stats(&stats)) return 1;
        printf("Exact candidate lines: %ld\n", stats.candidate_lines);
        printf("Exact accepted lines:  %ld (%.1f%%)\n", stats.accepted_lines, stats.acceptance_rate);
        printf("Business:             %ld / %ld (%.1f%%)\n",
               stats.business_accepted_lines, stats.business_candidate_lines,
               stats.business_acceptance_rate);
        printf("Tests:                %ld / %ld\n",
               stats.test_accepted_lines, stats.test_candidate_lines);
        printf("Documentation:        %ld / %ld\n",
               stats.documentation_accepted_lines, stats.documentation_candidate_lines);
        return 0;
    }
    
    // "web" 命令：启动一个提供 HTTP 服务和可视化数据展示页面的本地 Web 服务器
    if (strcmp(subcommand, "web") == 0) {
        int port = 8080;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
            }
        }
        return start_web_server(port);
    }
    
    // "summary" 命令：综合各个模块（使用、代码、Git、采纳率）的数据，做简洁快速的核心报表输出
    if (strcmp(subcommand, "summary") == 0) {
        AgentUsageStats usage; AgentCodeStats code; AgentGitStats git; AgentAttributionStats attribution;
        if(!load_usage_stats(&usage)||!load_code_stats(&code)||!load_git_stats(&git)||!load_attribution_stats(&attribution))return 1;
        printf("会话:       %ld\n",usage.total_sessions);
        printf("总 Token:   %ld（输入 %ld / 输出 %ld）\n",usage.input_tokens+usage.output_tokens,usage.input_tokens,usage.output_tokens);
        printf("工具调用:   %ld（MCP %ld）\n",usage.tool_calls,usage.mcp_calls);
        printf("代码变更:   %ld（新增 %ld / 删除 %ld）\n",code.change_events,code.lines_added,code.lines_deleted);
        printf("精确采纳率: %.1f%%（%ld / %ld 行）\n",attribution.acceptance_rate,attribution.accepted_lines,attribution.candidate_lines);
        printf("Git:        %ld 个仓库 / %ld 次提交\n",git.repositories,git.commits);
        return 0;
    }
    
    // "list" 命令：列表显示最近发生的若干次会话摘要（包括时间、来源、调用的模型以及输入输出量等）
    if (strcmp(subcommand, "list") == 0) {
        int limit = 10;
        if (argc >= 3 && strcmp(argv[2], "--limit") == 0 && argc >= 4) {
            limit = atoi(argv[3]);
        }
        
        if(limit<1)limit=1;if(limit>100)limit=100;
        AgentSessionStats sessions[100];int count=load_recent_session_stats(sessions,limit);if(count<0)return 1;
        printf("%-20s %-12s %-28s %10s %10s %7s %7s\n","时间","Agent","模型","输入","输出","工具","变更");
        for(int i=0;i<count;i++)printf("%-20.20s %-12.12s %-28.28s %10ld %10ld %7ld %7ld\n",sessions[i].started_at,sessions[i].source,sessions[i].models[0]?sessions[i].models:"未暴露",sessions[i].input_tokens,sessions[i].output_tokens,sessions[i].tool_calls,sessions[i].code_changes);
        return 0;
    }
    
    // "chart" 命令：按控制台列表形式展示各个平台下不同模型的使用对比（调用频率、处理的 Token 总数）
    if (strcmp(subcommand, "chart") == 0) {
        AgentModelStats models[64];int count=load_model_stats(models,64);if(count<0)return 1;
        printf("%-12s %-32s %10s %12s %12s\n","Agent","模型","调用/选择","输入 Token","输出 Token");
        for(int i=0;i<count;i++)printf("%-12.12s %-32.32s %5ld/%-4ld %12ld %12ld\n",models[i].source,models[i].model,models[i].model_calls,models[i].selections,models[i].input_tokens,models[i].output_tokens);
        return 0;
    }
    
    // "record" 命令：手动通过命令行参数创建并录入一条新的 Agent 执行记录，并实时计算预估 Token 花销
    if (strcmp(subcommand, "record") == 0) {
        AgentRecord rec;
        memset(&rec, 0, sizeof(rec));
        
        // 默认值
        generate_session_id(rec.session_id, sizeof(rec.session_id));
        get_current_timestamp(rec.timestamp, sizeof(rec.timestamp));
        strncpy(rec.project, "default-proj", sizeof(rec.project) - 1);
        strncpy(rec.language, "C", sizeof(rec.language) - 1);
        strncpy(rec.model, "manual-unspecified", sizeof(rec.model) - 1);
        rec.snippets_suggested = 1;
        rec.snippets_accepted = 1;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
                strncpy(rec.project, argv[++i], sizeof(rec.project) - 1);
            } else if (strcmp(argv[i], "--language") == 0 && i + 1 < argc) {
                strncpy(rec.language, argv[++i], sizeof(rec.language) - 1);
            } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
                strncpy(rec.model, argv[++i], sizeof(rec.model) - 1);
            } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
                rec.input_tokens = atol(argv[++i]);
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                rec.output_tokens = atol(argv[++i]);
            } else if (strcmp(argv[i], "--suggested") == 0 && i + 1 < argc) {
                rec.lines_suggested = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--accepted") == 0 && i + 1 < argc) {
                rec.lines_accepted = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
                rec.duration_seconds = atof(argv[++i]);
            }
        }
        
        rec.estimated_cost_usd = calculate_estimated_cost(rec.model, rec.input_tokens, rec.output_tokens);
        
        if (save_record(&rec)) {
            double rate = (rec.lines_suggested > 0) ? ((double)rec.lines_accepted / rec.lines_suggested) * 100.0 : 0.0;
            printf("%s✓ Record saved successfully!%s\n", COLOR_GREEN, COLOR_RESET);
            printf("  Session ID:  %s%s%s\n", COLOR_BOLD, rec.session_id, COLOR_RESET);
            printf("  Project:     %s\n", rec.project);
            printf("  Model:       %s\n", rec.model);
            printf("  Tokens:      %ld Input / %ld Output\n", rec.input_tokens, rec.output_tokens);
            printf("  Est. Cost:   $%7.4f USD\n", rec.estimated_cost_usd);
            printf("  Acceptance:  %.1f%% (%d/%d lines)\n\n", rate, rec.lines_accepted, rec.lines_suggested);
        } else {
            fprintf(stderr, "%s✖ Error: Failed to save record to storage.%s\n", COLOR_RED, COLOR_RESET);
            return 1;
        }
        return 0;
    }
    
    // 如果输入的子命令未能匹配上以上任何一条命令，则输出错误信息并展示帮助指南
    fprintf(stderr, "%sUnknown command: '%s'%s\n", COLOR_RED, subcommand, COLOR_RESET);
    render_help(argv[0]);
    return 1;
}
