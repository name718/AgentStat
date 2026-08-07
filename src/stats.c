#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief 存储不同 AI 模型定价信息的结构体
 * 包含模型名称关键词以及每百万 token 的输入和输出成本。
 */
typedef struct {
    const char *model_keyword;     // 模型名称的关键词（用于子串匹配）
    double input_cost_per_m;       // 每百万输入 token 的价格（美元）
    double output_cost_per_m;      // 每百万输出 token 的价格（美元）
} ModelPricing;

/**
 * @brief 已知模型的定价表
 * 包含了各主流大语言模型的每百万 token 输入/输出单价。
 * 默认费率设在最后一行。
 */
static const ModelPricing PRICING_TABLE[] = {
    {"claude-3-5-sonnet", 3.00, 15.00},
    {"claude-3-opus",      15.00, 75.00},
    {"claude-3-haiku",     0.25, 1.25},
    {"gpt-4o",             2.50, 10.00},
    {"gpt-4o-mini",        0.15, 0.60},
    {"gemini-1.5-pro",     1.25, 5.00},
    {"gemini-1.5-flash",   0.075, 0.30},
    {"deepseek-coder",     0.14, 0.28},
    {"default",            2.00, 8.00}
};

/**
 * @brief 根据模型名称、输入和输出的 token 数量计算预估成本
 * 
 * @param model 模型名称的字符串（如果为 NULL，则使用默认费率）
 * @param input_tokens 消耗的输入 token 数量
 * @param output_tokens 消耗的输出 token 数量
 * @return double 计算出的总成本（美元）
 */
double calculate_estimated_cost(const char *model, long input_tokens, long output_tokens) {
    // 如果模型名称未提供，使用默认名称
    if (!model) model = "default";
    
    // 初始化为默认的输入输出费率
    double input_rate = 2.00;
    double output_rate = 8.00;
    
    // 遍历定价表，通过关键词匹配来查找对应模型的具体费率
    size_t table_size = sizeof(PRICING_TABLE) / sizeof(PRICING_TABLE[0]);
    for (size_t i = 0; i < table_size; i++) {
        // 如果传入的 model 字符串包含定价表中的关键词
        if (strstr(model, PRICING_TABLE[i].model_keyword) != NULL) {
            input_rate = PRICING_TABLE[i].input_cost_per_m;
            output_rate = PRICING_TABLE[i].output_cost_per_m;
            break; // 找到匹配项后立即跳出循环
        }
    }
    
    // 按每百万 token 的费率计算总输入和输出成本
    double cost_input = ((double)input_tokens / 1000000.0) * input_rate;
    double cost_output = ((double)output_tokens / 1000000.0) * output_rate;
    
    // 返回总预估成本
    return cost_input + cost_output;
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
