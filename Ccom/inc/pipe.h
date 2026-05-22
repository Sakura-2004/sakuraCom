#ifndef H_PIPE_H
#define H_PIPE_H

/**
 *  @brief 管道通信组件
 *  封装管道文件描述符的管理，对外只暴露读写接口。
 *  支持两种模式：
 *    - 正常模式（mode=0）：匿名管道 fork+exec
 *    - extra 模式（mode=1）：命名管道 FIFO
 */

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

// ═══════════════════════════════════════════════════════════════
// 模式管理
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 设置管道运行模式
 * @param mode 0 = 正常模式，1 = extra 模式
 */
void pipe_set_mode(int mode);

/**
 * @brief 获取当前管道运行模式
 * @return 0 = 正常模式，1 = extra 模式
 */
int pipe_get_mode(void);

// ═══════════════════════════════════════════════════════════════
// fd 管理
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 设置写端文件描述符（C → UI）
 * @param fd 写端 fd
 */
void pipe_set_write_fd(int fd);

/**
 * @brief 设置读端文件描述符（UI → C）
 * @param fd 读端 fd
 */
void pipe_set_read_fd(int fd);

/**
 * @brief 获取写端文件描述符
 * @return 写端 fd，未设置时返回 -1
 */
int pipe_get_write_fd(void);

/**
 * @brief 获取读端文件描述符
 * @return 读端 fd，未设置时返回 -1
 */
int pipe_get_read_fd(void);

// ═══════════════════════════════════════════════════════════════
// 核心读写接口
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 向 UI 管道写入数据
 * @param buf 数据缓冲区
 * @param len 写入字节数
 * @return 实际写入字节数，失败返回 -1
 */
ssize_t pipe_write(const void *buf, size_t len);

/**
 * @brief 从 UI 管道读取数据
 * @param buf 数据缓冲区
 * @param len 缓冲区大小（字节）
 * @return 实际读取字节数，失败返回 -1
 */
ssize_t pipe_read(void *buf, size_t len);

// ═══════════════════════════════════════════════════════════════
// extra 模式专用：FIFO 路径管理
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 保存 FIFO 路径（extra 模式专用，用于清理时 unlink）
 * @param to_ui_path C→UI 的 FIFO 路径
 * @param from_ui_path UI→C 的 FIFO 路径
 */
void pipe_set_fifo_paths(const char *to_ui_path, const char *from_ui_path);

/**
 * @brief 清理管道资源
 *        关闭所有 fd，extra 模式下删除 FIFO 文件
 */
void pipe_cleanup(void);

// ═══════════════════════════════════════════════════════════════
// 接收线程管理
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 接收数据回调函数类型
 *        子线程从管道读取到数据后调用此回调
 * @param data 接收到的数据缓冲区
 * @param len  数据长度
 */
typedef void (*pipe_recv_callback_t)(const char *data, size_t len);

/**
 * @brief 启动接收子线程
 *        子线程内部循环调用 pipe_read() 读取数据，
 *        读取到数据后调用 callback 进行处理
 * @param callback 数据接收回调（NULL 则丢弃数据）
 * @return 0 成功，-1 失败
 */
int pipe_start_receiver(pipe_recv_callback_t callback);

/**
 * @brief 停止接收子线程并等待其退出
 */
void pipe_stop_receiver(void);

/**
 * @brief 检查接收子线程是否仍在运行
 * @return 1 运行中，0 已停止
 */
int pipe_is_receiver_running(void);

#endif
