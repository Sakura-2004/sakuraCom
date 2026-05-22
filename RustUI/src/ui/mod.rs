use crossterm::{
    event::{self, DisableMouseCapture, EnableMouseCapture, Event},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::{Backend, CrosstermBackend},
    layout::{Alignment, Constraint, Direction, Layout},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, Paragraph},
    Frame, Terminal,
};
use std::{
    io,
    sync::atomic::{AtomicBool, AtomicU8, Ordering},
    time::Duration,
};

mod button;
mod checkbar;
mod component;
mod dropdown;
mod inputbox;
mod outputbox;
mod radiobox;
mod ui_manage;

pub use button::Button;
pub use checkbar::Checkbar;
pub use component::Component;
pub use dropdown::Dropdown;
pub use inputbox::{hex_str_to_bytes, InputBox};
pub use outputbox::{OutputBox, OutputMode};
pub use radiobox::RadioBox;
pub use ui_manage::Scheduler;

/// 退出标志
static SHOULD_QUIT: AtomicBool = AtomicBool::new(false);

/// 连接状态: 0=未连接(红色), 1=已连接(绿色)
pub static CONNECTION_STATUS: AtomicU8 = AtomicU8::new(0);

/// 输入模式: 0=ASCII, 1=HEX
pub static INPUT_MODE: AtomicU8 = AtomicU8::new(0);

/// 输出显示模式: 0=ASCII, 1=HEX
pub static OUTPUT_MODE: AtomicU8 = AtomicU8::new(0);

/// 上一次的输出模式值（用于检测变化，触发 rebuild_from_raw）
pub static PREV_OUTPUT_MODE: AtomicU8 = AtomicU8::new(0);

/// 上一次原始字节缓冲区长度（用于检测新数据到达）
pub static PREV_RAW_LEN: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);

/// 检测并处理输出模式切换
/// 返回 true 表示发生了切换，调用方应执行 rebuild
pub fn check_output_mode_switch() -> bool {
    let current = OUTPUT_MODE.load(Ordering::Relaxed);
    let prev = PREV_OUTPUT_MODE.swap(current, Ordering::Relaxed);
    prev != current
}

/// 检测原始字节缓冲区是否有新数据（比较当前长度与上次记录长度）
/// 返回 (has_new, current_len)
pub fn detect_new_raw_data() -> (bool, usize) {
    let raw = crate::pipe::get_raw_output_snapshot();
    let current_len = raw.len();
    let prev = PREV_RAW_LEN.swap(current_len, std::sync::atomic::Ordering::Relaxed);
    (current_len != prev, current_len)
}

/// 设置退出标志
pub fn request_quit() {
    SHOULD_QUIT.store(true, Ordering::Relaxed);
}

fn should_quit() -> bool {
    SHOULD_QUIT.load(Ordering::Relaxed)
}

/// 运行应用的主循环
pub fn run_app<B: Backend>(
    terminal: &mut Terminal<B>,
    scheduler: &mut Scheduler,
) -> io::Result<()> {
    loop {
        if should_quit() {
            return Ok(());
        }

        while event::poll(Duration::from_millis(0))? {
            if should_quit() {
                return Ok(());
            }

            let ev = event::read()?;
            /*
            if let Event::Key(key) = &ev {
                if key.code == crossterm::event::KeyCode::Char('q') {
                    return Ok(());
                }
            }
            */
            scheduler.handle_event(&ev);
        }

        // 每 20ms 绘制
        terminal.draw(|f| {
            ui(f, scheduler);
        })?;

        std::thread::sleep(Duration::from_millis(20));
    }
}

/// 设置终端
pub fn setup() -> io::Result<Terminal<CrosstermBackend<io::Stdout>>> {
    enable_raw_mode()?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen, EnableMouseCapture)?;
    let backend = CrosstermBackend::new(stdout);
    let terminal = Terminal::new(backend)?;
    Ok(terminal)
}

/// 恢复终端
pub fn restore(terminal: &mut Terminal<CrosstermBackend<io::Stdout>>) -> io::Result<()> {
    while event::poll(Duration::from_millis(0))? {
        event::read()?;
    }

    execute!(
        terminal.backend_mut(),
        DisableMouseCapture,
        LeaveAlternateScreen,
    )?;
    terminal.show_cursor()?;
    disable_raw_mode()?;
    Ok(())
}

/// 统计信息：已发送字节数和已接收字节数
pub struct Stats {
    pub sent_bytes: u64,
    pub received_bytes: u64,
}

impl Stats {
    pub fn new() -> Self {
        Self {
            sent_bytes: 0,
            received_bytes: 0,
        }
    }
}

/// 绘制 UI 布局
fn ui(f: &mut Frame, scheduler: &mut Scheduler) {
    let area = f.area();

    // ── 整体垂直布局 ──
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints(
            [
                Constraint::Length(3),   // [0] 标题区 + 提示
                Constraint::Length(3),   // [1] 串口选择下拉菜单
                Constraint::Length(3),   // [2] 波特率 / 校验位 / 停止位（三栏）
                Constraint::Length(3),   // [3] 打开串口按钮 + 统计输出 + 状态
                Constraint::Length(5),   // [4] 输入栏 + 输入模式选择
                Constraint::Min(5),      // [5] 输出栏（填满剩余）
                Constraint::Length(3),   // [6] 发送 / 清空 / 退出按钮
            ]
            .as_ref(),
        )
        .margin(1)
        .split(area);

    // ── [0] 标题区 ──
    let main_block = Block::default()
        .title("SakuraCom")
        .title_alignment(Alignment::Center)
        .borders(Borders::ALL);

    let text = vec![
        Line::from(vec![
            Span::styled(
                "SakuraCom Serial Tool",
                Style::default()
                    .fg(Color::Cyan)
                    .add_modifier(Modifier::BOLD),
            ),
        ]),
        Line::from(vec![
            Span::styled(
                "Tab: switch focus  |  Enter/Space: select  |  q: quit",
                Style::default().fg(Color::Gray),
            ),
        ]),
    ];

    let paragraph = Paragraph::new(text)
        .block(main_block)
        .alignment(Alignment::Center);

    f.render_widget(paragraph, chunks[0]);

    // ── 获取所有组件 ──
    let comps = scheduler.components_mut();
    let total = comps.len();

    // 组件注册顺序：
    //   0: Dropdown (串口选择)       → chunks[1]
    //   1: Dropdown (波特率)         → chunks[2] 左1
    //   2: Dropdown (数据位)         → chunks[2] 左2
    //   3: Dropdown (校验位)         → chunks[2] 右2
    //   4: Dropdown (停止位)         → chunks[2] 右1
    //   5: Button   (打开串口)       → chunks[3] 左
    //   6: OutputBox (统计)          → chunks[3] 中
    //   7: RadioBox (输入模式)       → chunks[4] 右
    //   8: InputBox (输入栏)         → chunks[4] 左
    //   9: OutputBox (输出栏)        → chunks[5] 左
    //  10: RadioBox (输出模式)       → chunks[5] 右
    //  11: Button   (发送)           → chunks[6] 左
    //  12: Button   (清空)           → chunks[6] 中
    //  13: Button   (退出)           → chunks[6] 右

    if total >= 5 {
        // ── [1] 串口选择 ──
        comps[0].set_area(chunks[1]);

        // ── [2] 波特率 / 数据位 / 校验位 / 停止位 ──
        let param_chunks = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([
                Constraint::Ratio(1, 4),
                Constraint::Ratio(1, 4),
                Constraint::Ratio(1, 4),
                Constraint::Ratio(1, 4),
            ])
            .split(chunks[2]);

        comps[1].set_area(param_chunks[0]); // 波特率
        comps[2].set_area(param_chunks[1]); // 数据位
        comps[3].set_area(param_chunks[2]); // 校验位
        comps[4].set_area(param_chunks[3]); // 停止位

        // ── [3] 打开串口按钮 + 统计输出 + 状态指示 ──
        if total >= 7 {
            let bottom_chunks = Layout::default()
                .direction(Direction::Horizontal)
                .constraints([
                    Constraint::Length(16), // 按钮占 16 字符
                    Constraint::Min(10),    // 统计输出占剩余
                    Constraint::Length(12), // 状态指示占 12 字符
                ])
                .split(chunks[3]);

            comps[5].set_area(bottom_chunks[0]); // 打开串口按钮
            comps[6].set_area(bottom_chunks[1]); // 统计输出栏

            // ── [3-右] 状态指示器 (直接绘制，非组件) ──
            let connected = CONNECTION_STATUS.load(Ordering::Relaxed) != 0;
            let (indicator_text, indicator_color) = if connected {
                ("● 已连接", Color::Green)
            } else {
                ("○ 未连接", Color::Red)
            };
            let indicator_style = Style::default()
                .fg(indicator_color)
                .bg(Color::Black);
            let indicator_block = Block::default()
                .borders(Borders::ALL)
                .style(indicator_style);
            let indicator_paragraph = Paragraph::new(
                Line::from(vec![
                    Span::styled(indicator_text, indicator_style),
                ])
            )
            .block(indicator_block)
            .alignment(Alignment::Center);
            f.render_widget(indicator_paragraph, bottom_chunks[2]);

            // ── 根据连接状态更新按钮标签、样式和锁定模式 ──
            if let Some(btn) = comps[5].as_any_mut().downcast_mut::<Button>() {
                if connected {
                    btn.set_label("断开连接");
                    btn.set_style(Style::default().fg(Color::White).bg(Color::Red));
                    btn.set_focus_style(
                        Style::default()
                            .fg(Color::Black)
                            .bg(Color::LightRed)
                            .add_modifier(Modifier::BOLD),
                    );
                } else {
                    btn.set_label("打开串口");
                    btn.set_style(Style::default().fg(Color::White).bg(Color::Green));
                    btn.set_focus_style(
                        Style::default()
                            .fg(Color::Black)
                            .bg(Color::LightGreen)
                            .add_modifier(Modifier::BOLD),
                    );
                }
            }

            // ── 锁定模式：已连接时锁定串口配置 dropdown (索引 0-4)，未连接时禁用发送/清空按钮 ──
            // 串口选择 (0)、波特率 (1)、数据位 (2)、校验位 (3)、停止位 (4)
            for i in 0..5usize {
                if let Some(dropdown) = comps[i].as_any_mut().downcast_mut::<Dropdown>() {
                    if connected {
                        dropdown.lock();
                    } else {
                        dropdown.unlock();
                    }
                }
            }
            // 发送按钮 (11)、清空按钮 (12)
            if total >= 13 {
                for i in 11..13usize {
                    if let Some(btn) = comps[i].as_any_mut().downcast_mut::<Button>() {
                        btn.set_disabled(!connected);
                    }
                }
            }
        }

        // ── [4] 输入栏（左 2/3）+ 输入模式选择（右 1/3） ──
        if total >= 9 {
            let input_row = Layout::default()
                .direction(Direction::Horizontal)
                .constraints([
                    Constraint::Ratio(3, 4),  // 输入框占 2/3
                    Constraint::Ratio(1, 4),  // 模式选择占 1/3
                ])
                .split(chunks[4]);

            comps[8].set_area(input_row[0]);  // InputBox
            comps[7].set_area(input_row[1]);  // RadioBox（输入模式）
        }

        // ── [5] 输出栏（左 2/3）+ 输出模式选择（右 1/3） ──
        if total >= 11 {
            let output_row = Layout::default()
                .direction(Direction::Horizontal)
                .constraints([
                    Constraint::Ratio(3, 4),  // 输出框占 2/3
                    Constraint::Ratio(1, 4),  // 模式选择占 1/3
                ])
                .split(chunks[5]);

            comps[9].set_area(output_row[0]);  // OutputBox（输出栏）
            comps[10].set_area(output_row[1]);  // RadioBox（输出模式）
        } else if total >= 10 {
            comps[9].set_area(chunks[5]);  // 只有输出栏时占满
        }

        // ── [6] 发送 / 清空 / 退出按钮 ──
        if total >= 14 {
            let btn_row = Layout::default()
                .direction(Direction::Horizontal)
                .constraints([
                    Constraint::Ratio(1, 3),  // 发送
                    Constraint::Ratio(1, 3),  // 清空
                    Constraint::Ratio(1, 3),  // 退出
                ])
                .split(chunks[6]);

            comps[11].set_area(btn_row[0]);  // 发送按钮
            comps[12].set_area(btn_row[1]);  // 清空按钮
            comps[13].set_area(btn_row[2]);  // 退出按钮
        }
    }

    // ── 渲染前同步：OutputBox (索引9) 从共享缓冲区拉取 pipe reader 写入的数据 ──
    if total >= 10 {
        if let Some(output_box) = comps[9].as_any_mut().downcast_mut::<OutputBox>() {
            // 检查是否有清空请求（清空按钮通过 pipe::take_clear_request 设置）
            if crate::pipe::take_clear_request() {
                output_box.clear();
                // 清空时重置 PREV_RAW_LEN，避免清空后误判为新数据
                PREV_RAW_LEN.store(0, Ordering::Relaxed);
            }

            // 同步输出模式
            let mode = match OUTPUT_MODE.load(Ordering::Relaxed) {
                0 => OutputMode::Ascii,
                _ => OutputMode::Hex,
            };
            output_box.set_mode(mode);

            // 检测输出模式是否切换
            let mode_switched = check_output_mode_switch();
            // 检测是否有新数据到达（比较 raw buffer 长度变化）
            let (has_new_data, _current_len) = detect_new_raw_data();

            // 统一策略：有新数据 或 模式切换时，基于 raw buffer 重建显示并滚动到底部
            if has_new_data || mode_switched {
                let raw_snapshot = crate::pipe::get_raw_output_snapshot();
                output_box.rebuild_from_raw(&raw_snapshot);
                output_box.scroll_to_bottom();
            }

            // 始终消耗共享缓冲区，防止内存泄漏
            output_box.drain_shared();
        }
    }

    // ── 每帧检查 InputBox 错误消息是否过期 (索引8: InputBox) ──
    if total >= 9 {
        if let Some(input_box) = comps[8].as_any_mut().downcast_mut::<InputBox>() {
            input_box.take_error_message();
        }
    }

    // ── 更新统计信息 (索引6: stats OutputBox) ──
    if total >= 7 {
        if let Some(stats_box) = comps[6].as_any_mut().downcast_mut::<OutputBox>() {
            let sent = crate::pipe::sent_bytes();
            let recv = crate::pipe::received_bytes();
            stats_box.clear();
            stats_box.append_line(&format!("发送: {} bytes  |  接收: {} bytes", sent, recv));
        }
    }

    // ── 渲染所有组件 ──
    scheduler.render_all(f);
}
