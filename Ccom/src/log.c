#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

// 全局状态变量：是否启用日志
bool g_log_enabled = false;

// 内部状态
static char g_log_dir[512] = {0};           // log/[timestamp]/
static char g_send_byte_file[512] = {0};    // log/[timestamp]/send_byte_log.log
static char g_recv_byte_file[512] = {0};    // log/[timestamp]/receive_byte_log.log
static char g_send_ascii_file[512] = {0};   // log/[timestamp]/send_ascii_log.log
static char g_recv_ascii_file[512] = {0};   // log/[timestamp]/receive_ascii_log.log
static char g_startup_timestamp[64] = {0};  // 启动时保存的时间戳，后续所有日志统一使用
static bool g_initialized = false;           // 是否已初始化

// 创建目录（递归创建中间目录）
static int log_mkdir(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

// 将缓冲区数据格式化为十六进制+ASCII hexdump 字符串
static void log_format_hexdump(const uint8_t *data, uint32_t len, char *out, size_t out_size) {
    size_t pos = 0;
    for (uint32_t offset = 0; offset < len; offset += 16) {
        // 偏移量
        int written = snprintf(out + pos, out_size - pos, "%08X  ", offset);
        if (written < 0 || (size_t)written >= out_size - pos) return;
        pos += written;

        // 十六进制部分
        for (uint32_t i = 0; i < 16; i++) {
            if (offset + i < len) {
                written = snprintf(out + pos, out_size - pos, "%02X ", data[offset + i]);
            } else {
                written = snprintf(out + pos, out_size - pos, "   ");
            }
            if (written < 0 || (size_t)written >= out_size - pos) return;
            pos += written;
            if (i == 7) {
                if (pos < out_size) out[pos++] = ' ';
            }
        }

        // ASCII 部分
        if (pos < out_size) out[pos++] = ' ';
        if (pos < out_size) out[pos++] = '|';
        for (uint32_t i = 0; i < 16; i++) {
            if (offset + i < len) {
                uint8_t c = data[offset + i];
                if (pos < out_size) out[pos++] = (c >= 32 && c <= 126) ? (char)c : '.';
            } else {
                if (pos < out_size) out[pos++] = ' ';
            }
        }
        if (pos < out_size) out[pos++] = '|';
        if (pos < out_size) out[pos++] = '\n';
    }
    if (pos < out_size) out[pos] = '\0';
}

// 提取缓冲区中的可打印 ASCII 字符，遇到 '\n' (0x0A) 时换行
static void log_format_ascii(const uint8_t *data, uint32_t len, char *out, size_t out_size) {
    size_t pos = 0;
    for (uint32_t i = 0; i < len && pos < out_size - 1; i++) {
        uint8_t c = data[i];
        if (c == '\n') {
            // 遇到换行符，在输出中也换行
            if (pos < out_size - 1) out[pos++] = '\n';
        } else if (c >= 32 && c <= 126) {
            out[pos++] = (char)c;
        }
        // 其他控制字符（如 \r 等）跳过
    }
    out[pos] = '\0';
}

// 初始化日志文件头部（3行：时间戳、说明信息、分割线）
static void log_init_file_header(const char *filepath, const char *file_description) {
    FILE *fp = fopen(filepath, "w");
    if (fp == NULL) return;

    // 第1行：启动时间戳
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(fp, "# Log started at: %s\n", time_buf);

    // 第2行：文件说明信息
    fprintf(fp, "# File: %s\n", file_description);

    // 第3行：分割线
    fprintf(fp, "# ==================================================\n");

    fclose(fp);
}

// 追加写入 byte 日志文件（hexdump 格式），纯追加不打印时间戳
static void log_write_byte_file(const char *filepath, const uint8_t *data, uint32_t len) {
    if (!g_log_enabled || !g_initialized || filepath[0] == '\0') return;
    if (data == NULL || len == 0) return;

    FILE *fp = fopen(filepath, "a");
    if (fp == NULL) {
        fprintf(stderr, "log: failed to open file %s: %s\n", filepath, strerror(errno));
        return;
    }

    char formatted[65536];
    log_format_hexdump(data, len, formatted, sizeof(formatted));
    fprintf(fp, "%s", formatted);

    fclose(fp);
}

// 追加写入 ASCII 日志文件（仅可打印字符，遇 \n 换行），纯追加不打印时间戳
static void log_write_ascii_file(const char *filepath, const uint8_t *data, uint32_t len) {
    if (!g_log_enabled || !g_initialized || filepath[0] == '\0') return;
    if (data == NULL || len == 0) return;

    FILE *fp = fopen(filepath, "a");
    if (fp == NULL) {
        fprintf(stderr, "log: failed to open file %s: %s\n", filepath, strerror(errno));
        return;
    }

    char ascii_buf[65536];
    log_format_ascii(data, len, ascii_buf, sizeof(ascii_buf));
    if (ascii_buf[0] != '\0') {
        fprintf(fp, "%s", ascii_buf);
    } else {
        fprintf(fp, "(no printable ASCII characters)\n");
    }

    fclose(fp);
}

void log_init(bool enable, const char *timestamp_str) {
    g_log_enabled = enable;
    if (!enable) return;

    // 确保基础目录 log/ 存在
    if (log_mkdir(LOG_BASE_DIR) != 0) {
        fprintf(stderr, "log: failed to create base directory %s: %s\n", LOG_BASE_DIR, strerror(errno));
        g_log_enabled = false;
        return;
    }

    // 确定启动时间戳
    if (timestamp_str == NULL || timestamp_str[0] == '\0') {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(g_startup_timestamp, sizeof(g_startup_timestamp), "%Y%m%d_%H%M%S", tm_info);
    } else {
        snprintf(g_startup_timestamp, sizeof(g_startup_timestamp), "%s", timestamp_str);
    }

    // 构建目录路径: log/[timestamp]/
    snprintf(g_log_dir, sizeof(g_log_dir), LOG_BASE_DIR "/%s", g_startup_timestamp);

    // 创建时间戳子目录
    if (log_mkdir(g_log_dir) != 0) {
        fprintf(stderr, "log: failed to create log directory %s: %s\n", g_log_dir, strerror(errno));
        g_log_enabled = false;
        return;
    }

    // 构建4个日志文件路径
    snprintf(g_send_byte_file, sizeof(g_send_byte_file), "%s/send_byte_log.log", g_log_dir);
    snprintf(g_recv_byte_file, sizeof(g_recv_byte_file), "%s/receive_byte_log.log", g_log_dir);
    snprintf(g_send_ascii_file, sizeof(g_send_ascii_file), "%s/send_ascii_log.log", g_log_dir);
    snprintf(g_recv_ascii_file, sizeof(g_recv_ascii_file), "%s/receive_ascii_log.log", g_log_dir);

    // 初始化4个日志文件头部
    log_init_file_header(g_send_byte_file, "Send Buffer (hexdump)");
    log_init_file_header(g_recv_byte_file, "Receive Buffer (hexdump)");
    log_init_file_header(g_send_ascii_file, "Send Buffer (ASCII)");
    log_init_file_header(g_recv_ascii_file, "Receive Buffer (ASCII)");

    g_initialized = true;

    fprintf(stderr, "log: enabled, dir=%s\n", g_log_dir);
    fprintf(stderr, "log:   send_byte_log=%s\n", g_send_byte_file);
    fprintf(stderr, "log:   recv_byte_log=%s\n", g_recv_byte_file);
    fprintf(stderr, "log:   send_ascii_log=%s\n", g_send_ascii_file);
    fprintf(stderr, "log:   recv_ascii_log=%s\n", g_recv_ascii_file);
}

void log_deinit(void) {
    g_log_enabled = false;
    g_initialized = false;
    g_log_dir[0] = '\0';
    g_send_byte_file[0] = '\0';
    g_recv_byte_file[0] = '\0';
    g_send_ascii_file[0] = '\0';
    g_recv_ascii_file[0] = '\0';
    g_startup_timestamp[0] = '\0';
}

void log_write_send(const uint8_t *data, uint32_t len) {
    log_write_byte_file(g_send_byte_file, data, len);
}

void log_write_recv(const uint8_t *data, uint32_t len) {
    log_write_byte_file(g_recv_byte_file, data, len);
}

void log_write_send_ascii(const uint8_t *data, uint32_t len) {
    log_write_ascii_file(g_send_ascii_file, data, len);
}

void log_write_recv_ascii(const uint8_t *data, uint32_t len) {
    log_write_ascii_file(g_recv_ascii_file, data, len);
}