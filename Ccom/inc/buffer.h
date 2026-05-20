
#ifndef H_BUFFER_H
#define H_BUFFER_H
/**
 *  @brief 此类维护了一个可自动变长的缓冲区类型
 */

#define DEFAULT_BUFFER_LEN 64

#include <stdint.h>
#include "error.h"

/**
 * @brief 缓冲区的维护变量
 * @param list 缓冲区的指针
 * @param len 缓冲区已使用的长度
 * @param max 缓冲区的大小
 * @param point 缓冲区游标的位置(游标指向的元素未访问)
 */
typedef struct buffer BUFFER;
struct buffer
{
    uint8_t* list;
    uint64_t len;
    uint64_t max; 
    uint64_t point;
};

/**
 * @brief 初始化缓冲区
 * @param len 初始分配的字节大小（若为 0 则使用默认值）
 * @return 成功返回缓冲区指针，失败返回 NULL
 */
BUFFER* initBuffer(uint64_t len);

/**
 * @brief 彻底删除缓冲区，释放所有内存
 * @param b 目标缓冲区
 * @return passTrue 成功，passFalse 失败
 */
ERROR freeBuffer(BUFFER* b);

/**
 * @brief 将字符串追加到缓冲区末尾
 * @param b 目标缓冲区
 * @param str 要插入的字符串
 * @return passTrue 成功，passFalse 失败
 */
ERROR insertBufferViaString(BUFFER* b,const char* str);

/**
 * @brief 将单个字符追加到缓冲区末尾
 * @param b 目标缓冲区
 * @param c 要插入的字符
 * @return passTrue 成功，passFalse 失败
 */
ERROR insertBufferViaByte(BUFFER* b,const char c);

/**
 * @brief 清空缓冲区（重置 len 和 point，不释放内存）
 * @param b 目标缓冲区
 * @return passTrue 成功，passFalse 失败
 */
ERROR cleanBuffer(BUFFER* b);

/**
 * @brief 将游标 point 归位（重置为 0）
 * @param b 目标缓冲区
 * @return passTrue 成功，passFalse 失败
 */
ERROR resetPoint(BUFFER* b);

/**
 * @brief 从游标当前位置读取一个字节，并将游标向后移动一位
 * @param b 目标缓冲区
 * @param byte 输出参数，存放读取到的字节
 * @return passTrue 成功，passFalse 失败（如游标越界）
 */
ERROR readBufferViaByte(BUFFER* b, uint8_t* byte);

/**
 * @brief 将游标移动到指定位置
 * @param b 目标缓冲区
 * @param n 目标位置
 * @return passTrue 成功，passFalse 失败（如 n 超出已使用长度）
 */
ERROR setPoint(BUFFER* b, uint64_t n);

/**
 * @brief 从头查找字符串是否在缓冲区中
 * @param b 目标缓冲区
 * @param pattern 要查找的字符串
 * @return passTrue 找到（游标移动到第一个匹配位置），passFalse 未找到
 */
ERROR searchBufferFromHead(BUFFER* b, const char* pattern);

/**
 * @brief 从游标当前位置开始查找字符串是否在缓冲区中
 * @param b 目标缓冲区
 * @param pattern 要查找的字符串
 * @return passTrue 找到（游标移动到第一个匹配位置），passFalse 未找到
 */
ERROR searchBufferFromPoint(BUFFER* b, const char* pattern);

#endif
