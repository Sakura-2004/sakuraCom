// 引入头文件
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>
#include "main.h"
#include "protocol.h"
#include "serial_monitor.h"
#include "log.h"

static volatile int g_running = 1;

// ═══════════════════════════════════════════════════════════════
// 全局连接状态
// ═══════════════════════════════════════════════════════════════

volatile conn_state_t g_conn_state = CONN_STATE_IDLE;

// ═══════════════════════════════════════════════════════════════
// 回调函数：子线程读取到数据后调用
// 数据为原始帧（含命令码），交给 protocol 层解析
// ═══════════════════════════════════════════════════════════════

static void recv_callback(const char *data, size_t len) {
    protocol_on_receive((const uint8_t *)data, len);
}

// ═══════════════════════════════════════════════════════════════
// 0x04 连接/断开请求处理回调
// payload 格式：
//   连接：  [波特率序号][数据位序号][校验位序号][停止位序号] （4字节）
//   断开：  [0xff] （1字节）
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// 0x01 消息回调：收到 RustUI 发来的数据后写入串口 + 日志
// ═══════════════════════════════════════════════════════════════

static void handle_msg_to_serial(const uint8_t *payload, size_t len) {
    if (len == 0) return;

    // 如果已连接串口，写入串口
    if (g_conn_state == CONN_STATE_CONNECTED) {
        int serial_fd = serial_monitor_get_fd();
        if (serial_fd >= 0) {
            // 日志：向串口写之前记录发送数据
            if (g_log_enabled) {
                log_write_send(payload, (uint32_t)len);
                log_write_send_ascii(payload, (uint32_t)len);
            }
            int w = (int)write(serial_fd, payload, len);
            if (w < 0) {
                fprintf(stderr, "[main] serial write error (from UI): %s\n", strerror(errno));
            }
        }
    }
}

static void handle_connect_request(const uint8_t *payload, size_t len) {
    // 断开连接请求
    if (len >= 1 && payload[0] == CONNECT_REQUEST_DISCONNECT) {
        serial_monitor_disconnect();
        g_conn_state = CONN_STATE_IDLE;
        protocol_send_connect_resp(CONNECT_STATUS_DISCONNECT_OK);
        fprintf(stderr, "[main] disconnect request processed, state=IDLE\n");
        return;
    }

    // 连接请求：需要 4 字节参数
    if (len < 4) {
        fprintf(stderr, "[main] connect request payload too short: %zu bytes\n", len);
        protocol_send_connect_resp(CONNECT_STATUS_FAIL);
        return;
    }

    int baudrate_idx = (int)payload[0];
    int databits_idx = (int)payload[1];
    int parity_idx   = (int)payload[2];
    int stopbits_idx = (int)payload[3];

    fprintf(stderr, "[main] connect request: baud=%d, data=%d, parity=%d, stop=%d\n",
            baudrate_idx, databits_idx, parity_idx, stopbits_idx);

    // 需要知道目标端口名称 —— 从下拉框选中项获取
    // 当前端口名通过 protocol 的 0x02 通知已经发给了 UI，UI 会记住选中项
    // 这里需要获取选中的端口名 —— 暂时使用一种约定：
    // 从 serial_monitor 的已知端口列表中取一个（或者由 UI 在连接请求中附带端口名）
    //
    // 方案：连接请求的 payload 格式扩展为：
    //   [port_name_len(1byte)][port_name...][baudrate_idx][databits_idx][parity_idx][stopbits_idx]
    if (len >= 5) {
        // port_name_len + port_name + 4 参数
        uint8_t name_len = payload[0];
        const uint8_t *name_start = payload + 1;
        int baudrate_idx2 = (int)payload[1 + name_len];
        int databits_idx2 = (int)payload[2 + name_len];
        int parity_idx2   = (int)payload[3 + name_len];
        int stopbits_idx2 = (int)payload[4 + name_len];

        // 临时字符串
        char port_name[256];
        if (name_len >= sizeof(port_name)) name_len = (uint8_t)(sizeof(port_name) - 1);
        memcpy(port_name, name_start, name_len);
        port_name[name_len] = '\0';

        fprintf(stderr, "[main] target port: %s\n", port_name);

        int ret = serial_monitor_connect(port_name,
                                         baudrate_idx2, databits_idx2,
                                         parity_idx2, stopbits_idx2);
        if (ret == 0) {
            g_conn_state = CONN_STATE_CONNECTED;
            protocol_send_connect_resp(CONNECT_STATUS_SUCCESS);
            fprintf(stderr, "[main] connect success, state=CONNECTED\n");
        } else {
            g_conn_state = CONN_STATE_IDLE;
            protocol_send_connect_resp(CONNECT_STATUS_FAIL);
            fprintf(stderr, "[main] connect failed, state=IDLE\n");
        }
        return;
    }

    // 旧格式兼容（无端口名）：尝试使用已知端口列表的第一个
    if (g_conn_state == CONN_STATE_IDLE) {
        fprintf(stderr, "[main] legacy connect: no port name, using first known port\n");
        // serial_monitor 内部已知端口列表不可外部访问
        // 这里返回失败，让 UI 使用新格式重发
        protocol_send_connect_resp(CONNECT_STATUS_FAIL);
        return;
    }

    protocol_send_connect_resp(CONNECT_STATUS_FAIL);
}

// ═══════════════════════════════════════════════════════════════
// 信号处理
// ═══════════════════════════════════════════════════════════════

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// ═══════════════════════════════════════════════════════════════
// Extra 模式：完整流程
// ═══════════════════════════════════════════════════════════════

static int run_extra_mode(void) {
    // 创建唯一的 FIFO 路径（基于 PID）
    char fifo_to_ui_path[128];
    char fifo_from_ui_path[128];
    snprintf(fifo_to_ui_path, sizeof(fifo_to_ui_path),
             "/tmp/sakuracom_%d_to_ui", getpid());
    snprintf(fifo_from_ui_path, sizeof(fifo_from_ui_path),
             "/tmp/sakuracom_%d_from_ui", getpid());

    pipe_set_fifo_paths(fifo_to_ui_path, fifo_from_ui_path);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // 创建 FIFO
    if (mkfifo(fifo_to_ui_path, 0666) < 0) {
        perror("mkfifo to_ui");
        return 1;
    }
    if (mkfifo(fifo_from_ui_path, 0666) < 0) {
        perror("mkfifo from_ui");
        unlink(fifo_to_ui_path);
        return 1;
    }

    // 获取当前工作目录
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        pipe_cleanup();
        return 1;
    }

    // 在 Terminal.app 中启动 RustUI
    char osa_cmd[2560];
    snprintf(osa_cmd, sizeof(osa_cmd),
        "osascript -e 'tell application \"Terminal\" to activate' "
        "-e 'tell application \"Terminal\" to do script "
        "\"cd \\\"%s\\\" && exec ./RustUI/UI --read-fd 3 --write-fd 4 "
        "3< \\\"%s\\\" 4> \\\"%s\\\"; exec bash\"'",
        cwd, fifo_to_ui_path, fifo_from_ui_path);

    printf("[DEBUG] Launching RustUI in new Terminal window...\n");
    if (system(osa_cmd) != 0) {
        fprintf(stderr, "Failed to launch Terminal for RustUI\n");
        pipe_cleanup();
        return 1;
    }

    usleep(500000);

    printf("[DEBUG] Waiting for RustUI to connect via FIFO...\n");

    // 打开 FIFO
    int to_ui_fd = open(fifo_to_ui_path, O_WRONLY);
    if (to_ui_fd < 0) {
        perror("open to_ui fifo");
        pipe_cleanup();
        return 1;
    }
    pipe_set_write_fd(to_ui_fd);

    int from_ui_fd = open(fifo_from_ui_path, O_RDONLY);
    if (from_ui_fd < 0) {
        perror("open from_ui fifo");
        pipe_cleanup();
        return 1;
    }
    pipe_set_read_fd(from_ui_fd);

    printf("[DEBUG] RustUI connected! Debug console active.\n");
    printf("[DEBUG] Type in this terminal to send to UI.\n");
    printf("[DEBUG] Data from UI will appear below.\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // 发送初始数据
    protocol_send_msg("SakuraCom Debug Mode - Extra Terminal\n", 39);

    // 注册回调
    protocol_set_connect_callback(handle_connect_request);
    protocol_set_msg_callback(handle_msg_to_serial);

    // ── 启动接收子线程 ──
    if (pipe_start_receiver(recv_callback) != 0) {
        fprintf(stderr, "Failed to start receiver thread\n");
        serial_monitor_cleanup();
        protocol_cleanup();
        pipe_cleanup();
        return 1;
    }

    // ── 主线程：stdin + 串口数据路由 + 10s 串口轮询 ──
    char buf[4096];
    time_t last_poll = time(NULL);

    while (g_running && pipe_is_receiver_running()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = STDIN_FILENO;

        // 如果已连接串口，同时监控串口 fd
        int serial_fd = serial_monitor_get_fd();
        if (g_conn_state == CONN_STATE_CONNECTED && serial_fd >= 0) {
            FD_SET(serial_fd, &read_fds);
            if (serial_fd > max_fd) max_fd = serial_fd;
        }

        struct timeval tv = {1, 0};
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR || errno == EBADF) continue;
            break;
        }

        // 检查 stdin
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            int n = (int)read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n <= 0) {
                break;
            }
            buf[n] = '\0';

            if (strcmp(buf, "exit\n") == 0 || strcmp(buf, "quit\n") == 0) {
                printf("[DEBUG] Exiting...\n");
                break;
            }

            // 如果已连接串口，发送到串口
            if (g_conn_state == CONN_STATE_CONNECTED) {
                // 日志：向串口写之前记录发送数据
                if (g_log_enabled && n > 0) {
                    log_write_send((const uint8_t *)buf, (uint32_t)n);
                    log_write_send_ascii((const uint8_t *)buf, (uint32_t)n);
                }
                int w = (int)write(serial_monitor_get_fd(), buf, (size_t)n);
                if (w < 0) {
                    fprintf(stderr, "[main] serial write error: %s\n", strerror(errno));
                }
            }

            // 同时通过协议通知 UI
            protocol_send_msg(buf, (size_t)n);
        }

        // 检查串口接收
        if (ret > 0 && serial_fd >= 0 && FD_ISSET(serial_fd, &read_fds)) {
            char serial_buf[4096];
            int nr = (int)read(serial_fd, serial_buf, sizeof(serial_buf));
            if (nr > 0) {
                // 日志：从串口读出后直接记录接收数据
                if (g_log_enabled) {
                    log_write_recv((const uint8_t *)serial_buf, (uint32_t)nr);
                    log_write_recv_ascii((const uint8_t *)serial_buf, (uint32_t)nr);
                }
                // 串口数据作为 0x01 消息发送给 UI
                protocol_send_msg(serial_buf, (size_t)nr);
            } else if (nr < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "[main] serial read error: %s\n", strerror(errno));
                // 串口断连，自动回退到 IDLE
                serial_monitor_disconnect();
                g_conn_state = CONN_STATE_IDLE;
                protocol_send_connect_resp(0xf0); // 通知 UI 断连
            }
        }

        // 每 2 秒轮询串口变化
        time_t now = time(NULL);
        if (now - last_poll >= 2) {
            serial_monitor_poll();
            last_poll = now;
        }
    }

    // ── 清理：先停止接收线程，再释放其他资源，防止 use-after-free 导致 SIGTRAP ──
    pipe_stop_receiver();       // 1. 停止接收线程（不再触发回调）
    protocol_cleanup();         // 2. 清理协议层缓冲区
    serial_monitor_cleanup();   // 3. 断开串口 + 释放端口列表
    pipe_cleanup();             // 4. 关闭 fd + 删除 FIFO（receiver 已停，安全）
    printf("[DEBUG] Debug mode exited.\n");
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// 正常模式：完整流程
// ═══════════════════════════════════════════════════════════════

static int run_normal_mode(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // 先搜索串口（UI 尚未启动，仅记录不发通知）
    if (serial_monitor_scan_only() != 0) {
        // UI 尚未启动，无法通知，静默处理
    }

    // 创建匿名管道
    int pipe_to_ui[2];
    int pipe_from_ui[2];
    if (pipe(pipe_to_ui) < 0 || pipe(pipe_from_ui) < 0) {
        serial_monitor_cleanup();
        protocol_cleanup();
        return 1;
    }

    // fork 启动 RustUI
    pid_t pid = fork();
    if (pid < 0) {
        serial_monitor_cleanup();
        protocol_cleanup();
        return 1;
    }

    if (pid == 0) {
        // 子进程：RustUI
        // 重定向 stderr 到 /dev/null，stdout 必须保留给 ratatui TUI 渲染
        if (!freopen("/dev/null", "w", stderr)) { /* 静默 */ }
        // 注意：不要重定向 stdout，ratatui 需要它渲染界面

        close(pipe_to_ui[1]);
        close(pipe_from_ui[0]);

        if (dup2(pipe_to_ui[0], 3) < 0) { perror("dup2 to_ui"); exit(1); }
        if (dup2(pipe_from_ui[1], 4) < 0) { perror("dup2 from_ui"); exit(1); }

        if (pipe_to_ui[0] != 3) close(pipe_to_ui[0]);
        if (pipe_from_ui[1] != 4) close(pipe_from_ui[1]);

        char *const args[] = { "./RustUI/UI", "--read-fd", "3", "--write-fd", "4", NULL };
        execvp(args[0], args);

        char *const args2[] = { "RustUI/UI", "--read-fd", "3", "--write-fd", "4", NULL };
        execvp(args2[0], args2);

        perror("execvp");
        exit(1);
    }

    // 父进程：C 进程不输出任何日志到终端，重定向 stdout 和 stderr
    // 必须在 fork 之后执行，否则会影响子进程的 TUI 渲染
    if (!freopen("/dev/null", "w", stderr)) { /* 静默 */ }
    if (!freopen("/dev/null", "w", stdout)) { /* 静默 */ }

    // 父进程：设置管道 fd
    close(pipe_to_ui[0]);
    close(pipe_from_ui[1]);
    pipe_set_write_fd(pipe_to_ui[1]);
    pipe_set_read_fd(pipe_from_ui[0]);

    // 发送初始数据
    protocol_send_msg("SakuraCom Serial Tool Ready!\n", 32);

    // 将预扫描的串口列表通知 UI（0x02 命令）
    serial_monitor_notify_init();

    // 注册回调
    protocol_set_connect_callback(handle_connect_request);
    protocol_set_msg_callback(handle_msg_to_serial);

    // ── 启动接收子线程 ──
    if (pipe_start_receiver(recv_callback) != 0) {
        fprintf(stderr, "Failed to start receiver thread\n");
        serial_monitor_cleanup();
        protocol_cleanup();
        pipe_cleanup();
        waitpid(pid, NULL, 0);
        return 1;
    }

    // ── 主线程：等待子进程退出 + 串口数据路由 + 10s 串口轮询 ──
    time_t last_poll = time(NULL);

    while (g_running && pipe_is_receiver_running()) {
        // 检查子进程是否退出
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            break;  // 子进程已退出
        }

        // 如果已连接串口，轮询串口数据
        if (g_conn_state == CONN_STATE_CONNECTED) {
            fd_set read_fds;
            FD_ZERO(&read_fds);

            int serial_fd = serial_monitor_get_fd();
            int max_fd = -1;

            if (serial_fd >= 0) {
                FD_SET(serial_fd, &read_fds);
                max_fd = serial_fd;
            }

            if (max_fd >= 0) {
                struct timeval tv = {0, 100000}; // 100ms 超时
                int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

                if (ret > 0 && FD_ISSET(serial_fd, &read_fds)) {
                    char serial_buf[4096];
                    int nr = (int)read(serial_fd, serial_buf, sizeof(serial_buf));
                    if (nr > 0) {
                        // 日志：从串口读出后直接记录接收数据
                        if (g_log_enabled) {
                            log_write_recv((const uint8_t *)serial_buf, (uint32_t)nr);
                            log_write_recv_ascii((const uint8_t *)serial_buf, (uint32_t)nr);
                        }
                        // 串口数据作为 0x01 消息发送给 UI
                        protocol_send_msg(serial_buf, (size_t)nr);
                    } else if (nr < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        fprintf(stderr, "[main] serial read error: %s\n", strerror(errno));
                        serial_monitor_disconnect();
                        g_conn_state = CONN_STATE_IDLE;
                        protocol_send_connect_resp(CONNECT_STATUS_DISCONNECT_OK);
                    }
                }
            }
        }

        // 每 3 秒轮询串口变化
        time_t now = time(NULL);
        if (now - last_poll >= 3) {
            serial_monitor_poll();
            last_poll = now;
        }

        // 100ms 间隔检查
        usleep(100000);
    }

    // ── 清理：先停止接收线程，再释放其他资源，防止 use-after-free 导致 SIGTRAP ──
    pipe_stop_receiver();       // 1. 停止接收线程（不再触发回调）
    protocol_cleanup();         // 2. 清理协议层缓冲区
    serial_monitor_cleanup();   // 3. 断开串口 + 释放端口列表
    pipe_cleanup();             // 4. 关闭管道 fd（receiver 已停，安全）
    waitpid(pid, NULL, 0);
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// 主入口
// ═══════════════════════════════════════════════════════════════

int main(int argc, char *argv[]) {
    int extra_mode = 0;
    int log_enabled = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-extra") == 0) {
            extra_mode = 1;
        } else if (strcmp(argv[i], "-log") == 0) {
            log_enabled = 1;
        }
    }

    // 初始化 log 组件（在 protocol_init 之前，以便 protocol 可以使用 log）
    if (log_enabled) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
        log_init(true, timestamp);
    } else {
        log_init(false, NULL);
    }

    pipe_set_mode(extra_mode);
    protocol_init();

    g_conn_state = CONN_STATE_IDLE;

    if (extra_mode) {
        int ret = run_extra_mode();
        log_deinit();
        return ret;
    }

    int ret = run_normal_mode();
    log_deinit();
    return ret;
}
