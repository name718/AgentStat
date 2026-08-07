#ifndef STATS_H
#define STATS_H

#include "agentstat.h"

// 根据使用的模型以及输入/输出的token数量，计算预估的花费（美元）
double calculate_estimated_cost(const char *model, long input_tokens, long output_tokens);

// 生成一个唯一的会话ID并将其写入到提供的缓冲区中
void generate_session_id(char *buffer, size_t size);

// 获取当前的时间戳字符串并将其写入到提供的缓冲区中
void get_current_timestamp(char *buffer, size_t size);

#endif // STATS_H
