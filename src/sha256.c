#include "sha256.h"
#include <stdint.h>
#include <string.h>

// SHA-256 算法的上下文结构体，用于保存计算过程中的状态
typedef struct {
  uint8_t data[64];    // 当前正在处理的 512 位（64 字节）数据块
  uint32_t state[8];   // 当前的 8 个 32 位哈希值状态 (a, b, c, d, e, f, g, h)
  uint64_t bit_length; // 已处理数据的总长度（以位为单位）
  size_t data_length;  // 当前 data 缓冲区中积累的数据字节数
} Sha256Context;

// SHA-256 算法中使用的 64 个 32 位常量（由前 64 个质数的立方根的小数部分取前 32
// 位得到）
static const uint32_t constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// 循环右移操作 (Right Rotate)，SHA-256 算法中的基本位运算
static uint32_t rotate_right(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32 - count));
}

// 对一个 64 字节（512 位）的数据块进行 SHA-256 核心变换处理
static void transform(Sha256Context *context, const uint8_t block[64]) {
  // 扩展 16 个 32 位的字为 64 个 32 位的字（消息调度）
  uint32_t words[64];
  for (int i = 0; i < 16; i++)
    words[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotate_right(words[i - 15], 7) ^
                  rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
    uint32_t s1 = rotate_right(words[i - 2], 17) ^
                  rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }
  // 初始化 8 个工作变量，使用当前的哈希状态
  uint32_t a = context->state[0], b = context->state[1], c = context->state[2],
           d = context->state[3];
  uint32_t e = context->state[4], f = context->state[5], g = context->state[6],
           h = context->state[7];
  // 执行 64 轮的主循环操作
  for (int i = 0; i < 64; i++) {
    uint32_t s1 =
        rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
    uint32_t choice = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + s1 + choice + constants[i] + words[i];
    uint32_t s0 =
        rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  // 将主循环结果与之前的哈希状态相加（模 2^32）
  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
  context->state[4] += e;
  context->state[5] += f;
  context->state[6] += g;
  context->state[7] += h;
}

// 初始化 SHA-256 上下文，设置初始的 8 个 32 位哈希值（由前 8
// 个质数的平方根的小数部分取前 32 位得到）
static void initialize(Sha256Context *context) {
  memset(context, 0, sizeof(*context));
  const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(context->state, initial, sizeof(initial));
}

// 将传入的数据逐步更新到 SHA-256 上下文中
// 如果累积的数据达到 64 字节（512 位），则触发一次 transform 变换
static void update(Sha256Context *context, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    context->data[context->data_length++] = data[i];
    if (context->data_length == 64) {
      transform(context, context->data);
      context->bit_length += 512;
      context->data_length = 0;
    }
  }
}

// 完成 SHA-256 计算，处理剩余的数据（进行填充）并输出最终的 32 字节哈希值
static void finish(Sha256Context *context, uint8_t digest[32]) {
  size_t i = context->data_length;
  // 附加比特 '1' 到消息末尾（以 0x80 表示，因为按字节处理）
  context->data[i++] = 0x80;
  // 如果剩余空间不足以存放 64 位的长度信息（即已占用超过 56 字节），则先填充 0
  // 并进行一次变换
  if (i > 56) {
    while (i < 64)
      context->data[i++] = 0;
    transform(context, context->data);
    i = 0;
  }
  // 填充 0，直到第 56 个字节
  while (i < 56)
    context->data[i++] = 0;
  // 计算消息的总位长度并追加到最后 8 个字节（大端序）
  context->bit_length += context->data_length * 8;
  for (int j = 7; j >= 0; j--)
    context->data[56 + (7 - j)] = (uint8_t)(context->bit_length >> (j * 8));
  // 执行最后一次变换
  transform(context, context->data);
  // 将 8 个 32 位状态变量转换为 32 字节的输出结果（大端序）
  for (i = 0; i < 8; i++) {
    digest[i * 4] = (uint8_t)(context->state[i] >> 24);
    digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
    digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
    digest[i * 4 + 3] = (uint8_t)context->state[i];
  }
}

// 对外提供的便捷接口：计算数据的 SHA-256 哈希，并将结果转换为 64
// 个字符的十六进制字符串（包含结束符 '\0'）
void sha256_hex(const unsigned char *data, size_t length, char output[65]) {
  static const char hex[] = "0123456789abcdef";
  Sha256Context context;
  uint8_t digest[32];
  initialize(&context);
  update(&context, data, length);
  finish(&context, digest);
  for (int i = 0; i < 32; i++) {
    output[i * 2] = hex[digest[i] >> 4];
    output[i * 2 + 1] = hex[digest[i] & 15];
  }
  output[64] = '\0';
}
