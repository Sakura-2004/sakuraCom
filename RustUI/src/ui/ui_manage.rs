use crossterm::event::{Event, KeyCode, MouseButton, MouseEventKind, MouseEvent};
use ratatui::{
    Frame,
    layout::Position,
};

use super::component::Component;

/// 事件调度器：管理所有注册的组件，负责事件分发
pub struct Scheduler {
    /// 注册的组件列表（按注册顺序）
    components: Vec<Box<dyn Component>>,
    /// 当前获得键盘焦点的组件索引
    focus_index: usize,
}

impl Scheduler {
    /// 创建一个新的调度器
    pub fn new() -> Self {
        Self {
            components: Vec::new(),
            focus_index: 0,
        }
    }

    /// 注册一个组件到调度器中
    pub fn register(&mut self, component: Box<dyn Component>) -> &mut Self {
        self.components.push(component);
        self
    }

    /// 获取所有组件的可变引用（供 ui() 设置 area 用）
    pub fn components_mut(&mut self) -> &mut [Box<dyn Component>] {
        &mut self.components
    }

    /// 渲染所有组件
    pub fn render_all(&mut self, f: &mut Frame) {
        let buf = f.buffer_mut();
        for comp in &mut self.components {
            comp.sync();
            comp.render(comp.area(), buf);
        }
    }

    /// 获取 OutputBox 组件的可变引用（索引已知时）
    pub fn get_component(&mut self, index: usize) -> Option<&mut Box<dyn Component>> {
        self.components.get_mut(index)
    }

    /// 获取指定索引组件的向下转型引用
    pub fn get_component_downcast<T: 'static>(&mut self, index: usize) -> Option<&mut T> {
        self.components.get_mut(index)
            .and_then(|c| c.as_any_mut().downcast_mut::<T>())
    }

    /// 处理事件，返回 true 表示事件已被消费
    pub fn handle_event(&mut self, event: &Event) -> bool {
        match event {
            Event::Mouse(mouse) => self.handle_mouse(mouse),
            Event::Key(key) => self.handle_key(key.code),
            _ => false,
        }
    }

    /// 处理鼠标事件（包括点击和滚轮）
    fn handle_mouse(&mut self, mouse: &MouseEvent) -> bool {
        // 处理滚轮事件：向上滚动 or 向下滚动
        if mouse.kind == MouseEventKind::ScrollDown || mouse.kind == MouseEventKind::ScrollUp {
            let pos = Position::new(mouse.column, mouse.row);
            let up = mouse.kind == MouseEventKind::ScrollUp;
            // 从后往前遍历，优先传递给上层组件
            for i in (0..self.components.len()).rev() {
                if self.components[i].area().contains(pos) {
                    return self.components[i].on_scroll(up);
                }
            }
            return false;
        }

        // 非左键按下不处理
        if mouse.kind != MouseEventKind::Down(MouseButton::Left) {
            return false;
        }
        let pos = Position::new(mouse.column, mouse.row);

        // 从后往前遍历（上层组件优先响应）
        for i in (0..self.components.len()).rev() {
            if self.components[i].area().contains(pos) {
                // 按钮点击始终触发动作
                if self.components[i].is_button() {
                    if self.components[i].focusable() {
                        self.set_focus(i);
                    }
                    return self.components[i].on_click();
                }

                // 非按钮组件：第一次点击只选中（设置焦点），第二次点击才触发动作
                let already_focused = self.focus_index == i && self.components[i].focusable();
                if self.components[i].focusable() {
                    self.set_focus(i);
                }
                if already_focused {
                    return self.components[i].on_click();
                }
                return true; // 事件已消费（选中了该组件）
            }
        }
        false
    }

    /// 处理键盘事件
    fn handle_key(&mut self, key: KeyCode) -> bool {
        match key {
            KeyCode::Tab => {
                // Tab 键：切换到下一个可聚焦的组件
                self.focus_next();
                true
            }
            KeyCode::BackTab => {
                // Shift+Tab：切换到上一个可聚焦的组件
                self.focus_prev();
                true
            }
            // Up / Down 键优先路由到当前焦点组件（用于滚动）
            // 如果当前组件没处理，则切换到上下组件
            KeyCode::Up | KeyCode::Down => {
                if !self.components.is_empty() {
                    let idx = self.focus_index.min(self.components.len() - 1);
                    if self.components[idx].on_key(key) {
                        return true;
                    }
                }
                false
            }
            _ => {
                // 其他按键：路由到当前焦点组件
                if !self.components.is_empty() {
                    let idx = self.focus_index.min(self.components.len() - 1);
                    self.components[idx].on_key(key)
                } else {
                    false
                }
            }
        }
    }

    /// 设置焦点到指定索引的组件
    fn set_focus(&mut self, index: usize) {
        // 清除旧的焦点
        if self.focus_index < self.components.len() {
            self.components[self.focus_index].set_focused(false);
        }
        // 设置新的焦点
        self.focus_index = index;
        if index < self.components.len() {
            self.components[index].set_focused(true);
        }
    }

    /// 切换到下一个可聚焦的组件
    fn focus_next(&mut self) {
        if self.components.is_empty() {
            return;
        }
        let len = self.components.len();
        for i in 1..=len {
            let idx = (self.focus_index + i) % len;
            if self.components[idx].focusable() {
                self.set_focus(idx);
                return;
            }
        }
    }

    /// 切换到上一个可聚焦的组件
    fn focus_prev(&mut self) {
        if self.components.is_empty() {
            return;
        }
        let len = self.components.len();
        for i in 1..=len {
            let idx = (self.focus_index + len - i) % len;
            if self.components[idx].focusable() {
                self.set_focus(idx);
                return;
            }
        }
    }
}