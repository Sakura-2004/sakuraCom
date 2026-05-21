use crossterm::event::KeyCode;
use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::{Color, Style},
    text::Span,
    widgets::{Block, Borders, Paragraph, Widget, Wrap},
};

use super::component::{BaseComponent, Component};
use std::any::Any;
use std::sync::{Arc, Mutex};

/// 输出显示模式
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OutputMode {
    Ascii,
    Hex,
}

/// 输出框组件（只读，用于显示日志/结果，支持 HEX/ASCII 模式）
///
/// 支持两种缓冲区模式：
/// - 自有模式（`new`）：行数据存储在组件内部
/// - 共享模式（`new_shared`）：行数据存储在 `Arc<Mutex<Vec<String>>>` 中，
///   用于 pipe reader 线程直接写入，渲染时自动同步
///
/// 支持滚动：
/// - 鼠标滚轮：在区域内滚动
/// - Up/Down 方向键：上下移动滚动偏移
pub struct OutputBox {
    /// 基础组件
    base: BaseComponent,
    /// 自有行缓冲区（当 shared_lines 为 None 时使用）
    own_lines: Vec<String>,
    /// 共享行缓冲区（pipe reader 线程直接写入，渲染时同步到 own_lines）
    shared_lines: Option<Arc<Mutex<Vec<String>>>>,
    /// 最大保留行数（防止无限增长）
    max_lines: usize,
    /// 当前显示模式
    mode: OutputMode,
    /// 滚动偏移量（从顶部算起，跳过了多少**物理行**（换行后的行））
    scroll_offset: usize,
}

impl OutputBox {
    /// 创建自有缓冲区的输出框
    pub fn new(_title: &str) -> Self {
        let base = BaseComponent::new("");

        Self {
            base,
            own_lines: Vec::new(),
            shared_lines: None,
            max_lines: 1000,
            mode: OutputMode::Ascii,
            scroll_offset: 0,
        }
    }

    /// 创建共享缓冲区的输出框（用于 pipe 接收）
    pub fn new_shared(_title: &str) -> (Self, Arc<Mutex<Vec<String>>>) {
        let base = BaseComponent::new("");
        let shared = Arc::new(Mutex::new(Vec::<String>::new()));

        let component = Self {
            base,
            own_lines: Vec::new(),
            shared_lines: Some(shared.clone()),
            max_lines: 1000,
            mode: OutputMode::Ascii,
            scroll_offset: 0,
        };

        (component, shared)
    }

    /// 从共享缓冲区同步行数据到自有缓冲区（渲染前调用）
    /// 新数据到达时自动滚动到最底部
    pub fn sync_from_shared(&mut self) {
        let has_new_data = if let Some(ref shared) = self.shared_lines {
            if let Ok(mut shared_lines) = shared.lock() {
                if !shared_lines.is_empty() {
                    self.own_lines.append(&mut shared_lines);
                    // 裁剪超出部分
                    while self.own_lines.len() > self.max_lines {
                        self.own_lines.remove(0);
                    }
                    true
                } else {
                    false
                }
            } else {
                false
            }
        } else {
            false
        };

        if has_new_data {
            self.scroll_to_bottom();
        }
    }

    /// 清空共享缓冲区（HEX 模式下不追加到 own_lines，仅消耗共享数据防止泄漏）
    pub fn drain_shared(&mut self) {
        if let Some(ref shared) = self.shared_lines {
            if let Ok(mut shared_lines) = shared.lock() {
                shared_lines.clear();
            }
        }
    }

    /// 追加一行输出
    pub fn append_line(&mut self, text: &str) {
        self.own_lines.push(text.to_string());
        while self.own_lines.len() > self.max_lines {
            self.own_lines.remove(0);
        }
        // 追加时也自动滚动到底部
        self.scroll_to_bottom();
    }

    /// 追加原始字节数据（根据当前模式自动格式化）
    pub fn append_bytes(&mut self, data: &[u8]) {
        match self.mode {
            OutputMode::Ascii => {
                let text = String::from_utf8_lossy(data).to_string();
                let cleaned: String = text
                    .chars()
                    .map(|c| {
                        if c.is_ascii_control() && c != '\n' && c != '\r' {
                            format!("[{:02X}]", c as u8)
                        } else {
                            c.to_string()
                        }
                    })
                    .collect();
                self.append_line(&cleaned);
            }
            OutputMode::Hex => {
                let hex_str: String = data
                    .iter()
                    .map(|b| format!("{:02X}", b))
                    .collect::<Vec<_>>()
                    .join(" ");
                self.append_line(&hex_str);
            }
        }
    }

    /// 设置内容（覆盖）
    pub fn set_content(&mut self, text: &str) {
        self.own_lines = text.lines().map(|s| s.to_string()).collect();
    }

    /// 清空输出
    pub fn clear(&mut self) {
        self.own_lines.clear();
        self.scroll_offset = 0;
    }

    /// 获取当前行数
    pub fn line_count(&self) -> usize {
        self.own_lines.len()
    }

    /// 设置样式
    pub fn set_style(&mut self, style: Style) {
        self.base.set_style(style);
    }

    /// 设置焦点样式
    pub fn set_focus_style(&mut self, style: Style) {
        self.base.set_focus_style(style);
    }

    /// 设置最大保留行数
    pub fn set_max_lines(&mut self, max: usize) {
        self.max_lines = max;
        while self.own_lines.len() > self.max_lines {
            self.own_lines.remove(0);
        }
    }

    /// 设置显示模式
    pub fn set_mode(&mut self, mode: OutputMode) {
        self.mode = mode;
    }

    /// 获取当前显示模式
    pub fn mode(&self) -> OutputMode {
        self.mode
    }

    /// 从原始字节数据重建行显示（上层封装：模式切换时调用）
    /// 先清空 own_lines，再根据当前 self.mode 将 raw_bytes 转为行并压入
    pub fn rebuild_from_raw(&mut self, raw_bytes: &[u8]) {
        self.own_lines.clear();
        self.scroll_offset = 0;

        if raw_bytes.is_empty() {
            return;
        }

        match self.mode {
            OutputMode::Ascii => {
                // ASCII 模式：将原始字节按 UTF-8 解析为字符串，非 UTF-8 部分用 HEX 表示
                let text = String::from_utf8_lossy(raw_bytes);
                // 按行分割
                for line in text.lines() {
                    self.own_lines.push(line.to_string());
                }
            }
            OutputMode::Hex => {
                // HEX 模式：所有字节按顺序排列，不带 ASCII 预览，不考虑换行
                let hex_str: String = raw_bytes
                    .iter()
                    .map(|b| format!("{:02X}", b))
                    .collect::<Vec<_>>()
                    .join(" ");
                self.own_lines.push(hex_str);
            }
        }
    }

    /// 对文本进行自动换行，返回每个显示行的内容
    fn wrap_text(&self, text: &str, max_width: u16) -> Vec<String> {
        if max_width == 0 {
            return vec![text.to_string()];
        }
        let max_w = max_width as usize;
        let mut lines = Vec::new();
        let mut current = String::new();
        for ch in text.chars() {
            let ch_width = if ch.is_ascii() || (ch as u32) < 0x2E80 { 1 } else { 2 };
            let cur_len = self.str_width(&current);
            if cur_len + ch_width > max_w {
                lines.push(std::mem::take(&mut current));
            }
            current.push(ch);
        }
        if !current.is_empty() {
            lines.push(current);
        }
        if lines.is_empty() {
            lines.push(String::new());
        }
        lines
    }

    /// 估算字符串的显示宽度（ASCII 字符按1，CJK/全角按2）
    fn str_width(&self, s: &str) -> usize {
        s.chars().map(|c| if c.is_ascii() || (c as u32) < 0x2E80 { 1 } else { 2 }).sum()
    }

    /// 计算总物理行数（用于计算滚动条比例）
    fn total_physical_lines(&self, visible_width: u16) -> usize {
        let inner_width = visible_width.saturating_sub(2); // 减去边框和滚动条区域
        let mut total = 0usize;
        for line in &self.own_lines {
            let wrapped = self.wrap_text(line, inner_width);
            total += wrapped.len();
        }
        total
    }

    /// 获取可见行数（不包括边框）
    fn visible_rows(&self, area_height: u16) -> usize {
        area_height.saturating_sub(2) as usize
    }

    /// 滚动到最底部（新数据到达时调用）
    fn scroll_to_bottom(&mut self) {
        let area = self.base.area;
        let inner_width = area.width.saturating_sub(4).max(1);
        let visible_rows = self.visible_rows(area.height);
        let mut total_phys = 0usize;
        for line in &self.own_lines {
            let wrapped = self.wrap_text(line, inner_width);
            total_phys += wrapped.len();
        }
        let max_offset = total_phys.saturating_sub(visible_rows);
        self.scroll_offset = max_offset;
    }

    /// 限制滚动偏移在有效范围内
    fn clamp_scroll(&mut self, total_phys: usize, visible_rows: usize) {
        let max_offset = total_phys.saturating_sub(visible_rows);
        if self.scroll_offset > max_offset {
            self.scroll_offset = max_offset;
        }
    }

    /// 渲染一个垂直滚动条字符：用 '│'、'█'、'▲'、'▼' 表示
    fn render_scrollbar(
        &self,
        buf: &mut Buffer,
        x: u16,
        y_start: u16,
        height: u16,
        offset: usize,
        total: usize,
        visible: usize,
    ) {
        if total == 0 || total <= visible {
            return;
        }
        // 滑块大小（至少 1）
        let thumb_size = (visible * height as usize / total).max(1);
        // 滑块位置
        let thumb_pos = if total > visible {
            (offset * height as usize / total).min(height as usize - thumb_size)
        } else {
            0
        };

        for row in 0..height as usize {
            let ch = if row == 0 {
                '▲'
            } else if row == height as usize - 1 {
                '▼'
            } else if row >= thumb_pos && row < thumb_pos + thumb_size {
                '█'
            } else {
                '│'
            };
            buf.get_mut(x, y_start + row as u16)
                .set_char(ch)
                .set_style(Style::default().fg(Color::DarkGray));
        }
    }
}

impl Component for OutputBox {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        let style = self.base.current_style();

        let inner_width = area.width.saturating_sub(4); // borders(2) + scrollbar(1) + padding
        let inner_width = inner_width.max(1);
        let visible_rows = area.height.saturating_sub(2) as usize;

        // 对所有原始行进行自动换行，得到物理行列表
        let mut phys_lines: Vec<String> = Vec::new();
        for line in &self.own_lines {
            let wrapped = self.wrap_text(line, inner_width);
            phys_lines.extend(wrapped);
        }
        let total_phys = phys_lines.len();

        // 计算最大滚动偏移
        let max_offset = total_phys.saturating_sub(visible_rows);
        let eff_offset = self.scroll_offset.min(max_offset);

        // 截取可见范围内的物理行
        let visible_slice: Vec<&str> = if total_phys > 0 {
            let start = eff_offset;
            let end = (start + visible_rows).min(total_phys);
            phys_lines[start..end].iter().map(|s| s.as_str()).collect()
        } else {
            Vec::new()
        };

        let mode_label = match self.mode {
            OutputMode::Ascii => "ASCII",
            OutputMode::Hex => "HEX",
        };

        let block = Block::default()
            .borders(Borders::ALL)
            .title(format!(
                " 输出 [{}] {} [{}/{}] ",
                mode_label,
                if self.base.has_focus { "[F]" } else { "" },
                eff_offset.saturating_add(1).min(total_phys),
                total_phys
            ))
            .style(style);

        // 渲染段落
        let paragraph = Paragraph::new(visible_slice.join("\n"))
            .block(block.clone())
            .wrap(Wrap { trim: false });
        paragraph.render(area, buf);

        // 绘制滚动条（在右侧边框内的右侧）
        if area.width >= 3 && area.height >= 3 {
            let sb_x = area.x + area.width.saturating_sub(2); // 右侧边框位置
            let sb_y = area.y + 1;
            let sb_height = area.height.saturating_sub(2);
            self.render_scrollbar(buf, sb_x, sb_y, sb_height, eff_offset, total_phys, visible_rows);
        }
    }

    fn area(&self) -> Rect {
        self.base.area
    }

    fn set_area(&mut self, rect: Rect) {
        self.base.area = rect;
    }

    fn on_click(&mut self) -> bool {
        false
    }

    fn on_key(&mut self, key: KeyCode) -> bool {
        let inner_width = self.base.area.width.saturating_sub(4).max(1);
        let visible_rows = self.base.area.height.saturating_sub(2) as usize;

        // 计算总物理行数
        let mut total_phys = 0usize;
        for line in &self.own_lines {
            let wrapped = self.wrap_text(line, inner_width);
            total_phys += wrapped.len();
        }

        let handled = match key {
            KeyCode::Up | KeyCode::Char('k') => {
                if self.scroll_offset > 0 {
                    self.scroll_offset = self.scroll_offset.saturating_sub(1);
                }
                true
            }
            KeyCode::Down | KeyCode::Char('j') => {
                let max_offset = total_phys.saturating_sub(visible_rows);
                if self.scroll_offset < max_offset {
                    self.scroll_offset += 1;
                }
                true
            }
            KeyCode::PageUp => {
                let jump = (visible_rows / 2).max(1);
                self.scroll_offset = self.scroll_offset.saturating_sub(jump);
                true
            }
            KeyCode::PageDown => {
                let max_offset = total_phys.saturating_sub(visible_rows);
                let jump = (visible_rows / 2).max(1);
                self.scroll_offset = (self.scroll_offset + jump).min(max_offset);
                true
            }
            KeyCode::Home => {
                self.scroll_offset = 0;
                true
            }
            KeyCode::End => {
                let max_offset = total_phys.saturating_sub(visible_rows);
                self.scroll_offset = max_offset;
                true
            }
            _ => false,
        };

        self.clamp_scroll(total_phys, visible_rows);
        handled
    }

    fn on_scroll(&mut self, up: bool) -> bool {
        let inner_width = self.base.area.width.saturating_sub(4).max(1);
        let visible_rows = self.base.area.height.saturating_sub(2) as usize;
        let mut total_phys = 0usize;
        for line in &self.own_lines {
            let wrapped = self.wrap_text(line, inner_width);
            total_phys += wrapped.len();
        }

        if up {
            // 向上滚动：减少 offset（显示更早的内容）
            self.scroll_offset = self.scroll_offset.saturating_sub(3);
        } else {
            // 向下滚动：增加 offset
            let max_offset = total_phys.saturating_sub(visible_rows);
            self.scroll_offset = (self.scroll_offset + 3).min(max_offset);
        }
        self.clamp_scroll(total_phys, visible_rows);
        true
    }

    fn focusable(&self) -> bool {
        true
    }

    fn set_focused(&mut self, focused: bool) {
        self.base.has_focus = focused;
    }

    fn component_name(&self) -> &'static str {
        "输出"
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }
}