use crossterm::event::KeyCode;
use ratatui::{
    buffer::Buffer,
    layout::{Alignment, Rect},
    style::{Color, Modifier, Style},
    widgets::{Block, Borders, Paragraph, Widget},
};

use super::component::{BaseComponent, Component};
use std::any::Any;

/// 按钮组件
pub struct Button {
    /// 基础组件（复用公共字段和方法）
    base: BaseComponent,
    /// 按钮标签
    label: String,
    /// 点击回调函数
    on_click_cb: Option<Box<dyn FnMut()>>,
    /// 是否禁用（禁用后不可交互、不可聚焦）
    disabled: bool,
}

impl Button {
    /// 创建一个新的按钮
    pub fn new(label: &str) -> Self {
        let mut base = BaseComponent::new(label);
        base.set_text(label);
        base.set_style(
            Style::default()
                .fg(Color::White)
                .bg(Color::DarkGray),
        );
        base.set_focus_style(
            Style::default()
                .fg(Color::Yellow)
                .bg(Color::DarkGray)
                .add_modifier(Modifier::BOLD),
        );

        Self {
            base,
            label: label.to_string(),
            on_click_cb: None,
            disabled: false,
        }
    }

    /// 设置点击回调函数
    pub fn set_on_click<F: FnMut() + 'static>(&mut self, callback: F) {
        self.on_click_cb = Some(Box::new(callback));
    }

    /// 获取按钮标签
    pub fn label(&self) -> &str {
        &self.label
    }

    /// 设置按钮标签
    pub fn set_label(&mut self, label: &str) {
        self.label = label.to_string();
        self.base.set_text(label);
    }

    /// 获取当前样式（根据焦点状态）
    fn current_style(&self) -> Style {
        self.base.current_style()
    }

    /// 设置按钮样式
    pub fn set_style(&mut self, style: Style) {
        self.base.set_style(style);
    }

    /// 设置焦点样式
    pub fn set_focus_style(&mut self, style: Style) {
        self.base.set_focus_style(style);
    }

    /// 设置禁用状态
    pub fn set_disabled(&mut self, disabled: bool) {
        self.disabled = disabled;
        if disabled {
            self.base.has_focus = false;
        }
    }

    /// 是否被禁用
    pub fn is_disabled(&self) -> bool {
        self.disabled
    }
}

/// 辅助函数：渲染按钮到缓冲区
fn render_button(base: &BaseComponent, area: Rect, buf: &mut Buffer) {
    let block = Block::default()
        .borders(Borders::ALL)
        .style(base.current_style());

    let paragraph = Paragraph::new(base.text.clone())
        .block(block)
        .alignment(Alignment::Center);

    paragraph.render(area, buf);
}

impl Component for Button {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        let block = Block::default()
            .borders(Borders::ALL)
            .style(self.base.current_style());

        let paragraph = Paragraph::new(self.base.text.clone())
            .block(block)
            .alignment(Alignment::Center);

        paragraph.render(area, buf);
    }

    fn area(&self) -> Rect {
        self.base.area
    }

    fn set_area(&mut self, rect: Rect) {
        self.base.area = rect;
    }

    fn on_click(&mut self) -> bool {
        if self.disabled {
            return false;
        }
        if let Some(ref mut cb) = self.on_click_cb {
            cb();
            true
        } else {
            false
        }
    }

    fn on_key(&mut self, key: KeyCode) -> bool {
        if self.disabled {
            return false;
        }
        match key {
            KeyCode::Enter | KeyCode::Char(' ') => {
                // 回车或空格触发按钮点击
                self.on_click()
            }
            _ => false,
        }
    }

    fn focusable(&self) -> bool {
        !self.disabled
    }

    fn set_focused(&mut self, focused: bool) {
        self.base.has_focus = focused;
    }

    fn is_button(&self) -> bool {
        true
    }

    fn component_name(&self) -> &'static str {
        "按钮"
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }
}
