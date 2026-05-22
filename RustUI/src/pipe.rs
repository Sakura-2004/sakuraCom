/// 管道通信模块
/// 管理进程间通信的全局管道状态
///
/// 发送：其他模块通过 send_text() 封装协议帧后向 C 端写入数据
/// 接收：子线程从管道读取原始帧数据，交由协议层解析并分发到各缓冲区
use std::os::unix::io::RawFd;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::thread;

/// 发送管道（UI -> Ccom）：向 Ccom 发送数据
static SEND_PIPE: OnceLock<Mutex<RawFd>> = OnceLock::new();

/// 接收管道（Ccom -> UI）：子线程从中读取数据
static READ_PIPE: OnceLock<Mutex<RawFd>> = OnceLock::new();

/// reader 线程是否已启动
static READER_STARTED: AtomicBool = AtomicBool::new(false);

/// 已发送字节计数
static SENT_BYTES: AtomicU64 = AtomicU64::new(0);

/// 已接收字节计数
static RECEIVED_BYTES: AtomicU64 = AtomicU64::new(0);

/// 清空请求标志（clear 按钮设置，mod.rs ui() 检查并清空 own_lines）
static CLEAR_REQUESTED: AtomicBool = AtomicBool::new(false);

/// 当前输入文本（InputBox 变更时同步，发送按钮回调读取）
static INPUT_TEXT: OnceLock<Mutex<String>> = OnceLock::new();

/// 输出框的共享行缓冲区（clear 按钮回调时清空）
static OUTPUT_LINES: OnceLock<Arc<Mutex<Vec<String>>>> = OnceLock::new();

/// 原始输出字节缓冲区（上层封装：模式切换时用于重建行数据显示）
static RAW_OUTPUT_BUFFER: OnceLock<Arc<Mutex<Vec<u8>>>> = OnceLock::new();

/// 串口列表（由协议层 0x02/0x03 命令更新，dropdown 组件读取）
static PORT_LIST: OnceLock<Arc<Mutex<Vec<String>>>> = OnceLock::new();

/// 下拉框选中状态缓存（连接按钮回调读取）
static SELECTED_PORT_NAME: OnceLock<Mutex<String>> = OnceLock::new();
static BAUDRATE_IDX: AtomicU8 = AtomicU8::new(3);    // 默认 115200 (索引3)
static DATABITS_IDX: AtomicU8 = AtomicU8::new(0);     // 默认 8 (索引0)
static PARITY_IDX: AtomicU8 = AtomicU8::new(0);       // 默认 无 (索引0)
static STOPBITS_IDX: AtomicU8 = AtomicU8::new(0);     // 默认 1 (索引0)

/// 初始化原始输出字节缓冲区（上层封装，main.rs 调用）
pub fn init_raw_output_buffer() -> Arc<Mutex<Vec<u8>>> {
    let raw = Arc::new(Mutex::new(Vec::<u8>::new()));
    RAW_OUTPUT_BUFFER
        .set(raw.clone())
        .expect("RAW_OUTPUT_BUFFER already initialized");
    raw
}

/// 获取当前原始输出字节缓冲区的快照（上层封装，模式切换时调用）
pub fn get_raw_output_snapshot() -> Vec<u8> {
    if let Some(raw) = RAW_OUTPUT_BUFFER.get() {
        if let Ok(buf) = raw.lock() {
            return buf.clone();
        }
    }
    Vec::new()
}

/// 向发送管道写入原始字节数据（上层封装）
pub fn send_bytes(data: &[u8]) {
    if let Some(pipe) = SEND_PIPE.get() {
        if let Ok(fd) = pipe.lock() {
            let write_fd = *fd;
            let _ = unsafe {
                libc::write(write_fd, data.as_ptr() as *const libc::c_void, data.len())
            };
        }
    }
}

/// 初始化发送管道
pub fn init_send(fd: RawFd) {
    SEND_PIPE
        .set(Mutex::new(fd))
        .expect("SEND_PIPE already initialized");
}

/// 初始化串口列表（main.rs 调用）
pub fn init_port_list() -> Arc<Mutex<Vec<String>>> {
    let list = Arc::new(Mutex::new(Vec::<String>::new()));
    PORT_LIST
        .set(list.clone())
        .expect("PORT_LIST already initialized");
    list
}

/// 获取串口列表引用（dropdown 组件渲染时读取）
pub fn port_list() -> Option<Arc<Mutex<Vec<String>>>> {
    PORT_LIST.get().cloned()
}

/// 初始化管道接收：传入 OutputBox 的行缓冲区，reader 线程写入解析后的数据
pub fn init_receive(read_fd: RawFd, output_lines: Arc<Mutex<Vec<String>>>) {
    // 保存输出缓冲区引用，供 clear_output 使用
    OUTPUT_LINES
        .set(output_lines.clone())
        .expect("OUTPUT_LINES already initialized");

    READ_PIPE
        .set(Mutex::new(read_fd))
        .expect("READ_PIPE already initialized");

    // 启动 reader 子线程（仅一次）
    if READER_STARTED.swap(true, Ordering::Relaxed) {
        return;
    }

    thread::Builder::new()
        .name("pipe-reader".into())
        .spawn(move || {
            let read_fd = match READ_PIPE.get() {
                Some(mtx) => *mtx.lock().expect("lock read fd"),
                None => {
                    return;
                }
            };
            let mut buf = [0u8; 4096];

            // 接收帧缓冲区：累积原始字节，按 [0x57][0x8a] 拆包
            let mut frame_buf: Vec<u8> = Vec::with_capacity(65536);

            loop {
                // 直接用 libc::read 操作原始 fd，而不通过 File::from_raw_fd，
                // 避免 File 在 drop 时关闭管道 fd 导致双重关闭或数据丢失
                let n = unsafe {
                    libc::read(read_fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len())
                };

                if n <= 0 {
                    // n == 0: 对端关闭（EOF），n < 0: 读取错误，静默退出
                    break;
                }

                let n = n as usize;

                // 累积到帧缓冲区
                frame_buf.extend_from_slice(&buf[..n]);

                // 按 [0x57][0x8a] 拆出完整帧
                // 魔术数字 0x57 0x8a 不会与 payload 数据冲突，不需要额外 lookahead 检查
                let raw_buf_ref = RAW_OUTPUT_BUFFER.get();
                loop {
                    let mut term_pos = None;
                    for i in 0..frame_buf.len().saturating_sub(1) {
                        if frame_buf[i] == 0x57 && frame_buf[i + 1] == 0x8a {
                            term_pos = Some(i);
                            break;
                        }
                    }

                    let term_pos = match term_pos {
                        Some(p) => p,
                        None => break,
                    };

                    // 取出完整帧（不含 [0x57][0x8a] 结束标记）
                    if term_pos > 0 {
                        let frame = &frame_buf[..term_pos];
                        if let Some(port_list) = PORT_LIST.get() {
                            crate::protocol::receive_and_dispatch_v2(
                                frame,
                                &output_lines,
                                raw_buf_ref,
                                port_list,
                            );
                        } else {
                            // port_list 未初始化时，回退到消息处理
                            crate::protocol::receive_and_output(
                                frame,
                                &output_lines,
                                raw_buf_ref,
                            );
                        }
                    }

                    // 移除已处理的帧 + 结束标记
                    let consumed = term_pos + 2;
                    frame_buf.drain(..consumed);
                }

                // 缓冲区溢出保护
                if frame_buf.len() > 65535 {
                    frame_buf.clear();
                }
            }
        })
        .expect("Failed to spawn pipe reader thread");
}

/// 向发送管道写入数据（由按钮回调直接调用）
/// 通过协议层封装为 0x01 帧后发送
pub fn send_text(text: &str) {
    crate::protocol::send_msg_str(text);
}

/// 初始化输入文本缓冲区（InputBox 通过此接口同步当前内容）
pub fn init_input_buffer() {
    INPUT_TEXT
        .set(Mutex::new(String::new()))
        .expect("INPUT_TEXT already initialized");
}

/// 更新输入文本（InputBox 的 on_input 回调调用）
pub fn set_input_text(text: &str) {
    if let Some(buf) = INPUT_TEXT.get() {
        if let Ok(mut s) = buf.lock() {
            *s = text.to_string();
        }
    }
}

/// 读取当前输入文本（发送按钮回调调用）
pub fn get_input_text() -> String {
    if let Some(buf) = INPUT_TEXT.get() {
        if let Ok(s) = buf.lock() {
            return s.clone();
        }
    }
    String::new()
}

/// 清空输出缓冲区（清空按钮回调直接调用）
/// 清空共享行缓冲区和原始字节缓冲区，并设置清空请求标志，
/// mod.rs 的 ui() 渲染前会检查该标志并同步清空 OutputBox 的 own_lines
pub fn clear_output() {
    // 清空共享行缓冲区
    if let Some(lines) = OUTPUT_LINES.get() {
        if let Ok(mut buf) = lines.lock() {
            buf.clear();
        }
    }
    // 清空原始字节缓冲区
    if let Some(raw) = RAW_OUTPUT_BUFFER.get() {
        if let Ok(mut buf) = raw.lock() {
            buf.clear();
        }
    }
    // 设置清空请求标志，ui() 会在渲染前检查并清空 own_lines
    CLEAR_REQUESTED.store(true, Ordering::Relaxed);
}

/// 检查并消费清空请求（由 mod.rs ui() 调用）
pub fn take_clear_request() -> bool {
    CLEAR_REQUESTED.swap(false, Ordering::Relaxed)
}

/// 获取发送管道 fd 的原始值（主要用于关闭）
pub fn send_fd() -> Option<RawFd> {
    SEND_PIPE.get().and_then(|m| m.lock().ok()).map(|fd| *fd)
}

/// 获取已发送字节数
pub fn sent_bytes() -> u64 {
    SENT_BYTES.load(Ordering::Relaxed)
}

/// 获取已接收字节数
pub fn received_bytes() -> u64 {
    RECEIVED_BYTES.load(Ordering::Relaxed)
}

/// 累加已发送字节数（由协议层在 0x01 命令发送时调用，只计 payload 字节数）
pub fn add_sent_bytes(count: u64) {
    SENT_BYTES.fetch_add(count, Ordering::Relaxed);
}

/// 累加已接收字节数（由协议层在 0x01 命令接收时调用，只计 payload 字节数）
pub fn add_received_bytes(count: u64) {
    RECEIVED_BYTES.fetch_add(count, Ordering::Relaxed);
}

/// 重置统计数据
pub fn reset_stats() {
    SENT_BYTES.store(0, Ordering::Relaxed);
    RECEIVED_BYTES.store(0, Ordering::Relaxed);
}

/// 初始化选中端口名缓存
pub fn init_selected_port_name() {
    SELECTED_PORT_NAME
        .set(Mutex::new(String::new()))
        .expect("SELECTED_PORT_NAME already initialized");
}

/// 设置选中的端口名（由 port dropdown 的 on_select 回调调用）
pub fn set_selected_port_name(name: &str) {
    if let Some(b) = SELECTED_PORT_NAME.get() {
        if let Ok(mut s) = b.lock() {
            *s = name.to_string();
        }
    }
}

/// 获取选中的端口名（连接按钮回调读取）
pub fn get_selected_port_name() -> String {
    if let Some(b) = SELECTED_PORT_NAME.get() {
        if let Ok(s) = b.lock() {
            return s.clone();
        }
    }
    String::new()
}

/// 设置波特率索引
pub fn set_baudrate_idx(idx: u8) {
    BAUDRATE_IDX.store(idx, Ordering::Relaxed);
}

/// 获取波特率索引
pub fn get_baudrate_idx() -> u8 {
    BAUDRATE_IDX.load(Ordering::Relaxed)
}

/// 设置数据位索引
pub fn set_databits_idx(idx: u8) {
    DATABITS_IDX.store(idx, Ordering::Relaxed);
}

/// 获取数据位索引
pub fn get_databits_idx() -> u8 {
    DATABITS_IDX.load(Ordering::Relaxed)
}

/// 设置校验位索引
pub fn set_parity_idx(idx: u8) {
    PARITY_IDX.store(idx, Ordering::Relaxed);
}

/// 获取校验位索引
pub fn get_parity_idx() -> u8 {
    PARITY_IDX.load(Ordering::Relaxed)
}

/// 设置停止位索引
pub fn set_stopbits_idx(idx: u8) {
    STOPBITS_IDX.store(idx, Ordering::Relaxed);
}

/// 获取停止位索引
pub fn get_stopbits_idx() -> u8 {
    STOPBITS_IDX.load(Ordering::Relaxed)
}
