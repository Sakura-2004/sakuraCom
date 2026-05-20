use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::Style,
    text::Line,
};
use std::any::Any;

/// 组件特征：所有 UI 组件必须实现的接口
pub trait Component: Any {
    /// 渲染组件到缓冲区
    fn render(&self, area: Rect, buf: &mut Buffer);

    /// 获取组件在屏幕上的区域
    fn area(&self) -> Rect;

    /// 设置组件区域（由布局调用）
    fn set_area(&mut self, rect: Rect);

    /// 处理鼠标点击事件
    fn on_click(&mut self) -> bool;

    /// 处理键盘事件
    fn on_key(&mut self, key: crossterm::event::KeyCode) -> bool;

    /// 处理鼠标滚轮事件（up=true 向上滚动，false 向下滚动）
    fn on_scroll(&mut self, _up: bool) -> bool {
        false
    }

    /// 是否可获取焦点（用于 Tab 切换）
    fn focusable(&self) -> bool;

    /// 设置焦点状态
    fn set_focused(&mut self, focused: bool);

    /// 组件名称（中文描述，如 "串口选择"、"波特率" 等）
    fn component_name(&self) -> &'static str;

    /// 获取可聚焦状态标签
    fn focus_label(&self) -> &'static str {
        if self.focusable() {
            "[可聚焦]"
        } else {
            "[只读]"
        }
    }

    /// 是否是按钮类型组件（按钮点击始终触发动作，其他组件先选中再触发）
    fn is_button(&self) -> bool {
        false
    }

    /// 向下转型到 Any，支持按具体类型访问
    fn as_any(&self) -> &dyn Any;

    /// 向下转型到 Any (mut)
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// 基础组件：封装了所有组件的公共状态
/// 其他具体组件（Button、Checkbar 等）通过组合 BaseComponent 来复用代码
pub struct BaseComponent {
    /// 组件占用的屏幕区域
    pub area: Rect,
    /// 组件的显示文本行
    pub text: Vec<Line<'static>>,
    /// 常规样式
    pub style: Style,
    /// 获得焦点时的样式
    pub focus_style: Style,
    /// 是否持有焦点
    pub has_focus: bool,
}

impl BaseComponent {
    /// 创建一个新的基础组件
    pub fn new(text: &str) -> Self {
        Self {
            area: Rect::default(),
            text: vec![Line::from(text.to_string())],
            style: Style::default(),
            focus_style: Style::default(),
            has_focus: false,
        }
    }

    /// 设置显示文本
    pub fn set_text(&mut self, text: &str) {
        self.text = vec![Line::from(text.to_string())];
    }

    /// 设置多行显示文本
    pub fn set_lines(&mut self, lines: Vec<Line<'static>>) {
        self.text = lines;
    }

    /// 设置样式
    pub fn set_style(&mut self, style: Style) {
        self.style = style;
    }

    /// 设置焦点样式
    pub fn set_focus_style(&mut self, style: Style) {
        self.focus_style = style;
    }

    /// 获取当前生效的样式（根据焦点状态）
    pub fn current_style(&self) -> Style {
        if self.has_focus {
            self.focus_style
        } else {
            self.style
        }
    }

    /// 在指定区域渲染文本（供子组件调用）
    pub fn render_text(&self, area: Rect, buf: &mut Buffer) {
        use ratatui::widgets::{Paragraph, Widget, Wrap};
        use ratatui::layout::Alignment;
        let paragraph = Paragraph::new(self.text.clone())
            .style(self.current_style())
            .alignment(Alignment::Center)
            .wrap(Wrap { trim: false });
        paragraph.render(area, buf);
    }
}