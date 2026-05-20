use crossterm::event::KeyCode;
use ratatui::{
    buffer::Buffer,
    layout::Rect,
    style::Style,
    widgets::{Block, Borders, Paragraph, Widget},
};

use super::component::{BaseComponent, Component};
use std::any::Any;

/// 下拉选择菜单组件（水平导航模式，单行显示，不展开）
pub struct Dropdown {
    /// 基础组件
    base: BaseComponent,
    /// 选项列表（变长数组）
    options: Vec<String>,
    /// 当前选中项的索引
    selected_index: usize,
    /// 是否锁定（锁定后不可修改选择）
    locked: bool,
    /// 选择变更回调
    on_select_cb: Option<Box<dyn FnMut(usize, &str)>>,
    /// 组件标题（如 "串口选择"、"校验位" 等）
    title: String,
}

impl Dropdown {
    /// 创建一个新的下拉菜单
    pub fn new(options: Vec<&str>, title: &str) -> Self {
        let label = if options.is_empty() {
            "(empty)".to_string()
        } else {
            format!("▸ [1/{}] {}", options.len(), options[0])
        };
        let base = BaseComponent::new(&label);

        Self {
            base,
            options: options.iter().map(|s| s.to_string()).collect(),
            selected_index: 0,
            locked: false,
            on_select_cb: None,
            title: title.to_string(),
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

    /// 获取选项总数
    pub fn option_count(&self) -> usize {
        self.options.len()
    }

    /// 设置样式
    pub fn set_style(&mut self, style: Style) {
        self.base.set_style(style);
    }

    /// 设置焦点样式
    pub fn set_focus_style(&mut self, style: Style) {
        self.base.set_focus_style(style);
    }

    /// 设置选项列表（替换全部内容）
    pub fn set_options(&mut self, options: Vec<&str>) {
        if self.locked {
            return;
        }
        self.options = options.iter().map(|s| s.to_string()).collect();
        if self.selected_index >= self.options.len() && !self.options.is_empty() {
            self.selected_index = 0;
        }
        self.update_label();
    }

    /// 添加一个选项到末尾
    pub fn add_option(&mut self, text: &str) {
        if self.locked {
            return;
        }
        self.options.push(text.to_string());
        self.update_label();
    }

    /// 在指定位置插入一个选项
    pub fn insert_option(&mut self, index: usize, text: &str) {
        if self.locked {
            return;
        }
        let index = index.min(self.options.len());
        self.options.insert(index, text.to_string());
        if self.selected_index >= index {
            self.selected_index += 1;
        }
        self.update_label();
    }

    /// 删除指定位置的选项
    pub fn remove_option(&mut self, index: usize) -> bool {
        if self.locked {
            return false;
        }
        if index >= self.options.len() {
            return false;
        }
        self.options.remove(index);
        if self.selected_index >= self.options.len() && !self.options.is_empty() {
            self.selected_index = self.options.len() - 1;
        }
        self.update_label();
        true
    }

    /// 清空所有选项
    pub fn clear_options(&mut self) {
        if self.locked {
            return;
        }
        self.options.clear();
        self.selected_index = 0;
        self.update_label();
    }

    /// 获取所有选项的引用
    pub fn options(&self) -> &[String] {
        &self.options
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

    /// 更新显示文本
    fn update_label(&mut self) {
        if self.options.is_empty() {
            self.base.set_text("(empty)");
        } else {
            let total = self.options.len();
            let text = format!("▸ [{}/{}] {}", self.selected_index + 1, total, self.options[self.selected_index]);
            self.base.set_text(&text);
        }
    }

    /// 构建块标题（包含 标题 [当前/总数]）
    fn build_block_title(&self) -> String {
        if self.options.is_empty() {
            format!(" {} [0/0] ", self.title)
        } else {
            format!(" {} [{}/{}] ", self.title, self.selected_index + 1, self.options.len())
        }
    }

    /// 选择下一项
    fn select_next(&mut self) {
        if self.locked || self.options.is_empty() {
            return;
        }
        self.selected_index = (self.selected_index + 1) % self.options.len();
        self.update_label();
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
        self.update_label();
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

impl Component for Dropdown {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        let style = self.base.current_style();

        let title = if self.locked {
            format!(" {} 🔒", self.build_block_title().trim())
        } else {
            self.build_block_title()
        };

        let block = Block::default()
            .borders(Borders::ALL)
            .title(title)
            .style(style);

        let display_text = if self.options.is_empty() {
            "(empty)".to_string()
        } else {
            self.options[self.selected_index].clone()
        };

        let paragraph = Paragraph::new(display_text)
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
        self.select_next();
        true
    }

    fn on_key(&mut self, key: KeyCode) -> bool {
        match key {
            KeyCode::Right | KeyCode::Char('l') => {
                self.select_next();
                true
            }
            KeyCode::Left | KeyCode::Char('h') => {
                self.select_prev();
                true
            }
            KeyCode::Enter | KeyCode::Char(' ') => {
                self.select_next();
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
        "Dropdown"
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }
}
