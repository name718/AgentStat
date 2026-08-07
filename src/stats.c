#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief 旧版手工记录不再猜测模型价格
 * 
 * @param model 模型名称的字符串（如果为 NULL，则使用默认费率）
 * @param input_tokens 消耗的输入 token 数量
 * @param output_tokens 消耗的输出 token 数量
 * @return double 计算出的总成本（美元）
 */
double calculate_estimated_cost(const char *model, long input_tokens, long output_tokens) {
    (void)model;
    (void)input_tokens;
    (void)output_tokens;
    return 0.0;
}

/**
 * @brief 生成基于当前时间和随机数的会话 ID
 * 
 * @param buffer 用于存储生成的会话 ID 的字符串缓冲区
 * @param size buffer 的大小
 */
void generate_session_id(char *buffer, size_t size) {
    // 获取当前时间戳
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    
    // 生成一个 0 到 999 之间的随机数
    int rand_val = rand() % 1000;
    
    // 格式化会话 ID，例如：sess_20231025_1430_123
    snprintf(buffer, size, "sess_%04d%02d%02d_%02d%02d_%03d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, rand_val);
}

/**
 * @brief 获取格式化后的当前时间戳字符串
 * 
 * @param buffer 用于存储时间戳的字符串缓冲区
 * @param size buffer 的大小
 */
void get_current_timestamp(char *buffer, size_t size) {
    // 获取当前时间戳
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    
    // 格式化为 "YYYY-MM-DD HH:MM:SS" 形式
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}
