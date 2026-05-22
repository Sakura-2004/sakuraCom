#ifndef H_SERIAL_MONITOR_H
#define H_SERIAL_MONITOR_H

/**
 * @brief 串口监控模块
 *        使用 libserialport 枚举系统串口，通过协议命令（0x02/0x03）
 *        通知 Rust UI 串口的插入和拔出事件。
 *
 * 工作流程：
 *   1. init      → 首次枚举全部串口，逐条发送 0x02 通知 UI
 *   2. poll      → 每 10s 重新枚举，对比变化，发送 0x02（新增）/0x03（移除）
 *   3. scan_only → 仅枚举并记录串口，不发送 0x02 通知（用于 UI 启动前预扫描）
 *   4. notify_init → 将 scan_only 记录的结果逐条以 0x02 发送给 UI
 *   5. cleanup   → 释放资源
 */

#include <stdint.h>

// ═══════════════════════════════════════════════════════════════
// 生命周期
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 初始化串口监控（首次枚举并通知 UI）
 * @return 0 成功，-1 失败
 */
int serial_monitor_init(void);

/**
 * @brief 仅扫描串口并记录到已知列表，不发送通知
 *        用于 UI 尚未启动时的预扫描
 * @return 0 成功，-1 失败
 */
int serial_monitor_scan_only(void);

/**
 * @brief 将已知串口列表作为 0x02 命令逐条发送给 UI
 *        在 UI 启动完成后调用，推送初始串口列表
 */
void serial_monitor_notify_init(void);

/**
 * @brief 定时轮询：重新枚举串口，对比上次结果并发送变更通知
 *        由主线程每 10 秒调用一次
 */
void serial_monitor_poll(void);

/**
 * @brief 清理串口监控资源
 */
void serial_monitor_cleanup(void);

// ═══════════════════════════════════════════════════════════════
// 串口连接/断开
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 打开串口连接
 * @param port_name     串口设备名（如 "/dev/ttyUSB0"）
 * @param baudrate_idx  波特率选项序号（0=9600, 1=19200, 2=38400, 3=57600,
 *                       4=115200, 5=230400, 6=460800, 7=921600）
 * @param databits_idx  数据位选项序号（0=8, 1=7, 2=6, 3=5）
 * @param parity_idx    校验位选项序号（0=无, 1=奇校验, 2=偶校验, 3=1校验, 4=0校验）
 * @param stopbits_idx  停止位选项序号（0=1, 1=1.5, 2=2）
 * @return 0 成功，-1 失败
 */
int serial_monitor_connect(
    const char *port_name,
    int baudrate_idx,
    int databits_idx,
    int parity_idx,
    int stopbits_idx
);

/**
 * @brief 关闭当前串口连接
 * @return 0 成功，-1 失败
 */
int serial_monitor_disconnect(void);

/**
 * @brief 获取当前串口连接的文件描述符
 * @return 文件描述符，未连接时返回 -1
 */
int serial_monitor_get_fd(void);

#endif