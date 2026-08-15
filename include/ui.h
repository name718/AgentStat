#ifndef UI_H
#define UI_H

// 终端输出颜色和样式宏定义
#define COLOR_RESET "\033[0m"    // 重置所有颜色和样式
#define COLOR_BOLD "\033[1m"     // 加粗文本
#define COLOR_DIM "\033[2m"      // 变暗文本（降低亮度）
#define COLOR_CYAN "\033[36m"    // 青色文本
#define COLOR_GREEN "\033[32m"   // 绿色文本
#define COLOR_YELLOW "\033[33m"  // 黄色文本
#define COLOR_BLUE "\033[34m"    // 蓝色文本
#define COLOR_MAGENTA "\033[35m" // 洋红色文本
#define COLOR_RED "\033[31m"     // 红色文本
#define COLOR_WHITE "\033[97m"   // 亮白色文本

// 在终端中渲染并显示应用程序的Banner（如Logo或标题）
void render_banner(void);

// 渲染并显示帮助信息，说明如何使用程序及其参数
void render_help(const char *prog_name);

#endif // UI_H
