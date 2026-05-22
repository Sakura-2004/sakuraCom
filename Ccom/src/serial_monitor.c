#include "serial_monitor.h"
#include "protocol.h"
#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

// ═══════════════════════════════════════════════════════════════
// 已知串口列表（简单动态数组）
// ═══════════════════════════════════════════════════════════════

#define MAX_PORTS 64
static char *g_known_ports[MAX_PORTS];
static int g_known_count = 0;

// ═══════════════════════════════════════════════════════════════
// 串口连接状态
// ═══════════════════════════════════════════════════════════════

static int g_serial_fd = -1;         // 当前连接串口的文件描述符
static struct sp_port *g_sp_port = NULL;  // libserialport 端口对象

// 波特率映射表（序号 → 实际值）
static const int g_baudrate_table[] = {
    9600, 19200, 38400, 57600,
    115200, 230400, 460800, 921600
};
static const int g_baudrate_count = 8;

// 数据位映射表（序号 → termios 值）
static const int g_databits_table[] = {
    8, 7, 6, 5
};
static const int g_databits_count = 4;

// 校验位映射表（序号 → libserialport 枚举）
// 0=无, 1=奇, 2=偶, 3=mark(1), 4=space(0)
static const enum sp_parity g_parity_table[] = {
    SP_PARITY_NONE,
    SP_PARITY_ODD,
    SP_PARITY_EVEN,
    SP_PARITY_MARK,
    SP_PARITY_SPACE
};
static const int g_parity_count = 5;

// 停止位映射表（序号 → 实际值）
// 0=1, 1=1.5, 2=2
static const int g_stopbits_table[] = {
    1,  // SP_STOPBITS_ONE
    3,  // SP_STOPBITS_ONE_POINT_FIVE
    2   // SP_STOPBITS_TWO
};
static const int g_stopbits_count = 3;

// ═══════════════════════════════════════════════════════════════
// 内部辅助
// ═══════════════════════════════════════════════════════════════

static void free_known_ports(void) {
    for (int i = 0; i < g_known_count; i++) {
        free(g_known_ports[i]);
        g_known_ports[i] = NULL;
    }
    g_known_count = 0;
}

static int port_in_known(const char *name) {
    for (int i = 0; i < g_known_count; i++) {
        if (strcmp(g_known_ports[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_to_known(const char *name) {
    if (g_known_count >= MAX_PORTS) return;
    g_known_ports[g_known_count++] = strdup(name);
}

// ═══════════════════════════════════════════════════════════════
// 公共接口：生命周期
// ═══════════════════════════════════════════════════════════════

int serial_monitor_init(void) {
    struct sp_port **port_list = NULL;

    enum sp_return result = sp_list_ports(&port_list);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor: sp_list_ports failed: %d\n", result);
        return -1;
    }

    if (!port_list) {
        // 无串口，这是正常情况
        return 0;
    }

    // 遍历所有串口，加入已知列表并发送 0x02 通知 UI
    for (int i = 0; port_list[i] != NULL; i++) {
        const char *name = sp_get_port_name(port_list[i]);
        if (name) {
            add_to_known(name);
            protocol_send_port_add(name);
        }
    }

    sp_free_port_list(port_list);
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// scan_only: 仅枚举串口并记录，不发送通知（UI 尚未启动）
// ═══════════════════════════════════════════════════════════════

int serial_monitor_scan_only(void) {
    struct sp_port **port_list = NULL;

    if (sp_list_ports(&port_list) != SP_OK) {
        return -1;
    }

    if (!port_list) {
        return 0;
    }

    for (int i = 0; port_list[i] != NULL; i++) {
        const char *name = sp_get_port_name(port_list[i]);
        if (name) {
            add_to_known(name);
            // 不调用 protocol_send_port_add，UI 尚未连接
        }
    }

    sp_free_port_list(port_list);
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// notify_init: 将 scan_only 记录的串口逐条发送 0x02 给 UI
// ═══════════════════════════════════════════════════════════════

void serial_monitor_notify_init(void) {
    for (int i = 0; i < g_known_count; i++) {
        protocol_send_port_add(g_known_ports[i]);
    }
}

void serial_monitor_poll(void) {
    struct sp_port **port_list = NULL;

    if (sp_list_ports(&port_list) != SP_OK) {
        return;
    }

    int current_count = 0;
    char *current_names[MAX_PORTS];

    // 收集当前串口名称
    if (port_list) {
        for (int i = 0; port_list[i] != NULL && current_count < MAX_PORTS; i++) {
            const char *name = sp_get_port_name(port_list[i]);
            if (name) {
                current_names[current_count] = strdup(name);
                current_count++;
            }
        }
        sp_free_port_list(port_list);
        port_list = NULL;
    }

    // 查找新增（在当前列表中但不在已知列表）
    for (int i = 0; i < current_count; i++) {
        if (!port_in_known(current_names[i])) {
            add_to_known(current_names[i]);
            protocol_send_port_add(current_names[i]);
        }
    }

    // 查找移除（在已知列表中但不在当前列表）
    for (int i = 0; i < g_known_count; i++) {
        int found = 0;
        for (int j = 0; j < current_count; j++) {
            if (strcmp(g_known_ports[i], current_names[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            protocol_send_port_remove(g_known_ports[i]);
            free(g_known_ports[i]);
            // 从数组中移除该元素
            for (int k = i; k < g_known_count - 1; k++) {
                g_known_ports[k] = g_known_ports[k + 1];
            }
            g_known_count--;
            i--; // 回退索引，因为数组元素前移
        }
    }

    // 清理当前名称临时数组
    for (int i = 0; i < current_count; i++) {
        free(current_names[i]);
    }
}

void serial_monitor_cleanup(void) {
    // 断开串口连接
    serial_monitor_disconnect();
    free_known_ports();
}

// ═══════════════════════════════════════════════════════════════
// 串口连接/断开
// ═══════════════════════════════════════════════════════════════

int serial_monitor_connect(
    const char *port_name,
    int baudrate_idx,
    int databits_idx,
    int parity_idx,
    int stopbits_idx
) {
    // 如果已连接，先断开
    if (g_serial_fd >= 0 || g_sp_port != NULL) {
        serial_monitor_disconnect();
    }

    // 参数校验
    if (!port_name || port_name[0] == '\0') {
        fprintf(stderr, "serial_monitor_connect: invalid port_name\n");
        return -1;
    }
    if (baudrate_idx < 0 || baudrate_idx >= g_baudrate_count) {
        fprintf(stderr, "serial_monitor_connect: invalid baudrate_idx %d\n", baudrate_idx);
        return -1;
    }
    if (databits_idx < 0 || databits_idx >= g_databits_count) {
        fprintf(stderr, "serial_monitor_connect: invalid databits_idx %d\n", databits_idx);
        return -1;
    }
    if (parity_idx < 0 || parity_idx >= g_parity_count) {
        fprintf(stderr, "serial_monitor_connect: invalid parity_idx %d\n", parity_idx);
        return -1;
    }
    if (stopbits_idx < 0 || stopbits_idx >= g_stopbits_count) {
        fprintf(stderr, "serial_monitor_connect: invalid stopbits_idx %d\n", stopbits_idx);
        return -1;
    }

    // 使用 libserialport 获取端口
    enum sp_return result = sp_get_port_by_name(port_name, &g_sp_port);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor_connect: sp_get_port_by_name(%s) failed: %d\n",
                port_name, result);
        g_sp_port = NULL;
        return -1;
    }

    // 打开串口（读写模式）
    result = sp_open(g_sp_port, SP_MODE_READ_WRITE);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor_connect: sp_open(%s) failed: %d\n",
                port_name, result);
        sp_free_port(g_sp_port);
        g_sp_port = NULL;
        return -1;
    }

    // 配置串口参数
    int baudrate = g_baudrate_table[baudrate_idx];
    int databits = g_databits_table[databits_idx];
    enum sp_parity parity = g_parity_table[parity_idx];
    int stopbits = g_stopbits_table[stopbits_idx];

    result = sp_set_baudrate(g_sp_port, baudrate);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor_connect: sp_set_baudrate(%d) failed\n", baudrate);
        goto fail;
    }

    result = sp_set_bits(g_sp_port, databits);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor_connect: sp_set_bits(%d) failed\n", databits);
        goto fail;
    }

    result = sp_set_parity(g_sp_port, parity);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor_connect: sp_set_parity failed\n");
        goto fail;
    }

    result = sp_set_stopbits(g_sp_port, stopbits);
    if (result != SP_OK) {
        fprintf(stderr, "serial_monitor_connect: sp_set_stopbits failed\n");
        goto fail;
    }

    // 获取底层 fd 用于 select 轮询
    result = sp_get_port_handle(g_sp_port, &g_serial_fd);
    if (result != SP_OK || g_serial_fd < 0) {
        fprintf(stderr, "serial_monitor_connect: sp_get_port_handle failed\n");
        goto fail;
    }

    // 设置 fd 为非阻塞模式
    int flags = fcntl(g_serial_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(g_serial_fd, F_SETFL, flags | O_NONBLOCK);
    }

    fprintf(stderr, "serial_monitor: connected to %s (baud=%d, data=%d, parity=%d, stop=%d)\n",
            port_name, baudrate, databits, parity_idx, stopbits_idx);
    return 0;

fail:
    sp_close(g_sp_port);
    sp_free_port(g_sp_port);
    g_sp_port = NULL;
    g_serial_fd = -1;
    return -1;
}

int serial_monitor_disconnect(void) {
    if (g_sp_port) {
        sp_close(g_sp_port);
        sp_free_port(g_sp_port);
        g_sp_port = NULL;
    }
    g_serial_fd = -1;
    fprintf(stderr, "serial_monitor: disconnected\n");
    return 0;
}

int serial_monitor_get_fd(void) {
    return g_serial_fd;
}