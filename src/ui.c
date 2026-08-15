#include "ui.h"
#include <stdio.h>

/**
 * @brief 渲染 CLI 的欢迎横幅 (Banner)
 *
 * 该函数在终端输出带有特定颜色的 ASCII 艺术字。
 * 使用 ANSI 转义序列 (如 COLOR_CYAN, COLOR_BOLD 等) 来控制颜色和字体样式。
 */
void render_banner(void) {
  // 启用青色和粗体字体样式
  printf("%s%s", COLOR_CYAN, COLOR_BOLD);
  // 打印 ASCII 艺术字
  printf("   ___                    __  _____ __        __ \n");
  printf("  / _ |___ ____ ___  ____/ /_/ __/ /____ ____/ /_\n");
  printf(" / __ / _ `/ -_) _ \\/ __/ __/\\ \\/ __/ _ `/ __/ __/\n");
  printf("/_/ |_\\_, /\\__/_//_/\\__/\\__/___/\\__/\\_,_/\\__/\\__/ \n");
  // 恢复默认字体样式并附加副标题
  printf("     /___/   %sAI Agent 使用与代码采纳分析%s\n\n", COLOR_DIM,
         COLOR_RESET);
}

/**
 * @brief 渲染 CLI 的帮助信息
 *
 * @param prog_name 当前程序的名称 (通常为 argv[0])
 *
 * 该函数首先调用 render_banner() 输出横幅，
 * 然后分类列出所有支持的命令及其功能说明，
 * 接着列出特定命令（如 Web 看板、手工记录等）所需的参数，
 * 最后给出常用的命令示例，帮助用户快速上手使用。
 */
void render_help(const char *prog_name) {
  // 首先打印欢迎横幅
  render_banner();

  // 打印基础用法概述
  printf("%s用法:%s\n", COLOR_BOLD, COLOR_RESET);
  printf("  %s <command> [options]\n\n", prog_name);

  // 分类打印所有支持的子命令及其功能描述
  printf("%s命令:%s\n", COLOR_BOLD, COLOR_RESET);
  printf("  %ssummary%s    查看真实数据汇总\n", COLOR_GREEN, COLOR_RESET);
  printf("  %sweb%s        启动中文 Web 看板（默认端口 8080）\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %srecord%s     手工写入一条兼容记录\n", COLOR_GREEN, COLOR_RESET);
  printf("  %slist%s       查看最近真实 Agent 会话\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %schart%s      查看真实模型用量明细\n", COLOR_GREEN, COLOR_RESET);
  printf("  %sseed%s       显式写入测试记录（不进入真实看板）\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %simport-codex%s 导入单个 Codex JSONL\n", COLOR_GREEN, COLOR_RESET);
  printf("  %ssync-codex%s  递归同步 Codex 会话目录\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %simport-claude%s 导入单个 Claude Code JSONL\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %ssync-claude%s 递归同步 Claude Code 会话\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %simport-antigravity%s 导入单个 AGY/Antigravity transcript\n",
         COLOR_GREEN, COLOR_RESET);
  printf("  %ssync-antigravity%s 递归同步 AGY/Antigravity 会话\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %ssync%s       自动发现三种 Agent，并同步当前 Git 仓库\n",
         COLOR_GREEN, COLOR_RESET);
  printf("  %susage%s      查看 Token、缓存、工具和 MCP 指标\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %scode%s       查看 Agent 成功应用的代码变更\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %ssync-git%s   同步指定 Git 仓库的提交数据\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %sgit-stats%s  查看 Git 提交指标\n", COLOR_GREEN, COLOR_RESET);
  printf("  %sattribution%s 查看 Agent 到 Git 的精确行归因\n", COLOR_GREEN,
         COLOR_RESET);
  printf("  %spricing%s    配置或查看模型精确价格\n", COLOR_GREEN, COLOR_RESET);
  printf("  %shelp%s       显示帮助\n\n", COLOR_GREEN, COLOR_RESET);

  // 打印 Web 服务的相关参数说明
  printf("%sWeb 参数:%s\n", COLOR_BOLD, COLOR_RESET);
  printf("  --port <port>         指定看板端口，例如 8080\n\n");

  // 打印手动录入记录的相关参数说明
  printf("%s手工记录参数:%s\n", COLOR_BOLD, COLOR_RESET);
  printf("  --project <name>      项目名称\n");
  printf("  --language <lang>     编程语言\n");
  printf("  --model <model>       模型名称\n");
  printf("  --input <n>           手工输入 Token\n");
  printf("  --output <n>          手工输出 Token\n");
  printf("  --suggested <n>       手工建议行数\n");
  printf("  --accepted <n>        手工采纳行数\n");
  printf("  --duration <sec>      会话时长（秒）\n\n");

  printf("%s价格参数（USD / 百万 Token）:%s\n", COLOR_BOLD, COLOR_RESET);
  printf("  pricing list\n");
  printf("  pricing set --source <agent> --model <model> --input <rate> "
         "--cache-read <rate> --cache-write <rate> --output <rate>\n\n");

  // 打印典型使用示例
  printf("%s示例:%s\n", COLOR_BOLD, COLOR_RESET);
  printf("  $ %s web --port 8080\n", prog_name);
  printf("  $ %s summary\n", prog_name);
  printf("  $ %s sync-codex ~/.codex/sessions\n", prog_name);
  printf("  $ %s sync\n", prog_name);
  printf(
      "  $ %s record --project my-app --language C --model claude-3-5-sonnet "
      "--input 12000 --output 3000 --suggested 100 --accepted 92\n\n",
      prog_name);
}
