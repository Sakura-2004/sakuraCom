use crossterm::event::KeyCode;
use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::{Color, Style},
    widgets::{Block, Borders, Paragraph, Widget, Wrap},
};

use super::component::{BaseComponent, Component};
use std::any::Any;

/// 输入框组件（支持多行输入，Enter 插入换行，HEX 模式校验）
pub struct InputBox {
    /// 基础组件
    base: BaseComponent,
    /// 输入的内容（支持换行符）
    content: String,
    /// 光标位置（字符索引）
    cursor_pos: usize,
    /// 输入回调（每次内容变更时触发）
    on_input_cb: Option<Box<dyn FnMut(&str)>>,
    /// 错误提示信息（非法的 HEX 字符等）
    error_message: Option<String>,
    /// 错误消息定时器（帧数）
    error_timer: u32,
    /// 滚动偏移（物理行级别）
    scroll_offset: usize,
}

impl InputBox {
    /// 创建一个新的输入框
    pub fn new(_placeholder: &str) -> Self {
        let base = BaseComponent::new("");

        Self {
            base,
            content: String::new(),
            cursor_pos: 0,
            on_input_cb: None,
            error_message: None,
            error_timer: 0,
            scroll_offset: 0,
        }
    }

    /// 设置输入回调
    pub fn on_input<F: FnMut(&str) + 'static>(&mut self, callback: F) {
        self.on_input_cb = Some(Box::new(callback));
    }

    /// 获取当前输入的内容
    pub fn content(&self) -> &str {
        &self.content
    }

    /// 设置内容
    pub fn set_content(&mut self, text: &str) {
        self.content = text.to_string();
        self.cursor_pos = self.content.len();
    }

    /// 清空内容
    pub fn clear(&mut self) {
        self.content.clear();
        self.cursor_pos = 0;
        self.scroll_offset = 0;
    }

    /// 设置样式
    pub fn set_style(&mut self, style: Style) {
        self.base.set_style(style);
    }

    /// 设置焦点样式
    pub fn set_focus_style(&mut self, style: Style) {
        self.base.set_focus_style(style);
    }

    /// 尝试插入字符（HEX 模式下过滤非法字符）
    pub fn try_insert_char(&mut self, c: char, hex_mode: bool) {
        if hex_mode {
            // HEX 模式：只允许 0-9, a-f, A-F, 空格
            if c.is_ascii_hexdigit() || c == ' ' {
                self.content.insert(self.cursor_pos, c);
                self.cursor_pos += 1;
                self.fire_input();
            } else if !c.is_ascii_whitespace() {
                self.error_message = Some(format!("非法 HEX 字符: '{}'，仅允许 0-9, a-f, A-F", c));
                self.error_timer = 30;
            }
        } else {
            // ASCII 模式：接受所有字符
            self.content.insert(self.cursor_pos, c);
            self.cursor_pos += 1;
            self.fire_input();
        }
    }

    /// 插入字符到光标位置
    fn insert_char(&mut self, c: char) {
        self.content.insert(self.cursor_pos, c);
        self.cursor_pos += 1;
        self.fire_input();
    }

    /// 删除光标前的字符
    fn delete_char_before(&mut self) {
        if self.cursor_pos > 0 {
            self.content.remove(self.cursor_pos - 1);
            self.cursor_pos -= 1;
            self.fire_input();
        }
    }

    /// 删除光标后的字符
    fn delete_char_after(&mut self) {
        if self.cursor_pos < self.content.len() {
            self.content.remove(self.cursor_pos);
            self.fire_input();
        }
    }

    /// 触发输入回调
    fn fire_input(&mut self) {
        if let Some(ref mut cb) = self.on_input_cb {
            cb(&self.content);
        }
    }

    /// 获取错误消息（自动降帧清除）
    pub fn take_error_message(&mut self) -> Option<String> {
        if self.error_timer > 0 {
            self.error_timer -= 1;
            self.error_message.clone()
        } else {
            self.error_message = None;
            None
        }
    }

    // ── 辅助：字符串显示宽度 ──
    fn str_width(&self, s: &str) -> usize {
        s.chars()
            .map(|c| if c.is_ascii() || (c as u32) < 0x2E80 { 1 } else { 2 })
            .sum()
    }

    // ── 把 content 换行为物理行 ──
    fn phys_lines(&self, content: &str, max_width: u16) -> Vec<String> {
        let max_w = max_width.max(1) as usize;
        let mut lines = Vec::new();
        // 先按 '\n' 切分逻辑行
        for logical_line in content.lines() {
            let logical = if logical_line.is_empty() { "" } else { logical_line };
            let mut current = String::new();
            for ch in logical.chars() {
                let ch_w = if ch.is_ascii() || (ch as u32) < 0x2E80 { 1 } else { 2 };
                if self.str_width(&current) + ch_w > max_w {
                    lines.push(std::mem::take(&mut current));
                }
                current.push(ch);
            }
            lines.push(current);
        }
        // 如果 content 以 \n 结尾，追加一个空行
        if content.ends_with('\n') {
            lines.push(String::new());
        }
        if lines.is_empty() {
            lines.push(String::new());
        }
        lines
    }

    // ── 将光标字符索引转换成 (物理行号, 列偏移) ──
    fn cursor_to_visual(&self, content: &str, max_width: u16) -> (usize, usize, Vec<String>) {
        let phys_lines = self.phys_lines(content, max_width);
        let mut pos = 0usize;
        let mut target_line = 0usize;
        let mut target_col = 0usize;
        let mut found = false;

        for (li, pline) in phys_lines.iter().enumerate() {
            let _start = pos;
            // 这个物理行对应哪些字符段？
            // 物理行就是 content 中从 start 开始的 pline.chars() 个字符（不含 \n）
            // 实际上 phys_lines 构建时已经把换行后的空行都处理了
            // 简化：逐字符计数匹配
            let mut char_count = 0usize;
            for _ in pline.chars() {
                if pos == self.cursor_pos {
                    target_line = li;
                    target_col = char_count;
                    found = true;
                    break;
                }
                pos += 1;
                char_count += 1;
            }
            if found {
                break;
            }
            // 逻辑行末尾可能还有 '\n'（被 lines() 吃掉了）
            // 如果 cursor_pos 正好在 '\n' 上，光标显示在下一行行首
            if pos < content.len() && content.as_bytes()[pos] == b'\n' {
                if pos == self.cursor_pos {
                    target_line = li + 1;
                    target_col = 0;
                    found = true;
                    break;
                }
                pos += 1; // 跳过 '\n'
            }
        }

        if !found {
            // cursor 在末尾
            target_line = phys_lines.len().saturating_sub(1);
            target_col = phys_lines.last().map(|l| l.chars().count()).unwrap_or(0);
        }

        (target_line, target_col, phys_lines)
    }

    // ── 通过 (物理行, 列) 反推 cursor_pos 字符索引 ──
    fn visual_to_cursor(&self, content: &str, max_width: u16, line: usize, col: usize, _phys_lines: &[String]) -> usize {
        let phys_lines = self.phys_lines(content, max_width);
        let mut pos = 0usize;
        for (li, pline) in phys_lines.iter().enumerate() {
            if li == line {
                let chars: Vec<char> = pline.chars().collect();
                let clamped_col = col.min(chars.len());
                return pos + clamped_col;
            }
            // 跳过该物理行的字符数
            pos += pline.chars().count();
            // 跳过后面的 '\n'（如果存在）
            if pos < content.len() && content.as_bytes()[pos] == b'\n' {
                pos += 1;
            }
        }
        content.len()
    }

    fn clamp_scroll(&mut self, total_phys: usize, visible_rows: usize) {
        let max_offset = total_phys.saturating_sub(visible_rows);
        if self.scroll_offset > max_offset {
            self.scroll_offset = max_offset;
        }
    }
}

impl Component for InputBox {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        let style = self.base.current_style();

        let inner_width = area.width.saturating_sub(4).max(1);
        let visible_rows = area.height.saturating_sub(2) as usize;

        // 构建带光标的显示文本（和 OutputBox 类似：换行 + 滚动）
        let display = if self.content.is_empty() {
            if self.base.has_focus { "|".to_string() } else { String::new() }
        } else {
            let mut display_str = self.content.clone();
            if self.base.has_focus && self.cursor_pos <= display_str.len() {
                display_str.insert(self.cursor_pos, '|');
            }
            display_str
        };

        let phys_lines = self.phys_lines(&display, inner_width);
        let total_phys = phys_lines.len();
        let max_offset = total_phys.saturating_sub(visible_rows);
        let eff_offset = self.scroll_offset.min(max_offset);

        let visible_slice: Vec<&str> = if total_phys > 0 {
            let start = eff_offset;
            let end = (start + visible_rows).min(total_phys);
            phys_lines[start..end].iter().map(|s| s.as_str()).collect()
        } else {
            Vec::new()
        };

        let block_style = if self.error_timer > 0 {
            Style::default().fg(Color::Red)
        } else {
            style
        };

        let block = Block::default()
            .borders(Borders::ALL)
            .title(" 发送数据 ")
            .style(block_style);

        let paragraph = Paragraph::new(visible_slice.join("\n"))
            .block(block)
            .wrap(Wrap { trim: false });
        paragraph.render(area, buf);
    }

    fn area(&self) -> Rect {
        self.base.area
    }

    fn set_area(&mut self, rect: Rect) {
        self.base.area = rect;
    }

    fn on_click(&mut self) -> bool {
        self.cursor_pos = self.content.len();
        true
    }

    fn on_key(&mut self, key: KeyCode) -> bool {
        let inner_width = self.base.area.width.saturating_sub(4).max(1);
        let visible_rows = self.base.area.height.saturating_sub(2) as usize;

        match key {
            KeyCode::Enter => {
                self.insert_char('\n');
                true
            }
            KeyCode::Char(c) => {
                let hex_mode = super::INPUT_MODE.load(std::sync::atomic::Ordering::Relaxed) == 1;
                self.try_insert_char(c, hex_mode);
                true
            }
            KeyCode::Backspace => {
                self.delete_char_before();
                true
            }
            KeyCode::Delete => {
                self.delete_char_after();
                true
            }
            KeyCode::Left => {
                if self.cursor_pos > 0 {
                    self.cursor_pos -= 1;
                }
                true
            }
            KeyCode::Right => {
                if self.cursor_pos < self.content.len() {
                    self.cursor_pos += 1;
                }
                true
            }
            KeyCode::Up => {
                // 光标上移一行（同一视觉列）
                let (vis_line, vis_col, phys) = self.cursor_to_visual(&self.content, inner_width);
                if vis_line > 0 {
                    let new_line = vis_line - 1;
                    self.cursor_pos = self.visual_to_cursor(&self.content, inner_width, new_line, vis_col, &phys);
                }
                true
            }
            KeyCode::Down => {
                let (vis_line, vis_col, phys) = self.cursor_to_visual(&self.content, inner_width);
                let total = phys.len();
                if vis_line + 1 < total {
                    let new_line = vis_line + 1;
                    self.cursor_pos = self.visual_to_cursor(&self.content, inner_width, new_line, vis_col, &phys);
                }
                true
            }
            KeyCode::Home => {
                // 光标跳到行首
                let (vis_line, _vis_col, phys) = self.cursor_to_visual(&self.content, inner_width);
                self.cursor_pos = self.visual_to_cursor(&self.content, inner_width, vis_line, 0, &phys);
                true
            }
            KeyCode::End => {
                // 光标跳到行尾
                let (vis_line, _vis_col, phys) = self.cursor_to_visual(&self.content, inner_width);
                let line_len = phys.get(vis_line).map(|l| l.chars().count()).unwrap_or(0);
                self.cursor_pos = self.visual_to_cursor(&self.content, inner_width, vis_line, line_len, &phys);
                true
            }
            _ => false,
        };

        // 渲染后校准滚动偏移，保持光标可见
        let phys_lines = self.phys_lines(&self.content, inner_width);
        let total_phys = phys_lines.len();
        let visible = visible_rows;
        let (vis_line, _vis_col, _) = self.cursor_to_visual(&self.content, inner_width);

        // 如果光标在可见区域上方，向上滚动
        if vis_line < self.scroll_offset {
            self.scroll_offset = vis_line;
        }
        // 如果光标在可见区域下方，向下滚动
        if vis_line >= self.scroll_offset + visible {
            self.scroll_offset = vis_line.saturating_sub(visible) + 1;
        }
        self.clamp_scroll(total_phys, visible);

        true
    }

    fn on_scroll(&mut self, up: bool) -> bool {
        let inner_width = self.base.area.width.saturating_sub(4).max(1);
        let visible_rows = self.base.area.height.saturating_sub(2) as usize;
        let phys_lines = self.phys_lines(&self.content, inner_width);
        let total_phys = phys_lines.len();

        if up {
            self.scroll_offset = self.scroll_offset.saturating_sub(3);
        } else {
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
        if focused {
            self.cursor_pos = self.content.len();
        }
    }

    fn component_name(&self) -> &'static str {
        "发送数据"
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }
}