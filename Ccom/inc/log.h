#ifndef _LOG_H
#define _LOG_H

#include <stdint.h>
#include <stdbool.h>

// 日志基础目录：在执行文件同级创建 log/ 文件夹
#define LOG_BASE_DIR "log"

// 全局状态变量：是否启用日志
extern bool g_log_enabled;

/**
 * @brief 初始化 log 组件
 * @param enable 是否启用日志
 * @param timestamp_str 时间戳字符串（用于创建目录名），如 "20260522_125521"
 *        传入后将被保存为全局时间戳，后续所有日志均使用此时间戳
 */
void log_init(bool enable, const char *timestamp_str);

/**
 * @brief 清理 log 组件资源
 */
void log_deinit(void);

/**
 * @brief 将发送缓冲区数据写入 byte 日志文件（十六进制+ASCII hexdump 格式）
 * @param data 缓冲区数据指针
 * @param len  数据长度
 */
void log_write_send(const uint8_t *data, uint32_t len);

/**
 * @brief 将接收缓冲区数据写入 byte 日志文件（十六进制+ASCII hexdump 格式）
 * @param data 缓冲区数据指针
 * @param len  数据长度
 */
void log_write_recv(const uint8_t *data, uint32_t len);

/**
 * @brief 将发送缓冲区中的可打印 ASCII 字符写入 ASCII 日志文件
 * @param data 缓冲区数据指针
 * @param len  数据长度
 */
void log_write_send_ascii(const uint8_t *data, uint32_t len);

/**
 * @brief 将接收缓冲区中的可打印 ASCII 字符写入 ASCII 日志文件
 * @param data 缓冲区数据指针
 * @param len  数据长度
 */
void log_write_recv_ascii(const uint8_t *data, uint32_t len);

#endif // _LOG_H