use crossterm::event::KeyCode;
use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::{Color, Style},
    text::Line,
    widgets::{Block, Borders, Paragraph, Widget},
};

use super::component::{BaseComponent, Component};
use std::any::Any;

/// 单选组件（多选一，类似 radio button）
pub struct RadioBox {
    /// 基础组件
    base: BaseComponent,
    /// 选项列表
    options: Vec<String>,
    /// 当前选中项的索引
    selected_index: usize,
    /// 是否锁定（锁定后不可修改选择）
    locked: bool,
    /// 标题名称
    title: String,
    /// 选择变更回调
    on_select_cb: Option<Box<dyn FnMut(usize, &str)>>,
}

impl RadioBox {
    /// 创建一个新的单选组件（默认标题为"选项"）
    pub fn new(options: Vec<&str>) -> Self {
        Self::with_title("选项", options)
    }

    /// 创建一个带自定义标题的单选组件
    pub fn with_title(title: &str, options: Vec<&str>) -> Self {
        let base = BaseComponent::new("");

        Self {
            base,
            options: options.iter().map(|s| s.to_string()).collect(),
            selected_index: 0,
            locked: false,
            title: title.to_string(),
            on_select_cb: None,
        }
    }

    /// 设置选择变更回调
    pub fn on_select<F: FnMut(usize, &str) + 'static>(&mut self, callback: F) {
        self.on_select_cb = Some(Box::new(callback));
    }

    /// 获取当前选中的索引
    pub fn selected_index(&self) -> usize {
        self.selected_index
    }

    /// 获取当前选中的文本
    pub fn selected_text(&self) -> &str {
        if self.selected_index < self.options.len() {
            &self.options[self.selected_index]
        } else {
            ""
        }
    }

    /// 设置选中项
    pub fn set_selected(&mut self, index: usize) {
        if index < self.options.len() {
            self.selected_index = index;
            self.fire_select();
        }
    }

    /// 是否被锁定
    pub fn is_locked(&self) -> bool {
        self.locked
    }

    /// 锁定（禁止修改选择）
    pub fn lock(&mut self) {
        self.locked = true;
    }

    /// 解锁（允许修改选择）
    pub fn unlock(&mut self) {
        self.locked = false;
    }

    /// 设置样式
    pub fn set_style(&mut self, style: Style) {
        self.base.set_style(style);
    }

    /// 设置焦点样式
    pub fn set_focus_style(&mut self, style: Style) {
        self.base.set_focus_style(style);
    }

    /// 设置选项列表
    pub fn set_options(&mut self, options: Vec<&str>) {
        if self.locked {
            return;
        }
        self.options = options.iter().map(|s| s.to_string()).collect();
        if self.selected_index >= self.options.len() {
            self.selected_index = if self.options.is_empty() { 0 } else { 0 };
        }
    }

    /// 选择下一项
    fn select_next(&mut self) {
        if self.locked || self.options.is_empty() {
            return;
        }
        self.selected_index = (self.selected_index + 1) % self.options.len();
        self.fire_select();
    }

    /// 选择上一项
    fn select_prev(&mut self) {
        if self.locked || self.options.is_empty() {
            return;
        }
        self.selected_index = if self.selected_index == 0 {
            self.options.len() - 1
        } else {
            self.selected_index - 1
        };
        self.fire_select();
    }

    /// 触发选择回调
    fn fire_select(&mut self) {
        if let Some(ref mut cb) = self.on_select_cb {
            let text = self.options[self.selected_index].clone();
            cb(self.selected_index, &text);
        }
    }
}

impl Component for RadioBox {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        let style = self.base.current_style();
        let selected_style = Style::default()
            .fg(Color::Green)
            .bg(style.bg.unwrap_or(Color::Reset));

        // 构建显示文本，带 radio 按钮标记
        let display: Vec<Line<'static>> = self
            .options
            .iter()
            .enumerate()
            .map(|(i, opt)| {
                let radio = if i == self.selected_index {
                    "(●)"  // 选中
                } else {
                    "(○)"  // 未选中
                };
                let prefix = if i == self.selected_index { ">" } else { " " };
                let text = format!(" {} {} {}", prefix, radio, opt);
                if i == self.selected_index {
                    Line::styled(text, selected_style)
                } else {
                    Line::from(text)
                }
            })
            .collect();

        let title_text = if self.locked {
            format!(" {} 🔒 ", self.title)
        } else {
            format!(" {} ", self.title)
        };

        let block = Block::default()
            .borders(Borders::ALL)
            .title(title_text)
            .style(style);

        let paragraph = Paragraph::new(display)
            .block(block);
        paragraph.render(area, buf);
    }

    fn area(&self) -> Rect {
        self.base.area
    }

    fn set_area(&mut self, rect: Rect) {
        self.base.area = rect;
    }

    fn on_click(&mut self) -> bool {
        // 点击切换选择
        self.select_next();
        true
    }

    fn on_key(&mut self, key: KeyCode) -> bool {
        match key {
            KeyCode::Up | KeyCode::Char('k') => {
                self.select_prev();
                true
            }
            KeyCode::Down | KeyCode::Char('j') => {
                self.select_next();
                true
            }
            KeyCode::Enter | KeyCode::Char(' ') => {
                // 空格/回车也切换
                self.select_next();
                true
            }
            _ => false,
        }
    }

    fn focusable(&self) -> bool {
        !self.locked
    }

    fn set_focused(&mut self, focused: bool) {
        self.base.has_focus = focused;
    }

    fn component_name(&self) -> &'static str {
        "RadioBox"
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }
}