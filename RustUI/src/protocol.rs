/// 协议层模块
///
/// 与 C 端协议层对齐，封装命令帧的构建、发送、接收解析。
///
/// 命令定义（与 C 端一致）：
///   CMD_MSG(0x01)         - 消息收发，数据部分为文本/二进制
///   CMD_PORT_ADD(0x02)    - 串口新增通知，后跟串口名字符串
///   CMD_PORT_REMOVE(0x03) - 串口断开通知，后跟串口名字符串
///
/// 帧格式：[1字节命令码][变长数据载荷][0x57][0x8a]
use std::sync::{Arc, Mutex};

/// 命令码常量（与 C 端 protocol.h 对齐）
const CMD_MSG: u8 = 0x01;
const CMD_PORT_ADD: u8 = 0x02;
const CMD_PORT_REMOVE: u8 = 0x03;
const CMD_CONNECT: u8 = 0x04;
const CMD_CONNECT_RESP: u8 = 0x05;

/// 0x05 响应状态码（与 C 端对齐）
pub const CONNECT_STATUS_SUCCESS: u8 = 0x00;
pub const CONNECT_STATUS_FAIL: u8 = 0x01;
pub const CONNECT_STATUS_DISCONNECT_OK: u8 = 0xf0;
pub const CONNECT_STATUS_DISCONNECT_FAIL: u8 = 0xf1;

/// 0x04 请求标记
pub const CONNECT_REQUEST_DISCONNECT: u8 = 0xff;

/// 帧结束标记（与 C 端对齐，魔术数字 0x57 0x8a）
const FRAME_TERM: &[u8] = &[0x57, 0x8a];

/// 0x05 响应回调类型
pub type ConnectResponseCallback = Box<dyn Fn(u8) + Send + Sync + 'static>;

/// 0x05 响应回调（主线程注册，接收线程调用时设置标志）
static CONNECT_RESPONSE_CALLBACK: std::sync::Mutex<Option<ConnectResponseCallback>> =
    std::sync::Mutex::new(None);

// ═══════════════════════════════════════════════════════════════
// 发送接口（自动追加 [0x57][0x8a] 结束标记）
// ═══════════════════════════════════════════════════════════════

/// 发送消息（0x01）
pub fn send_msg(data: &[u8]) {
    // 只统计 0x01 命令的 payload 数据字节数
    super::pipe::add_sent_bytes(data.len() as u64);
    send_frame(CMD_MSG, data);
}

/// 发送消息文本（0x01）—— 便利函数
pub fn send_msg_str(text: &str) {
    send_msg(text.as_bytes());
}

/// 构建并发送完整帧：cmd + data + [0x57][0x8a]
fn send_frame(cmd: u8, data: &[u8]) {
    let mut frame = Vec::with_capacity(data.len() + 3);
    frame.push(cmd);
    frame.extend_from_slice(data);
    frame.extend_from_slice(FRAME_TERM);
    super::pipe::send_bytes(&frame);
}

// ═══════════════════════════════════════════════════════════════
// 接收接口（pipe 接收线程回调中调用）
// ═══════════════════════════════════════════════════════════════

/// 处理从 C 端接收到的原始数据（port_list 未初始化时的回退路径）
/// 仅处理 0x01 消息命令，其他命令被忽略
pub fn receive_and_output(
    data: &[u8],
    output_lines: &Arc<Mutex<Vec<String>>>,
    raw_buf: Option<&Arc<Mutex<Vec<u8>>>>,
) {
    if data.is_empty() {
        return;
    }

    let cmd = data[0];
    let payload = &data[1..];

    match cmd {
        CMD_MSG => {
            super::pipe::add_received_bytes(payload.len() as u64);
            handle_msg(payload, output_lines, raw_buf);
        }
        _ => {
            // 向后兼容：未知/非MSG命令，按原始数据当文本处理
            handle_msg(data, output_lines, raw_buf);
        }
    }
}

/// 处理从 C 端接收到的原始帧数据
///
/// - 0x01：将数据载荷转为文本输出到 output_lines，同时追加到 raw_buf
/// - 0x02：提取串口名，添加到 port_list
/// - 0x03：提取串口名，从 port_list 移除
pub fn receive_and_dispatch(
    data: &[u8],
    output_lines: &Arc<Mutex<Vec<String>>>,
    raw_buf: Option<&Arc<Mutex<Vec<u8>>>>,
    port_list: &Arc<Mutex<Vec<String>>>,
) {
    if data.is_empty() {
        return;
    }

    let cmd = data[0];
    let payload = &data[1..];

    match cmd {
        CMD_MSG => {
            super::pipe::add_received_bytes(payload.len() as u64);
            handle_msg(payload, output_lines, raw_buf);
        }
        CMD_PORT_ADD => {
            handle_port_add(payload, port_list);
        }
        CMD_PORT_REMOVE => {
            handle_port_remove(payload, port_list);
        }
        _ => {
            // 未知命令，按 0x01 处理（向后兼容）
            handle_msg(data, output_lines, raw_buf);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 内部处理函数
// ═══════════════════════════════════════════════════════════════

fn handle_msg(
    payload: &[u8],
    output_lines: &Arc<Mutex<Vec<String>>>,
    raw_buf: Option<&Arc<Mutex<Vec<u8>>>>,
) {
    // UTF-8 文本解析
    let text = match std::str::from_utf8(payload) {
        Ok(s) => s.to_string(),
        Err(_) => {
            let hex: String = payload.iter().map(|b| format!("{:02X} ", b)).collect();
            format!("[HEX] {}", hex.trim())
        }
    };

    // 写入行缓冲区
    if let Ok(mut lines) = output_lines.lock() {
        for line in text.lines() {
            lines.push(line.to_string());
        }
    }

    // 追加原始字节缓冲
    if let Some(raw) = raw_buf {
        if let Ok(mut buf) = raw.lock() {
            buf.extend_from_slice(payload);
        }
    }
}

fn handle_port_add(payload: &[u8], port_list: &Arc<Mutex<Vec<String>>>) {
    let name = extract_cstring(payload);
    if name.is_empty() {
        return;
    }

    if let Ok(mut ports) = port_list.lock() {
        if !ports.contains(&name) {
            ports.push(name.clone());
        }
    }
}

fn handle_port_remove(payload: &[u8], port_list: &Arc<Mutex<Vec<String>>>) {
    let name = extract_cstring(payload);
    if name.is_empty() {
        return;
    }

    if let Ok(mut ports) = port_list.lock() {
        ports.retain(|p| p != &name);
    }
}

/// 从 C 端 payload 提取 null-terminated 字符串
fn extract_cstring(data: &[u8]) -> String {
    let end = data.iter().position(|&b| b == 0).unwrap_or(data.len());
    String::from_utf8_lossy(&data[..end]).into_owned()
}

// ═══════════════════════════════════════════════════════════════
// 0x04 连接请求发送接口
// ═══════════════════════════════════════════════════════════════

/// 发送连接请求（0x04）
/// payload 格式：[port_name_len(1byte)][port_name...][baudrate_idx][databits_idx][parity_idx][stopbits_idx]
pub fn send_connect_request(
    port_name: &str,
    baudrate_idx: u8,
    databits_idx: u8,
    parity_idx: u8,
    stopbits_idx: u8,
) {
    let name_bytes = port_name.as_bytes();
    let name_len = name_bytes.len().min(255) as u8;
    let mut payload = Vec::with_capacity(1 + name_len as usize + 4);
    payload.push(name_len);
    payload.extend_from_slice(&name_bytes[..name_len as usize]);
    payload.push(baudrate_idx);
    payload.push(databits_idx);
    payload.push(parity_idx);
    payload.push(stopbits_idx);
    send_frame(CMD_CONNECT, &payload);
}

/// 发送断开连接请求（0x04，payload 首字节为 0xff）
pub fn send_disconnect_request() {
    send_frame(CMD_CONNECT, &[CONNECT_REQUEST_DISCONNECT]);
}

// ═══════════════════════════════════════════════════════════════
// 0x05 响应处理
// ═══════════════════════════════════════════════════════════════

/// 注册 0x05 响应回调（由主线程调用）
pub fn set_connect_response_callback(cb: ConnectResponseCallback) {
    if let Ok(mut guard) = CONNECT_RESPONSE_CALLBACK.lock() {
        *guard = Some(cb);
    }
}

/// 处理 0x05 响应（由 receive_and_dispatch 调用）
/// payload 格式：[status_code]
pub fn handle_connect_response(payload: &[u8]) {
    if payload.is_empty() {
        return;
    }
    let status = payload[0];
    if let Ok(mut guard) = CONNECT_RESPONSE_CALLBACK.lock() {
        if let Some(ref cb) = *guard {
            cb(status);
        }
    }
}

/// 更新 receive_and_dispatch 以处理 0x05
pub fn receive_and_dispatch_v2(
    data: &[u8],
    output_lines: &Arc<Mutex<Vec<String>>>,
    raw_buf: Option<&Arc<Mutex<Vec<u8>>>>,
    port_list: &Arc<Mutex<Vec<String>>>,
) {
    if data.is_empty() {
        return;
    }

    let cmd = data[0];
    let payload = &data[1..];

    match cmd {
        CMD_MSG => {
            super::pipe::add_received_bytes(payload.len() as u64);
            handle_msg(payload, output_lines, raw_buf);
        }
        CMD_PORT_ADD => {
            handle_port_add(payload, port_list);
        }
        CMD_PORT_REMOVE => {
            handle_port_remove(payload, port_list);
        }
        CMD_CONNECT_RESP => {
            handle_connect_response(payload);
        }
        _ => {
            // 未知命令，按 0x01 处理（向后兼容）
            handle_msg(data, output_lines, raw_buf);
        }
    }
}
