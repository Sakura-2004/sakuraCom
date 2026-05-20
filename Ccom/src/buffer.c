#include "buffer.h"
#include <stdlib.h>
#include <string.h>

BUFFER* initBuffer(uint64_t len)
{
    BUFFER* b = (BUFFER*)malloc(sizeof(BUFFER));
    if (b == NULL) {
        return NULL;
    }

    if (len == 0) {
        len = DEFAULT_BUFFER_LEN;
    }

    b->list = (uint8_t*)malloc(len);
    if (b->list == NULL) {
        free(b);
        return NULL;
    }

    b->len = 0;
    b->max = len;
    b->point = 0;
    return b;
}

ERROR freeBuffer(BUFFER* b)
{
    if (b == NULL) {
        return passFalse;
    }
    if (b->list != NULL) {
        free(b->list);
        b->list = NULL;
    }
    free(b);
    return passTrue;
}

ERROR insertBufferViaString(BUFFER* b, const char* str)
{
    if (b == NULL || str == NULL) {
        return passFalse;
    }

    size_t str_len = strlen(str);
    if (str_len == 0) {
        return passTrue;
    }

    while (b->len + str_len > b->max) {
        b->max *= 2;
        uint8_t* new_list = (uint8_t*)realloc(b->list, b->max);
        if (new_list == NULL) {
            return passFalse;
        }
        b->list = new_list;
    }

    memcpy(b->list + b->len, str, str_len);
    b->len += str_len;
    return passTrue;
}

ERROR insertBufferViaByte(BUFFER* b, const char c)
{
    if (b == NULL) {
        return passFalse;
    }

    if (b->len + 1 > b->max) {
        b->max *= 2;
        uint8_t* new_list = (uint8_t*)realloc(b->list, b->max);
        if (new_list == NULL) {
            return passFalse;
        }
        b->list = new_list;
    }

    b->list[b->len] = (uint8_t)c;
    b->len++;
    return passTrue;
}

ERROR cleanBuffer(BUFFER* b)
{
    if (b == NULL) {
        return passFalse;
    }

    b->len = 0;
    b->point = 0;
    return passTrue;
}

ERROR resetPoint(BUFFER* b)
{
    if (b == NULL) {
        return passFalse;
    }

    b->point = 0;
    return passTrue;
}

ERROR readBufferViaByte(BUFFER* b, uint8_t* byte)
{
    if (b == NULL || byte == NULL) {
        return passFalse;
    }

    if (b->point >= b->len) {
        return passFalse;
    }

    *byte = b->list[b->point];
    b->point++;
    return passTrue;
}

ERROR setPoint(BUFFER* b, uint64_t n){
    if(b==NULL || n>b->len){
        return passFalse;
    }

    b->point=n;
    return passTrue;
}

/**
 * @brief KMP 算法：构建部分匹配表（前缀函数）
 * @param pattern 模式串
 * @param pattern_len 模式串长度
 * @param next 输出参数，部分匹配表数组
 */
static void buildKMPTable(const char* pattern, uint64_t pattern_len, uint64_t* next)
{
    next[0] = 0;
    uint64_t j = 0;

    for (uint64_t i = 1; i < pattern_len; i++) {
        while (j > 0 && pattern[i] != pattern[j]) {
            j = next[j - 1];
        }
        if (pattern[i] == pattern[j]) {
            j++;
        }
        next[i] = j;
    }
}

/**
 * @brief KMP 算法：在缓冲区中从指定起始位置开始查找模式串
 * @param b 缓冲区
 * @param pattern 要查找的模式串
 * @param pattern_len 模式串长度
 * @param start 查找起始位置
 * @param found_pos 输出参数，找到的位置
 * @return passTrue 找到，passFalse 未找到
 */
static ERROR kmpSearch(BUFFER* b, const char* pattern, uint64_t pattern_len, uint64_t start, uint64_t* found_pos)
{
    if (pattern_len == 0) {
        *found_pos = start;
        return passTrue;
    }

    uint64_t next[pattern_len];
    buildKMPTable(pattern, pattern_len, next);

    uint64_t j = 0;
    for (uint64_t i = start; i < b->len; i++) {
        while (j > 0 && b->list[i] != (uint8_t)pattern[j]) {
            j = next[j - 1];
        }
        if (b->list[i] == (uint8_t)pattern[j]) {
            j++;
        }
        if (j == pattern_len) {
            *found_pos = i - pattern_len + 1;
            return passTrue;
        }
    }

    return passFalse;
}

ERROR searchBufferFromHead(BUFFER* b, const char* pattern)
{
    if (b == NULL || pattern == NULL) {
        return passFalse;
    }

    uint64_t pattern_len = strlen(pattern);
    uint64_t found_pos = 0;

    ERROR result = kmpSearch(b, pattern, pattern_len, 0, &found_pos);
    if (result == passTrue) {
        b->point = found_pos;
        return passTrue;
    }

    return passFalse;
}

ERROR searchBufferFromPoint(BUFFER* b, const char* pattern)
{
    if (b == NULL || pattern == NULL) {
        return passFalse;
    }

    uint64_t pattern_len = strlen(pattern);
    uint64_t found_pos = 0;

    ERROR result = kmpSearch(b, pattern, pattern_len, b->point, &found_pos);
    if (result == passTrue) {
        b->point = found_pos;
        return passTrue;
    }

    return passFalse;
}
