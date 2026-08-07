#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>

// 计算给定数据的SHA-256哈希值，并将结果以64个字符的十六进制字符串（外加\0终止符）存入output数组
void sha256_hex(const unsigned char *data, size_t length, char output[65]);

#endif
