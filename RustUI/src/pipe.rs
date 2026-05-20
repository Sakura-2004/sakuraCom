/// 管道通信模块
/// 管理进程间通信的全局管道状态
///
/// 发送：其他模块通过 send_text() 直接向 C 端写入数据
/// 接收：子线程从管道读取数据，直接写入 OutputBox 的共享缓冲区
use std::os::unix::io::RawFd;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::thread;

/// 发送管道（UI -> Ccom）：向 Ccom 发送数据
static SEND_PIPE: OnceLock<Mutex<RawFd>> = OnceLock::new();

/// 接收管道（Ccom -> UI）：子线程从中读取数据
static READ_PIPE: OnceLock<Mutex<RawFd>> = OnceLock::new();

/// reader 线程是否已启动
static READER_STARTED: AtomicBool = AtomicBool::new(false);

/// 清空请求标志（clear 按钮设置，mod.rs ui() 检查并清空 own_lines）
static CLEAR_REQUESTED: AtomicBool = AtomicBool::new(false);

/// 当前输入文本（InputBox 变更时同步，发送按钮回调读取）
static INPUT_TEXT: OnceLock<Mutex<String>> = OnceLock::new();

/// 输出框的共享行缓冲区（clear 按钮回调时清空）
static OUTPUT_LINES: OnceLock<Arc<Mutex<Vec<String>>>> = OnceLock::new();

/// 初始化发送管道
pub fn init_send(fd: RawFd) {
    SEND_PIPE
        .set(Mutex::new(fd))
        .expect("SEND_PIPE already initialized");
}

/// 初始化管道接收：传入 OutputBox 的行缓冲区，reader 线程直接写入
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
                    eprintln!("pipe: READ_PIPE not initialized");
                    return;
                }
            };
            let mut buf = [0u8; 4096];
            loop {
                // 直接用 libc::read 操作原始 fd，而不通过 File::from_raw_fd，
                // 避免 File 在 drop 时关闭管道 fd 导致双重关闭或数据丢失
                let n = unsafe {
                    libc::read(read_fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len())
                };

                if n <= 0 {
                    // n == 0: 对端关闭（EOF），n < 0: 读取错误
                    if n < 0 {
                        let err = std::io::Error::last_os_error();
                        eprintln!("Pipe read error (fd={}): {}", read_fd, err);
                    }
                    break;
                }

                let n = n as usize;
                let text = match std::str::from_utf8(&buf[..n]) {
                    Ok(s) => s.to_string(),
                    Err(_) => {
                        // 非 UTF-8 数据，使用 HEX 格式
                        let hex: String = buf[..n]
                            .iter()
                            .map(|b| format!("{:02X} ", b))
                            .collect();
                        format!("[HEX] {}", hex.trim())
                    }
                };

                // 按行直接推入 OutputBox 的共享缓冲区
                if let Ok(mut lines) = output_lines.lock() {
                    for line in text.lines() {
                        lines.push(line.to_string());
                    }
                }
            }
        })
        .expect("Failed to spawn pipe reader thread");
}

/// 向发送管道写入数据（由按钮回调直接调用）
/// 注意：不再自动追加 '\n'，由调用方决定换行时机
pub fn send_text(text: &str) {
    if let Some(pipe) = SEND_PIPE.get() {
        if let Ok(fd) = pipe.lock() {
            let data = text.as_bytes();
            let write_fd = *fd;
            let _ = unsafe {
                libc::write(write_fd, data.as_ptr() as *const libc::c_void, data.len())
            };
        }
    }
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
/// 清空共享缓冲区，并设置清空请求标志，
/// mod.rs 的 ui() 渲染前会检查该标志并同步清空 OutputBox 的 own_lines
pub fn clear_output() {
    // 清空共享缓冲区
    if let Some(lines) = OUTPUT_LINES.get() {
        if let Ok(mut buf) = lines.lock() {
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
