#ifndef H_MAIN_H
#define H_MAIN_H

// 引用头文件
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <libserialport.h>

#include "error.h"
#include "buffer.h"
#include "pipe.h"

// ═══════════════════════════════════════════════════════════════
// 串口连接状态
// ═══════════════════════════════════════════════════════════════

/// C 端连接状态枚举
typedef enum {
    CONN_STATE_IDLE,        // 等待连接（未连接）
    CONN_STATE_CONNECTED    // 已连接
} conn_state_t;

/// 全局连接状态
extern volatile conn_state_t g_conn_state;

#endif