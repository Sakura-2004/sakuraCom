use ratatui::style::{Color, Modifier, Style};
use std::os::unix::io::RawFd;

mod pipe;
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
                    eprintln!("Unknown argument: {}", args[i]);
                    std::process::exit(1);
                }
            }
            i += 1;
        }
        if !got_read || !got_write {
            eprintln!("Usage: UI --read-fd <fd> --write-fd <fd>");
            std::process::exit(1);
        }
    }

    // ── 创建调度器 ──
    let mut scheduler = ui::Scheduler::new();

    // ═══════════════════════════════════════════════════════════
    // 注册顺序（与 mod.rs ui() 布局中的索引一致）：
    //   0: Dropdown (串口选择)       → chunks[1]
    //   1: Dropdown (波特率)         → chunks[2] 左
    //   2: Dropdown (校验位)         → chunks[2] 中
    //   3: Dropdown (停止位)         → chunks[2] 右
    //   4: Button   (打开串口)       → chunks[3] 左
    //   5: OutputBox (统计)          → chunks[3] 中
    //   6: RadioBox (输入模式)       → chunks[4] 右
    //   7: InputBox (输入栏)         → chunks[4] 左
    //   8: OutputBox (输出栏，共享缓冲区) → chunks[5] 左
    //   9: RadioBox (输出模式)       → chunks[5] 右
    //  10: Button   (发送)           → chunks[6] 左
    //  11: Button   (清空)           → chunks[6] 中
    //  12: Button   (退出)           → chunks[6] 右
    // ═══════════════════════════════════════════════════════════

    // ── [0] 串口选择 ──
    let mut port_dropdown = ui::Dropdown::new(
        vec![
            "/dev/ttyUSB0",
            "/dev/ttyUSB1",
            "/dev/ttyACM0",
            "/dev/cu.usbmodem",
        ],
        "串口选择",
    );
    port_dropdown.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    port_dropdown.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    scheduler.register(Box::new(port_dropdown));

    // ── [1] 波特率选择 ──
    let mut baudrate = ui::Dropdown::new(
        vec![
            "600", "1200", "2400", "4800", "9600", "14400", "19200", "28800", "38400",
            "57600", "115200", "230400", "460800",
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
    scheduler.register(Box::new(baudrate));

    // ── [2] 校验位选择 ──
    let mut parity = ui::Dropdown::new(
        vec!["无", "奇校验", "偶校验", "标记", "空格"],
        "校验位",
    );
    parity.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    parity.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    scheduler.register(Box::new(parity));

    // ── [3] 停止位选择 ──
    let mut stop_bits = ui::Dropdown::new(vec!["1", "1.5", "2"], "停止位");
    stop_bits.set_style(Style::default().fg(Color::White).bg(Color::DarkGray));
    stop_bits.set_focus_style(
        Style::default()
            .fg(Color::Yellow)
            .bg(Color::DarkGray)
            .add_modifier(Modifier::BOLD),
    );
    scheduler.register(Box::new(stop_bits));

    // ── [4] 打开串口按钮 ──
    let mut open_btn = ui::Button::new("打开串口");
    open_btn.set_style(Style::default().fg(Color::White).bg(Color::Green));
    open_btn.set_focus_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::LightGreen)
            .add_modifier(Modifier::BOLD),
    );
    open_btn.set_on_click(|| {
        let current = ui::CONNECTION_STATUS.load(std::sync::atomic::Ordering::Relaxed);
        ui::CONNECTION_STATUS.store(current ^ 1, std::sync::atomic::Ordering::Relaxed);
    });
    scheduler.register(Box::new(open_btn));

    // ── [5] 统计输出栏 ──
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

    // ── [6] 输入模式选择 (RadioBox: ASCII / HEX) ──
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

    // ── [7] 输入栏 ──
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

    // ── [8] 输出栏（使用共享缓冲区，pipe reader 线程直接写入）──
    let (mut output_box, output_lines) = ui::OutputBox::new_shared("输出数据");
    output_box.set_style(Style::default().fg(Color::White).bg(Color::Black));
    output_box.set_focus_style(
        Style::default()
            .fg(Color::White)
            .bg(Color::Rgb(10, 20, 10))
            .add_modifier(Modifier::BOLD),
    );
    scheduler.register(Box::new(output_box));

    // ── [9] 输出模式选择 (RadioBox: ASCII / HEX) ──
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

    // ── [10] 发送按钮 ──
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
        if !text.is_empty() {
            pipe::send_text(&text);
        }
    });
    scheduler.register(Box::new(send_btn));

    // ── [11] 清空按钮 ──
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

    // ── [12] 退出按钮 ──
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
    pipe::init_send(write_fd);
    pipe::init_receive(read_fd, output_lines);

    // ── 准备 UI ──
    let mut terminal = ui::setup()?;

    // ── UI 主循环 ──
    let res = ui::run_app(&mut terminal, &mut scheduler);

    // ── 结束 UI ──
    ui::restore(&mut terminal)?;

    std::thread::sleep(std::time::Duration::from_secs(1));

    if let Err(err) = res {
        println!("Error: {:?}", err);
    }

    Ok(())
}