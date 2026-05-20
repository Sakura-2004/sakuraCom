// 引入头文件
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include "main.h"

static char fifo_to_ui_path[128];
static char fifo_from_ui_path[128];

static void cleanup_fifos(void) {
    unlink(fifo_to_ui_path);
    unlink(fifo_from_ui_path);
}

static void signal_handler(int sig) {
    (void)sig;
    cleanup_fifos();
    _exit(1);
}

int main(int argc, char *argv[]) {
    int extra_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-extra") == 0) {
            extra_mode = 1;
        }
    }

    if (extra_mode) {
        // ═══════════════════════════════════════════════════════════════
        // -extra 模式：在独立的 Terminal.app 窗口中启动 RustUI，
        // 当前控制台作为 C 的输入/输出调试工具
        // ═══════════════════════════════════════════════════════════════

        // 创建唯一的 FIFO 路径（基于 PID）
        snprintf(fifo_to_ui_path, sizeof(fifo_to_ui_path),
                 "/tmp/sakuracom_%d_to_ui", getpid());
        snprintf(fifo_from_ui_path, sizeof(fifo_from_ui_path),
                 "/tmp/sakuracom_%d_from_ui", getpid());

        // 注册清理函数
        atexit(cleanup_fifos);
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // 创建 FIFO（命名管道）
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
            cleanup_fifos();
            return 1;
        }

        // 通过 osascript 在 Terminal.app 中打开新窗口运行 RustUI
        // 使用 shell 重定向将 FIFO 绑定到 fd 3 (读) 和 fd 4 (写)
        char osa_cmd[2560];
        snprintf(osa_cmd, sizeof(osa_cmd),
            "osascript -e 'tell application \"Terminal\" to activate' "
            "-e 'tell application \"Terminal\" to do script "
            "\"cd \\\"%s\\\" && exec ./RustUI/UI --read-fd 3 --write-fd 4 "
            "3< \\\"%s\\\" 4> \\\"%s\\\"; exec bash\"'",
            cwd, fifo_to_ui_path, fifo_from_ui_path);

        printf("[DEBUG] Launching RustUI in new Terminal window...\n");
        int sys_ret = system(osa_cmd);
        if (sys_ret != 0) {
            fprintf(stderr, "Failed to launch Terminal for RustUI\n");
            cleanup_fifos();
            return 1;
        }

        // 短暂等待 Terminal 启动
        usleep(500000); // 500ms

        printf("[DEBUG] Waiting for RustUI to connect via FIFO...\n");
        printf("[DEBUG] (Press Ctrl+C to cancel)\n");

        // 打开 FIFO（阻塞直到 RustUI 连接）
        // to_ui_fd: C 写入 → UI 读取
        int to_ui_fd = open(fifo_to_ui_path, O_WRONLY);
        if (to_ui_fd < 0) {
            perror("open to_ui fifo");
            cleanup_fifos();
            return 1;
        }
        // from_ui_fd: UI 写入 → C 读取
        int from_ui_fd = open(fifo_from_ui_path, O_RDONLY);
        if (from_ui_fd < 0) {
            perror("open from_ui fifo");
            close(to_ui_fd);
            cleanup_fifos();
            return 1;
        }

        printf("[DEBUG] RustUI connected! Debug console active.\n");
        printf("[DEBUG] Type in this terminal to send to UI.\n");
        printf("[DEBUG] Data from UI will appear below.\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

        // 发送初始数据到 UI
        const char* init_msg = "SakuraCom Debug Mode - Extra Terminal\n";
        write(to_ui_fd, init_msg, strlen(init_msg));

        // ── 调试 I/O 主循环 ──
        fd_set read_fds;
        char buf[4096];
        int running = 1;

        while (running) {
            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);
            FD_SET(from_ui_fd, &read_fds);
            int max_fd = (from_ui_fd > STDIN_FILENO) ? from_ui_fd : STDIN_FILENO;

            // 5 秒超时，使程序可被信号中断
            struct timeval tv = {5, 0};
            int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

            if (ret < 0) {
                if (errno == EINTR) continue; // 被信号中断，重试
                break;
            }

            // ── 从调试终端（stdin）读取 ──
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                int n = (int)read(STDIN_FILENO, buf, sizeof(buf) - 1);
                if (n <= 0) {
                    // EOF (Ctrl+D) 或错误 → 退出
                    running = 0;
                    break;
                }
                buf[n] = '\0';

                // 检查退出命令
                if (strcmp(buf, "exit\n") == 0 || strcmp(buf, "quit\n") == 0) {
                    printf("[DEBUG] Exiting debug mode...\n");
                    running = 0;
                    break;
                }

                // 将输入发送到 UI 显示
                write(to_ui_fd, buf, (size_t)n);
                printf("[DEBUG] Sent to UI: %s", buf);
            }

            // ── 从 UI 读取 ──
            if (FD_ISSET(from_ui_fd, &read_fds)) {
                int n = (int)read(from_ui_fd, buf, sizeof(buf) - 1);
                if (n <= 0) {
                    // UI 关闭了连接
                    printf("[DEBUG] RustUI disconnected.\n");
                    running = 0;
                    break;
                }
                buf[n] = '\0';
                // 显示来自 UI 的数据
                printf("[UI] %s", buf);
                fflush(stdout);
            }
        }

        // ── 清理 ──
        close(to_ui_fd);
        close(from_ui_fd);
        cleanup_fifos();
        printf("[DEBUG] Debug mode exited.\n");
        return 0;
    }

    // ═══════════════════════════════════════════════════════════════
    // 普通模式：通过匿名管道 fork+exec 启动 RustUI
    // ═══════════════════════════════════════════════════════════════
    // ── 创建 2 个管道 ──
    // pipe_to_ui:   C 写入 → RustUI 读取 (发送数据到 UI)
    // pipe_from_ui: RustUI 写入 → C 读取 (从 UI 接收数据)
    int pipe_to_ui[2];
    int pipe_from_ui[2];

    if (pipe(pipe_to_ui) < 0 || pipe(pipe_from_ui) < 0) {
        perror("pipe");
        return 1;
    }

    // ── 创建 RustUI 子进程 ──
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // ── 子进程：RustUI ──
        // 关闭不需要的管道端
        close(pipe_to_ui[1]);   // 关闭 pipe_to_ui 写端
        close(pipe_from_ui[0]); // 关闭 pipe_from_ui 读端

        // 将 pipe 端口重定位到固定 fd (3 和 4)，方便 Rust 端引用
        if (dup2(pipe_to_ui[0], 3) < 0) {
            perror("dup2 to_ui");
            exit(1);
        }
        if (dup2(pipe_from_ui[1], 4) < 0) {
            perror("dup2 from_ui");
            exit(1);
        }

        // dup2 后关闭原 fd（不再需要）
        close(pipe_to_ui[0]);
        close(pipe_from_ui[1]);

        // 执行 RustUI 可执行文件，传递管道 fd 作为参数
        char *const args[] = {
            "./RustUI/UI",
            "--read-fd", "3",
            "--write-fd", "4",
            NULL
        };
        execvp(args[0], args);

        // exec 失败时尝试备用路径
        char *const args2[] = {
            "RustUI/UI",
            "--read-fd", "3",
            "--write-fd", "4",
            NULL
        };
        execvp(args2[0], args2);

        perror("execvp");
        exit(1);
    }

    // ── 父进程：C 主逻辑 ──
    // 关闭不需要的管道端
    close(pipe_to_ui[0]);   // 关闭 pipe_to_ui 读端
    close(pipe_from_ui[1]); // 关闭 pipe_from_ui 写端

    // 发送初始数据到 RustUI 显示
    const char* test_data = "SakuraCom Serial Tool Ready!\n"
                            "Port: /dev/cu.usbmodem\n"
                            "Baud: 115200\n"
                            "Connection established.\n";
    write(pipe_to_ui[1], test_data, strlen(test_data));

    write(pipe_to_ui[1], "Buffer tests completed.\n", 24);

    // ── 主循环：接收 UI 发来的数据并回显 ──
    fd_set read_fds;
    char buf[4096];
    int running = 1;

    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(pipe_from_ui[0], &read_fds);

        // 1 秒超时，定期检查子进程状态
        struct timeval tv = {1, 0};
        int ret = select(pipe_from_ui[0] + 1, &read_fds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // 检查子进程是否已退出
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            // 子进程已退出，结束循环
            break;
        }

        // 从 UI 读取数据
        if (FD_ISSET(pipe_from_ui[0], &read_fds)) {
            int n = (int)read(pipe_from_ui[0], buf, sizeof(buf) - 1);
            if (n <= 0) {
                // UI 关闭了管道
                break;
            }
            buf[n] = '\0';

            // 回显数据到 UI（证明 C 侧收到了）
            write(pipe_to_ui[1], "[C received] ", 13);
            write(pipe_to_ui[1], buf, (size_t)n);
        }
    }

    // 关闭管道
    close(pipe_to_ui[1]);
    close(pipe_from_ui[0]);

    // 等待子进程完全退出
    waitpid(pid, NULL, 0);

    return 0;
}