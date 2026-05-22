#include "protocol.h"
#include "pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════
// 内部状态
// ═══════════════════════════════════════════════════════════════

static BUFFER *g_send_buf = NULL;  // 发送缓冲区（0x01 数据载荷，不含命令码）
static BUFFER *g_recv_buf = NULL;  // 接收缓冲区（0x01 数据载荷，不含命令码）

// 接收帧缓冲：用于累积原始字节并检测 [0x57][0x8a] 帧结束标记
#define RECV_FRAME_MAX 65536
static uint8_t g_recv_frame_buf[RECV_FRAME_MAX];
static size_t  g_recv_frame_len = 0;

// 帧结束标记常量（魔术数字 0x57 0x8a，避免与 payload 数据冲突）
#define FRAME_TERM_LEN  2
#define FRAME_TERM_1    0x57
#define FRAME_TERM_2    0x8a

// 0x04 连接/断开命令回调（由 main.c 设置）
static void (*g_connect_callback)(const uint8_t *payload, size_t len) = NULL;
static void (*g_msg_callback)(const uint8_t *payload, size_t len) = NULL;

// ═══════════════════════════════════════════════════════════════
// 前置声明
// ═══════════════════════════════════════════════════════════════

static void protocol_on_receive_one(const uint8_t *data, size_t len);

// ═══════════════════════════════════════════════════════════════
// 内部辅助：构建并发送完整帧（cmd + data + [0x00][0x00]）
// ═══════════════════════════════════════════════════════════════

static void protocol_send_frame(uint8_t cmd, const void *data, size_t len) {
    // 帧格式：[cmd][data...][0x57][0x8a]
    size_t frame_len = 1 + len + FRAME_TERM_LEN;
    uint8_t *frame = (uint8_t *)malloc(frame_len);
    if (!frame) return;
    frame[0] = cmd;
    if (len > 0) {
        memcpy(frame + 1, data, len);
    }
    frame[1 + len]     = FRAME_TERM_1;
    frame[1 + len + 1] = FRAME_TERM_2;

    pipe_write(frame, frame_len);
    free(frame);
}

// ═══════════════════════════════════════════════════════════════
// 内部辅助：扫描接收缓冲区，按 [0x57][0x8a] 拆出完整帧
// ═══════════════════════════════════════════════════════════════

static void protocol_parse_frames(void) {
    while (1) {
        // 查找 [0x57][0x8a] 结束标记
        // 魔术数字 0x57 0x8a 不会与 payload 数据冲突，不需要额外 lookahead 检查
        ssize_t term_pos = -1;
        for (size_t i = 0; i + 1 < g_recv_frame_len; i++) {
            if (g_recv_frame_buf[i]   == FRAME_TERM_1 &&
                g_recv_frame_buf[i+1] == FRAME_TERM_2) {
                term_pos = (ssize_t)i;
                break;
            }
        }
        if (term_pos < 0) {
            break;  // 尚未收到完整帧
        }

        // 取出完整帧（不含 [0x57][0x8a] 结束标记）
        size_t frame_len = (size_t)term_pos;
        if (frame_len > 0) {
            protocol_on_receive_one(g_recv_frame_buf, frame_len);
        }

        // 从缓冲头部移除已处理的帧 + 结束标记
        size_t consumed = frame_len + FRAME_TERM_LEN;
        if (consumed < g_recv_frame_len) {
            memmove(g_recv_frame_buf,
                    g_recv_frame_buf + consumed,
                    g_recv_frame_len - consumed);
        }
        g_recv_frame_len -= consumed;
    }

    // 缓冲区溢出保护：过大时清空以避免内存问题
    if (g_recv_frame_len >= RECV_FRAME_MAX - 1) {
        fprintf(stderr, "protocol: recv frame buffer overflow, clearing\n");
        g_recv_frame_len = 0;
    }
}

// ═══════════════════════════════════════════════════════════════
// 生命周期
// ═══════════════════════════════════════════════════════════════

void protocol_init(void) {
    g_send_buf = initBuffer(0);
    g_recv_buf = initBuffer(0);
    g_recv_frame_len = 0;
    g_connect_callback = NULL;
    g_msg_callback = NULL;
}

void protocol_cleanup(void) {
    if (g_send_buf) {
        freeBuffer(g_send_buf);
        g_send_buf = NULL;
    }
    if (g_recv_buf) {
        freeBuffer(g_recv_buf);
        g_recv_buf = NULL;
    }
    g_recv_frame_len = 0;
    g_connect_callback = NULL;
    g_msg_callback = NULL;
}

// ═══════════════════════════════════════════════════════════════
// 设置 0x04 命令回调
// ═══════════════════════════════════════════════════════════════

void protocol_set_connect_callback(void (*cb)(const uint8_t *payload, size_t len)) {
    g_connect_callback = cb;
}

void protocol_set_msg_callback(void (*cb)(const uint8_t *payload, size_t len)) {
    g_msg_callback = cb;
}

// ═══════════════════════════════════════════════════════════════
// 内部辅助：extra 模式下打印命令和数据
// 命令非0x01时打印为0xNN，0x01打印为"SEND"/"RECV"
// ═══════════════════════════════════════════════════════════════

static void extra_print_send(uint8_t cmd, const void *data, size_t len) {
    if (pipe_get_mode() != 1) return;

    if (cmd == CMD_MSG) {
        fwrite("[SEND][", 1, 7, stdout);
    } else {
        printf("[0x%02X][", cmd);
    }
    fwrite(data, 1, len, stdout);
    fwrite("]\n", 1, 2, stdout);
    fflush(stdout);
}

static void extra_print_recv(uint8_t cmd, const void *data, size_t len) {
    if (pipe_get_mode() != 1) return;

    if (cmd == CMD_MSG) {
        fwrite("[RECV][", 1, 7, stdout);
    } else {
        printf("[0x%02X][", cmd);
    }
    fwrite(data, 1, len, stdout);
    fwrite("]\n", 1, 2, stdout);
    fflush(stdout);
}

// ═══════════════════════════════════════════════════════════════
// 发送接口（所有帧自动追加 [0x57][0x8a] 结束标记）
// ═══════════════════════════════════════════════════════════════

void protocol_send_msg(const void *data, size_t len) {
    // 通过 protocol_send_frame 发送，自动追加 [0x57][0x8a]
    protocol_send_frame(CMD_MSG, data, len);

    // 存入发送缓冲区（仅数据部分，不含命令码和结束标记）
    if (g_send_buf) {
        insertBufferViaString(g_send_buf, (const char *)data);
        // 缓冲区容量保护：超过 102400 字节时清空
        if (g_send_buf->len > 102400) {
            cleanBuffer(g_send_buf);
        }
    }

    // extra 模式打印
    extra_print_send(CMD_MSG, data, len);
}

void protocol_send_port_add(const char *port_name) {
    size_t name_len = strlen(port_name);
    // 帧格式：[0x02][port_name][0x57][0x8a]
    // payload 不含 '\0'，由帧结束标记 [0x57][0x8a] 定界
    protocol_send_frame(CMD_PORT_ADD, port_name, name_len);

    // extra 模式打印（不算消息，不存入发送缓冲）
    extra_print_send(CMD_PORT_ADD, port_name, name_len);
}

void protocol_send_port_remove(const char *port_name) {
    size_t name_len = strlen(port_name);
    // 帧格式：[0x03][port_name][0x57][0x8a]
    protocol_send_frame(CMD_PORT_REMOVE, port_name, name_len);

    // extra 模式打印
    extra_print_send(CMD_PORT_REMOVE, port_name, name_len);
}

void protocol_send_connect_resp(uint8_t status) {
    // 帧格式：[0x05][status][0x57][0x8a]
    protocol_send_frame(CMD_CONNECT_RESP, &status, 1);

    // extra 模式打印
    extra_print_send(CMD_CONNECT_RESP, &status, 1);
}

// ═══════════════════════════════════════════════════════════════
// 接收接口
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 原始数据入口：累积到帧缓冲，然后拆出完整帧
 *        由 pipe 接收线程回调调用
 */
void protocol_on_receive(const uint8_t *data, size_t len) {
    if (len == 0) return;

    // 累积到帧缓冲区
    size_t remaining = RECV_FRAME_MAX - g_recv_frame_len;
    if (len > remaining) {
        len = remaining;
    }
    memcpy(g_recv_frame_buf + g_recv_frame_len, data, len);
    g_recv_frame_len += len;

    // 尝试拆分完整帧
    protocol_parse_frames();
}

/**
 * @brief 处理单条已拆包的完整帧（不含 [0x57][0x8a] 结束标记）
 *        由 protocol_parse_frames 内部调用
 */
static void protocol_on_receive_one(const uint8_t *data, size_t len) {
    if (len < 1) return;

    uint8_t cmd = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_len = len - 1;

    // extra 模式打印
    extra_print_recv(cmd, payload, payload_len);

    switch (cmd) {
    case CMD_MSG:
        // 0x01 消息存入接收缓冲区（仅数据部分，不含命令码）
        if (g_recv_buf) {
            insertBufferViaString(g_recv_buf, (const char *)payload);
            // 缓冲区容量保护：超过 102400 字节时清空
            if (g_recv_buf->len > 102400) {
                cleanBuffer(g_recv_buf);
            }
        }

        // 0x01 消息回调：将 payload 转发给 main.c 处理（写入串口 + 日志）
        if (g_msg_callback) {
            g_msg_callback(payload, payload_len);
        }
        break;

    case CMD_CONNECT:
        // 0x04 连接/断开请求：交由 main.c 中注册的回调处理
        if (g_connect_callback) {
            g_connect_callback(payload, payload_len);
        }
        break;

    default:
        // 0x02/0x03/其他：由 Rust UI 侧处理，C 侧不记录
        break;
    }
}

// ═══════════════════════════════════════════════════════════════
// 缓冲区访问
// ═══════════════════════════════════════════════════════════════

BUFFER* protocol_get_send_buf(void) {
    return g_send_buf;
}

BUFFER* protocol_get_recv_buf(void) {
    return g_recv_buf;
}