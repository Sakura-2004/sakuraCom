use ratatui::style::{Color, Modifier, Style};
use std::os::unix::io::RawFd;
use std::sync::atomic::Ordering;

mod pipe;
mod protocol;
mod ui;

fn main() -> std::io::Result<()> {
    // 忽略 SIGPIPE：当写入一个对端已关闭的管道时，
    // 不触发进程崩溃，而是让 libc::write 返回 EPIPE 错误码，
    // 由 pipe::send_text 静默处理。
    unsafe {
        libc::signal(libc::SIGPIPE, libc::SIG_IGN);
    }

    // ── 解析命令行参数 ──
    let args: Vec<String> = std::env::args().collect();
    let mut read_fd: RawFd = 0;
    let mut write_fd: RawFd = 0;
    if args.len() >= 3 && !args[1].starts_with("--") {
        read_fd = args[1].parse().expect("Invalid read_fd");
        write_fd = args[2].parse().expect("Invalid write_fd");
    } else {
        let mut i = 1;
        let mut got_read = false;
        let mut got_write = false;
        while i < args.len() {
            match args[i].as_str() {
                "--read-fd" => {
                    i += 1;
                    read_fd = args[i].parse().expect("Invalid --read-fd value");
                    got_read = true;
                }
                "--write-fd" => {
                    i += 1;
                    write_fd = args[i].parse().expect("Invalid --write-fd value");
                    got_write = true;
                }
                _ => {
                    std::process::exit(1);
                }
            }
            i += 1;
        }
        if !got_read || !got_write {
            std::process::exit(1);
        }
    }

    // ── 创建调度器 ──
    let mut scheduler = ui::Scheduler::new();

    // ═══════════════════════════════════════════════════════════
    // 注册顺序（与 mod.rs ui() 布局中的索引一致）：
    //   0: Dropdown (串口选择)       → chunks[1]
    //   1: Dropdown (波特率)         → chunks[2] 左1
    //   2: Dropdown (数据位)         → chunks[2] 左2
    //   3: Dropdown (校验位)         → chunks[2] 右2
    //   4: Dropdown (停止位)         → chunks[2] 右1
    //   5: Button   (打开串口)       → chunks[3] 左
    //   6: OutputBox (统计)          → chunks[3] 中
    //   7: RadioBox (输入模式)       → chunks[4] 右
    //   8: InputBox (输入栏)         → chunks[4] 左
    //   9: OutputBox (输出栏，共享缓冲区) → chunks[5] 左
    //  10: RadioBox (输出模式)       → chunks[5] 右
    //  11: Button   (发送)           → chunks[6] 左
    //  12: Button   (清空)           → chunks[6] 中
    //  13: Button   (退出)           → chunks[6] 右
    // ═══════════════════════════════════════════════════════════

    // ── 初始化串口列表（由协议层 0x02/0x03 命令更新）──
    let port_list = pipe::init_port_list();

    // ── 初始化全局缓存（dropdown on_select 回调写入，连接按钮读取）──
    pipe::init_selected_port_name();

    // ── [0] 串口选择 ──
    let mut port_dropdown = ui::Dropdown::new_with_shared(
        port_list.clone(),
        "串口选择",
    );
    port_dropdown.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    port_dropdown.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    port_dropdown.on_select(|_idx, text| {
        pipe::set_selected_port_name(text);
    });
    scheduler.register(Box::new(port_dropdown));

    // ── [1] 波特率选择 ──
    let mut baudrate = ui::Dropdown::new(
        vec![
            "9600", "19200", "38400","57600", "115200", "230400", "460800","921600"
        ],
        "波特率",
    );
    baudrate.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    baudrate.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    baudrate.on_select(|idx, _text| {
        pipe::set_baudrate_idx(idx as u8);
    });
    scheduler.register(Box::new(baudrate));

    // ── [2] 数据位选择 ──
    let mut data_bits = ui::Dropdown::new(vec!["8", "7", "6", "5"], "数据位");
    data_bits.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    data_bits.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    data_bits.on_select(|idx, _text| {
        pipe::set_databits_idx(idx as u8);
    });
    scheduler.register(Box::new(data_bits));

    // ── [3] 校验位选择 ──
    let mut parity = ui::Dropdown::new(
        vec!["无", "奇校验", "偶校验", "1校验", "0校验"],
        "校验位",
    );
    parity.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    parity.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    parity.on_select(|idx, _text| {
        pipe::set_parity_idx(idx as u8);
    });
    scheduler.register(Box::new(parity));

    // ── [4] 停止位选择 ──
    let mut stop_bits = ui::Dropdown::new(vec!["1", "1.5", "2"], "停止位");
    stop_bits.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    stop_bits.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    stop_bits.on_select(|idx, _text| {
        pipe::set_stopbits_idx(idx as u8);
    });
    scheduler.register(Box::new(stop_bits));

    // ── [5] 打开/断开串口按钮 ──
    let mut open_btn = ui::Button::new("打开串口");
    open_btn.set_style(Style::default().fg(Color::White).bg(Color::Green));
    open_btn.set_focus_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::LightGreen)
            .add_modifier(Modifier::BOLD),
    );
    open_btn.set_on_click(|| {
        let connected = ui::CONNECTION_STATUS.load(Ordering::Relaxed) != 0;
        if connected {
            // 发送断开请求
            protocol::send_disconnect_request();
        } else {
            // 获取下拉框选中的串口名和参数索引
            // 需要从 scheduler 中读取各个 dropdown 的选中项
            // 但 set_on_click 闭包无法访问 scheduler（借用冲突）
            // 解决方案：使用全局原子变量缓存下拉框选中索引
            // 这些索引由 dropdown 的 on_select 回调写入
            let port_name = pipe::get_selected_port_name();
            if port_name.is_empty() {
                // 无可用串口，不发送请求
                return;
            }
            let baudrate_idx = pipe::get_baudrate_idx();
            let databits_idx = pipe::get_databits_idx();
            let parity_idx   = pipe::get_parity_idx();
            let stopbits_idx = pipe::get_stopbits_idx();
            protocol::send_connect_request(
                &port_name,
                baudrate_idx,
                databits_idx,
                parity_idx,
                stopbits_idx,
            );
        }
    });
    scheduler.register(Box::new(open_btn));

    // ── 注册 0x05 响应回调 ──
    protocol::set_connect_response_callback(Box::new(|status| {
        match status {
            protocol::CONNECT_STATUS_SUCCESS => {
                ui::CONNECTION_STATUS.store(1, Ordering::Relaxed);
            }
            protocol::CONNECT_STATUS_FAIL => {
                ui::CONNECTION_STATUS.store(0, Ordering::Relaxed);
            }
            protocol::CONNECT_STATUS_DISCONNECT_OK => {
                ui::CONNECTION_STATUS.store(0, Ordering::Relaxed);
            }
            _ => {
                // 0xf0 等异常断开码也回退到未连接
                if status >= 0xf0 {
                    ui::CONNECTION_STATUS.store(0, Ordering::Relaxed);
                }
            }
        }
    }));

    // ── [6] 统计输出栏 ──
    let mut stats_output = ui::OutputBox::new("统计");
    stats_output.append_line("发送: 0 bytes  |  接收: 0 bytes");
    stats_output.set_style(Style::default().fg(Color::White).bg(Color::Black));
    stats_output.set_focus_style(
        Style::default()
            .fg(Color::White)
            .bg(Color::Rgb(10, 20, 10))
            .add_modifier(Modifier::BOLD),
    );
    scheduler.register(Box::new(stats_output));

    // ── [7] 输入模式选择 (RadioBox: ASCII / HEX) ──
    let mut input_mode = ui::RadioBox::new(vec!["ASCII", "HEX"]);
    input_mode.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    input_mode.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    input_mode.on_select(|index, _text| {
        ui::INPUT_MODE.store(index as u8, std::sync::atomic::Ordering::Relaxed);
    });
    scheduler.register(Box::new(input_mode));

    // ── [8] 输入栏 ──
    // 初始化共享输入文本缓冲区，然后通过 on_input 回调同步内容
    pipe::init_input_buffer();
    let mut input_box = ui::InputBox::new("输入数据");
    input_box.set_style(Style::default().fg(Color::White).bg(Color::Black));
    input_box.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::Rgb(20, 20, 30))
            .add_modifier(Modifier::BOLD),
    );
    input_box.on_input(|text| {
        pipe::set_input_text(text);
    });
    scheduler.register(Box::new(input_box));

    // ── [9] 输出栏（使用共享缓冲区，pipe reader 线程直接写入）──
    let (mut output_box, output_lines) = ui::OutputBox::new_shared("输出数据");
    output_box.set_style(Style::default().fg(Color::White).bg(Color::Black));
    output_box.set_focus_style(
        Style::default()
            .fg(Color::White)
            .bg(Color::Rgb(10, 20, 10))
            .add_modifier(Modifier::BOLD),
    );
    scheduler.register(Box::new(output_box));

    // ── [10] 输出模式选择 (RadioBox: ASCII / HEX) ──
    let mut output_mode = ui::RadioBox::with_title("输出模式", vec!["ASCII", "HEX"]);
    output_mode.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    output_mode.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    output_mode.on_select(|index, _text| {
        let mode = if index == 0 { 0 } else { 1 };
        ui::OUTPUT_MODE.store(mode, std::sync::atomic::Ordering::Relaxed);
    });
    scheduler.register(Box::new(output_mode));

    // ── [11] 发送按钮 ──
    let mut send_btn = ui::Button::new("发送");
    send_btn.set_style(Style::default().fg(Color::White).bg(Color::Blue));
    send_btn.set_focus_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::LightBlue)
            .add_modifier(Modifier::BOLD),
    );
    send_btn.set_on_click(|| {
        let text = pipe::get_input_text();
        if text.is_empty() {
            return;
        }
        let mode = ui::INPUT_MODE.load(std::sync::atomic::Ordering::Relaxed);
        let bytes = if mode == 1 {
            ui::hex_str_to_bytes(&text)
        } else {
            text.as_bytes().to_vec()
        };
        if !bytes.is_empty() {
            protocol::send_msg(&bytes);
        }
    });
    scheduler.register(Box::new(send_btn));

    // ── [12] 清空按钮 ──
    let mut clear_btn = ui::Button::new("清空");
    clear_btn.set_style(Style::default().fg(Color::White).bg(Color::Red));
    clear_btn.set_focus_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::LightRed)
            .add_modifier(Modifier::BOLD),
    );
    clear_btn.set_on_click(|| {
        pipe::clear_output();
    });
    scheduler.register(Box::new(clear_btn));

    // ── [13] 退出按钮 ──
    let mut quit_btn = ui::Button::new("退出");
    quit_btn.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    quit_btn.set_focus_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Gray)
            .add_modifier(Modifier::BOLD),
    );
    quit_btn.set_on_click(|| {
        ui::request_quit();
    });
    scheduler.register(Box::new(quit_btn));

    // ── 初始化管道通信 ──
    // 必须在注册 OutputBox 之后调用，才能把共享缓冲区传给 reader 线程
    // 同时初始化原始字节缓冲区（用于模式切换重建显示）
    pipe::init_raw_output_buffer();
    pipe::init_send(write_fd);
    pipe::init_receive(read_fd, output_lines);

    // ── 准备 UI ──
    let mut terminal = ui::setup()?;

    // ── UI 主循环 ──
    let res = ui::run_app(&mut terminal, &mut scheduler);

    // ── 结束 UI ──
    ui::restore(&mut terminal)?;

    std::thread::sleep(std::time::Duration::from_secs(1));

    // 静默处理 UI 运行错误（错误信息通过 TUI 界面展示）
    let _ = res;

    Ok(())
}