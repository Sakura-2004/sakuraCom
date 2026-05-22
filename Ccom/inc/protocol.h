#ifndef H_PROTOCOL_H
#define H_PROTOCOL_H

/**
 * @brief 协议层：封装命令帧的构建、发送、接收解析
 *
 * 命令定义：
 *   0x01 - 消息（收发信息），数据部分不包含命令字节
 *   0x02 - 串口新增通知（C→UI），后跟串口名字符串
 *   0x03 - 串口断开通知（C→UI），后跟串口名字符串
 *   0x04 - 串口连接/断开请求（UI→C），数据部分：
 *          [波特率序号][数据位序号][校验位序号][停止位序号][0x00][0x00] 连接
 *          或 [0xff][0x00][0x00] 断开
 *   0x05 - 串口连接/断开响应（C→UI），数据部分：
 *          [状态码][0x00][0x00]
 *          状态码：0x00=连接成功, 0x01=连接失败, 0xf0=断开成功, 其他0xf*=断开失败
 *
 * 帧格式：[1字节命令码][变长数据载荷][0x00][0x00]
 *         [0x00][0x00] 作为命令结束标记，接收端据此拆包
 */

#include <stdint.h>
#include <stddef.h>
#include "buffer.h"

// ═══════════════════════════════════════════════════════════════
// 命令码定义
// ═══════════════════════════════════════════════════════════════

#define CMD_MSG             0x01   // 消息收发
#define CMD_PORT_ADD        0x02   // 串口新增
#define CMD_PORT_REMOVE     0x03   // 串口断开
#define CMD_CONNECT         0x04   // 连接/断开请求
#define CMD_CONNECT_RESP    0x05   // 连接/断开响应

// 0x05 响应状态码
#define CONNECT_STATUS_SUCCESS          0x00   // 连接成功
#define CONNECT_STATUS_FAIL             0x01   // 连接失败（通用）
#define CONNECT_STATUS_DISCONNECT_OK    0xf0   // 断开成功
#define CONNECT_STATUS_DISCONNECT_FAIL  0xf1   // 断开失败（通用）

// 0x04 请求标记
#define CONNECT_REQUEST_DISCONNECT      0xff   // 断开连接请求标记

// ═══════════════════════════════════════════════════════════════
// 生命周期
// ═══════════════════════════════════════════════════════════════

void protocol_init(void);
void protocol_cleanup(void);

// ═══════════════════════════════════════════════════════════════
// 发送接口（构建帧 → pipe_write → extra 模式 stdout 打印）
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 发送消息（0x01）
 * @param data 消息数据
 * @param len  数据长度
 */
void protocol_send_msg(const void *data, size_t len);

/**
 * @brief 通知 UI 有串口新增（0x02）
 * @param port_name 串口名称
 */
void protocol_send_port_add(const char *port_name);

/**
 * @brief 通知 UI 有串口断开（0x03）
 * @param port_name 串口名称
 */
void protocol_send_port_remove(const char *port_name);

/**
 * @brief 发送连接/断开响应（0x05）
 * @param status 状态码：0x00=连接成功, 0x01=连接失败, 0xf0=断开成功, 0xf1=断开失败
 */
void protocol_send_connect_resp(uint8_t status);

/**
 * @brief 设置 0x04 连接/断开命令的回调函数
 *        当收到 CMD_CONNECT(0x04) 时，调用此回调处理 payload
 * @param cb 回调函数，参数为 payload 数据和长度
 */
void protocol_set_connect_callback(void (*cb)(const uint8_t *payload, size_t len));

/**
 * @brief 设置 0x01 消息接收回调函数
 *        当收到 CMD_MSG(0x01) 时，调用此回调处理 payload（已去掉命令字节）
 *        用于将 RustUI 发来的数据转发到串口
 * @param cb 回调函数，参数为 payload 数据和长度
 */
void protocol_set_msg_callback(void (*cb)(const uint8_t *payload, size_t len));

// ═══════════════════════════════════════════════════════════════
// 接收接口（pipe 接收线程回调中调用）
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 处理从 UI 接收到的原始帧数据
 *        解析命令码，extra 模式下打印 [0x01][data] 到 stdout，
 *        并将 0x01 消息数据存入接收缓冲区
 * @param data 原始帧（含命令码）
 * @param len  帧长度
 */
void protocol_on_receive(const uint8_t *data, size_t len);

// ═══════════════════════════════════════════════════════════════
// 缓冲区访问
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 获取发送缓冲区（存储所有已发送的 0x01 数据，不含命令码）
 */
BUFFER* protocol_get_send_buf(void);

/**
 * @brief 获取接收缓冲区（存储所有已接收的 0x01 数据，不含命令码）
 */
BUFFER* protocol_get_recv_buf(void);

#endif