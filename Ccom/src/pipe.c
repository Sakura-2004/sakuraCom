#include "pipe.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

// ═══════════════════════════════════════════════════════════════
// 全局状态
// ═══════════════════════════════════════════════════════════════

static int g_mode = 0;        // 0=正常模式, 1=extra模式
static int g_write_fd = -1;   // C → UI
static int g_read_fd  = -1;   // UI → C

// FIFO 路径（extra 模式专用）
static char g_fifo_to_ui[128];
static char g_fifo_from_ui[128];
static int  g_has_fifo_paths = 0;  // 是否已设置 FIFO 路径

// 接收线程状态
static pthread_t g_recv_thread = 0;
static int g_recv_running = 0;
static pipe_recv_callback_t g_recv_callback = NULL;

// ═══════════════════════════════════════════════════════════════
// 模式管理
// ═══════════════════════════════════════════════════════════════

void pipe_set_mode(int mode) {
    g_mode = mode;
}

int pipe_get_mode(void) {
    return g_mode;
}

// ═══════════════════════════════════════════════════════════════
// fd 管理
// ═══════════════════════════════════════════════════════════════

void pipe_set_write_fd(int fd) {
    g_write_fd = fd;
}

void pipe_set_read_fd(int fd) {
    g_read_fd = fd;
}

int pipe_get_write_fd(void) {
    return g_write_fd;
}

int pipe_get_read_fd(void) {
    return g_read_fd;
}

// ═══════════════════════════════════════════════════════════════
// 核心读写接口
// ═══════════════════════════════════════════════════════════════

ssize_t pipe_write(const void *buf, size_t len) {
    if (g_write_fd < 0) {
        return -1;
    }
    // extra 模式：同步打印到控制台
    if (g_mode == 1) {
        fwrite(buf, 1, len, stdout);
        fflush(stdout);
    }
    return write(g_write_fd, buf, len);
}

ssize_t pipe_read(void *buf, size_t len) {
    if (g_read_fd < 0) {
        return -1;
    }
    return read(g_read_fd, buf, len);
}

// ═══════════════════════════════════════════════════════════════
// extra 模式专用：FIFO 路径管理
// ═══════════════════════════════════════════════════════════════

void pipe_set_fifo_paths(const char *to_ui_path, const char *from_ui_path) {
    if (to_ui_path != NULL) {
        strncpy(g_fifo_to_ui, to_ui_path, sizeof(g_fifo_to_ui) - 1);
        g_fifo_to_ui[sizeof(g_fifo_to_ui) - 1] = '\0';
    }
    if (from_ui_path != NULL) {
        strncpy(g_fifo_from_ui, from_ui_path, sizeof(g_fifo_from_ui) - 1);
        g_fifo_from_ui[sizeof(g_fifo_from_ui) - 1] = '\0';
    }
    g_has_fifo_paths = 1;
}

void pipe_cleanup(void) {
    // 先停止接收线程
    pipe_stop_receiver();

    // 关闭写端
    if (g_write_fd >= 0) {
        close(g_write_fd);
        g_write_fd = -1;
    }

    // 关闭读端
    if (g_read_fd >= 0) {
        close(g_read_fd);
        g_read_fd = -1;
    }

    // extra 模式：删除 FIFO 文件
    if (g_mode == 1 && g_has_fifo_paths) {
        unlink(g_fifo_to_ui);
        unlink(g_fifo_from_ui);
    }
}

// ═══════════════════════════════════════════════════════════════
// 接收线程管理
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 接收线程主函数
 *        循环从管道读取数据，通过回调函数传递给调用者
 */
static void *pipe_recv_thread_func(void *arg) {
    (void)arg;
    char buf[4096];

    while (g_recv_running) {
        ssize_t n = pipe_read(buf, sizeof(buf) - 1);
        if (n <= 0) {
            // 管道关闭或出错，退出线程
            break;
        }
        buf[n] = '\0';

        // 调用回调函数处理数据
        if (g_recv_callback != NULL) {
            g_recv_callback(buf, (size_t)n);
        }
    }

    g_recv_running = 0;
    return NULL;
}

int pipe_start_receiver(pipe_recv_callback_t callback) {
    if (g_read_fd < 0) {
        return -1;  // 读端未初始化
    }

    if (g_recv_running) {
        return 0;   // 已在运行
    }

    g_recv_callback = callback;
    g_recv_running = 1;

    if (pthread_create(&g_recv_thread, NULL, pipe_recv_thread_func, NULL) != 0) {
        g_recv_running = 0;
        return -1;
    }

    return 0;
}

void pipe_stop_receiver(void) {
    if (!g_recv_running) {
        return;
    }

    g_recv_running = 0;

    // 关闭读端以唤醒阻塞在 read() 上的线程
    // 注意：不能用 shutdown()，它对管道/FIFO 无效且在 macOS 上触发 SIGTRAP
    if (g_read_fd >= 0) {
        close(g_read_fd);
        g_read_fd = -1;
    }

    // 等待线程退出
    if (g_recv_thread != 0) {
        pthread_join(g_recv_thread, NULL);
        g_recv_thread = 0;
    }
}

int pipe_is_receiver_running(void) {
    return g_recv_running;
}
