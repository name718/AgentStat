#ifndef STATS_H
#define STATS_H

#include "agentstat.h"

// 旧版兼容入口；不再使用模糊模型匹配或默认价格
double calculate_estimated_cost(const char *model, long input_tokens,
                                long output_tokens);

// 生成一个唯一的会话ID并将其写入到提供的缓冲区中
void generate_session_id(char *buffer, size_t size);

// 获取当前的时间戳字符串并将其写入到提供的缓冲区中
void get_current_timestamp(char *buffer, size_t size);

#endif // STATS_H
