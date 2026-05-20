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

/// 单个勾选条目
pub struct CheckItem {
    /// 显示文本
    pub text: String,
    /// 是否被选中
    pub checked: bool,
}

/// 勾选栏组件（类似复选框列表）
pub struct Checkbar {
    /// 基础组件（复用公共字段和方法）
    base: BaseComponent,
    /// 勾选条目列表
    items: Vec<CheckItem>,
    /// 当前选中的条目索引
    selected_index: usize,
    /// 是否锁定（锁定后不可修改）
    locked: bool,
    /// 状态变更回调（参数：索引，是否勾选）
    on_change_cb: Option<Box<dyn FnMut(usize, bool)>>,
}

impl Checkbar {
    /// 创建一个新的勾选栏
    pub fn new(items: Vec<(&str, bool)>) -> Self {
        let base = BaseComponent::new("");
        let check_items: Vec<CheckItem> = items
            .into_iter()
            .map(|(text, checked)| CheckItem {
                text: text.to_string(),
                checked,
            })
            .collect();

        Self {
            base,
            items: check_items,
            selected_index: 0,
            locked: false,
            on_change_cb: None,
        }
    }

    /// 设置状态变更回调
    pub fn on_change<F: FnMut(usize, bool) + 'static>(&mut self, callback: F) {
        self.on_change_cb = Some(Box::new(callback));
    }

    /// 是否被锁定
    pub fn is_locked(&self) -> bool {
        self.locked
    }

    /// 锁定（禁止修改）
    pub fn lock(&mut self) {
        self.locked = true;
    }

    /// 解锁（允许修改）
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

    /// 切换当前选中条目的勾选状态
    pub fn toggle_current(&mut self) {
        if self.locked || self.items.is_empty() {
            return;
        }
        let idx = self.selected_index;
        self.items[idx].checked = !self.items[idx].checked;
        if let Some(ref mut cb) = self.on_change_cb {
            cb(idx, self.items[idx].checked);
        }
    }

    /// 获取当前选中条目的索引
    pub fn selected_index(&self) -> usize {
        self.selected_index
    }

    /// 获取指定索引条目的勾选状态
    pub fn is_checked(&self, index: usize) -> bool {
        if index < self.items.len() {
            self.items[index].checked
        } else {
            false
        }
    }

    /// 获取所有勾选状态的条目
    pub fn checked_items(&self) -> Vec<usize> {
        self.items
            .iter()
            .enumerate()
            .filter(|(_, item)| item.checked)
            .map(|(i, _)| i)
            .collect()
    }

}

impl Component for Checkbar {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        let style = self.base.current_style();
        let is_focused = self.base.has_focus;

        // 已勾选条目样式：未聚焦时用绿色，聚焦时用当前焦点前景色
        let checked_style = if is_focused {
            style
        } else {
            Style::default()
                .fg(Color::Green)
                .bg(style.bg.unwrap_or(Color::Reset))
        };

        let display_text: Vec<Line<'static>> = self
            .items
            .iter()
            .enumerate()
            .map(|(i, item)| {
                let checkbox = if item.checked { "[x]" } else { "[ ]" };
                let prefix = if i == self.selected_index { ">" } else { " " };
                let text = format!(" {} {} {}", prefix, checkbox, item.text);
                if item.checked {
                    Line::styled(text, checked_style)
                } else {
                    Line::from(text)
                }
            })
            .collect();

        let block = Block::default()
            .borders(Borders::ALL)
            .title(if self.locked { " 参数配置 🔒 " } else { " 参数配置 " })
            .style(style);

        let paragraph = Paragraph::new(display_text)
            .block(block)
            .alignment(ratatui::layout::Alignment::Left);

        paragraph.render(area, buf);
    }

    fn area(&self) -> Rect {
        self.base.area
    }

    fn set_area(&mut self, rect: Rect) {
        self.base.area = rect;
    }

    fn on_click(&mut self) -> bool {
        // 点击时切换当前选中条目的勾选状态
        self.toggle_current();
        true
    }

    fn on_key(&mut self, key: KeyCode) -> bool {
        match key {
            KeyCode::Up | KeyCode::Char('k') => {
                if self.selected_index > 0 {
                    self.selected_index -= 1;
                }
                true
            }
            KeyCode::Down | KeyCode::Char('j') => {
                if self.selected_index + 1 < self.items.len() {
                    self.selected_index += 1;
                }
                true
            }
            KeyCode::Enter | KeyCode::Char(' ') => {
                self.toggle_current();
                true
            }
            _ => false,
        }
    }

    fn focusable(&self) -> bool {
        true
    }

    fn set_focused(&mut self, focused: bool) {
        self.base.has_focus = focused;
    }

    fn component_name(&self) -> &'static str {
        "参数配置"
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }
}
