# SakuraCom

串口调试工具，由 C 语言串口通信后端（Ccom）与 Rust TUI 前端（RustUI）组成，通过管道（pipe）进行进程间通信。


## 依赖

### Ccom（C 语言后端）

| 依赖 | 说明 | 安装方式 |
|------|------|---------|
| [libserialport](https://sigrok.org/wiki/Libserialport) | 跨平台串口通信库 | `brew install libserialport` |
| pthread | 多线程支持 | 系统自带 |

### RustUI（Rust 前端）

| 依赖 | 版本 | 说明 |
|------|------|------|
| [ratatui](https://ratatui.rs/) | 0.28 | 终端 UI 框架 |
| [crossterm](https://github.com/crossterm-rs/crossterm) | 0.28 | 跨平台终端控制 |
| [libc](https://crates.io/crates/libc) | 0.2 | C 语言 FFI 绑定 |

## 构建

### 前置条件

- GCC / Clang
- Rust 工具链（rustc、cargo）
- libserialport
- pkg-config

```bash
# macOS
brew install libserialport pkg-config

# 安装 Rust（如未安装）
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### 编译

```bash
# 编译全部（Ccom + RustUI）
make

# 清理构建产物
make clean
```

## 运行

```bash
./sakuraCom [-extra] [-log]
```

| 选项 | 说明 |
|------|------|
| `-extra` | 调试模式：在独立的 Terminal.app 窗口中启动 RustUI 图形化界面，通过 FIFO（命名管道）通信，方便分离观察后端日志和前端 UI |
| `-log` | 日志模式：将串口收发的原始数据（十六进制）和 ASCII 数据保存为带时间戳的日志文件（`YYYYMMDD_HHMMSS_send.log` / `YYYYMMDD_HHMMSS_recv.log`） |

普通模式（无参数）下，Ccom 通过匿名管道 fork 并 exec RustUI 子进程，在同一终端内运行 TUI 界面。

