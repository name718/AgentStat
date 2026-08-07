#include "cli.h"
#include <stdlib.h>
#include <time.h>

/**
 * 主函数，程序的入口点。
 * 
 * @param argc 命令行参数的数量
 * @param argv 命令行参数的字符串数组
 * @return 程序的退出状态码。通常返回 0 表示成功，非 0 表示发生错误。
 */
int main(int argc, char *argv[]) {
    // 使用当前时间作为随机数生成器的种子，以确保每次运行生成的随机数序列不同
    srand((unsigned int)time(NULL));
    
    // 解析命令行参数并执行相应的 CLI 命令
    // 将解析和执行的任务委托给 parse_and_execute_cli 函数处理，并返回其执行结果作为退出状态码
    return parse_and_execute_cli(argc, argv);
}
