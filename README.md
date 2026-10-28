# SakuraCom

SakuraCom 是一个运行在终端中的串口调试工具，使用 C++20、FTXUI 和
libserialport 实现。它提供串口参数配置、数据收发、ASCII/HEX 显示切换、
输出滚动和清空等常用功能。

项目已完成从 C/Rust 双进程架构到 C++ 单进程架构的重构。串口调试的主要
交互已经恢复，**日志保存功能尚未迁移到当前版本**。

## 功能状态

| 功能 | 状态 | 说明 |
|------|------|------|
| 串口发现与刷新 | 已完成 | 自动刷新可用串口，并尽量保留当前选择 |
| 串口开关 | 已完成 | 支持打开、关闭串口 |
| 串口参数配置 | 已完成 | 波特率、数据位、校验位、停止位 |
| ASCII 数据发送 | 已完成 | 输入文本后发送原始字节 |
| HEX 数据发送 | 已完成 | 输入十六进制文本，转换为原始字节发送 |
| ASCII 数据接收 | 已完成 | 实时显示接收内容 |
| HEX 数据接收 | 已完成 | 支持按 `0A` 换行和连续排列 |
| 输出滚动与清空 | 已完成 | 键盘和鼠标滚轮均可滚动 |
| 保存收发日志 | **待实现** | 当前没有 `-log` 参数或日志文件输出 |
| 双进程调试模式 | 已废弃 | 单进程 TUI 不再需要旧版 `-extra` 模式 |

## 环境要求

| 依赖 | 要求 |
|------|------|
| CMake | 3.20 或更高版本 |
| C++ 编译器 | 支持 C++20 |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | 提供 CMake package config |
| [libserialport](https://sigrok.org/wiki/Libserialport) | 串口访问 |
| pkg-config | 用于发现 libserialport |
| 操作系统 | 当前代码使用 POSIX API，面向 macOS/Linux |

### macOS

```bash
brew install cmake ftxui libserialport pkg-config
```

### Linux

需要安装 CMake、C++ 编译器、pkg-config、libserialport 开发包，以及能够被
`find_package(ftxui CONFIG REQUIRED)` 找到的 FTXUI。不同发行版的包名可能
不同，请以本机发行版提供的软件包为准。

## 构建

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build -j
```

构建成功后生成可执行文件：

```text
build/sakuraCom
```

如需从头构建，可以删除生成目录后重新执行上面的命令：

```bash
rm -rf build
cmake -S . -B build
cmake --build build -j
```

`build/` 是生成目录，不应提交到 Git。

## 运行

```bash
./build/sakuraCom
```

程序会进入全屏终端界面。建议在足够大的终端窗口中使用，以便同时显示串口
配置、发送区和接收区。

当前版本不接收命令行参数：

- 旧版的 `-log` 日志模式尚未实现。
- 旧版的 `-extra` 调试模式属于 C/Rust 双进程架构，单进程版本中已不再需要。

## 使用方法

1. 在顶部选择波特率、数据位、校验位和停止位。
2. 在串口列表中选择设备，然后选择“打开串口”。
3. 在输入区选择 `Ascii` 或 `Hex` 模式并输入数据。
4. 选择“发送”，数据会通过已打开的串口写出。
5. 在输出区选择 `Ascii` 或 `Hex` 查看接收数据。
6. 使用“清空”按钮清除输入区或输出区内容。
7. 选择“关闭串口”断开设备，或按 `Ctrl+C` 退出程序。

### 常用操作

| 操作 | 按键或方式 |
|------|------------|
| 切换焦点 | `Tab` / `Shift+Tab` |
| 选择菜单项 | 方向键 |
| 确认按钮或选项 | `Enter` |
| 向上滚动输出 | 输出区获得焦点时按 `W` / `w` |
| 向下滚动输出 | 输出区获得焦点时按 `S` / `s` |
| 滚动输入或输出区 | 鼠标滚轮 |
| 退出程序 | `Ctrl+C` |

### HEX 输入

HEX 输入只保留 `0-9`、`a-f`、`A-F` 和换行符，发送前会将其转换为原始字节。
例如：

```text
48 65 6C 6C 6F 0A
```

会发送 `Hello\n` 对应的字节序列。输入区中的空白字符不会作为数据发送。

### HEX 输出

输出区选择 `Hex` 后，可以选择：

- `0A换行`：以接收数据中的 `0A` 为换行依据显示。
- `不换行`：将全部接收数据连续转换为 HEX，并按照终端宽度排列。

## 串口权限

### macOS

通常应选择 `/dev/cu.*` 设备。如果串口没有出现在列表中，请检查 USB 转串口
驱动是否安装、数据线是否连接，以及设备是否被其他程序占用。

### Linux

常见设备名为 `/dev/ttyUSB*` 或 `/dev/ttyACM*`。如果当前用户无权访问设备，
请确认用户属于相应设备组，例如：

```bash
sudo usermod -aG dialout "$USER"
```

修改用户组后需要重新登录。不同发行版和设备可能使用不同的设备组。

## 项目结构

```text
.
├── CMakeLists.txt       # CMake 构建配置
├── inc/
│   ├── serial.hpp       # 串口封装接口
│   └── ui.hpp           # FTXUI 组件接口
├── src/
│   ├── main.cpp         # 程序入口、布局与事件循环
│   ├── serial.cpp       # libserialport 封装和非阻塞读写
│   └── ui.cpp           # 串口菜单、输入区和输出区
├── build/               # 本地构建产物，不提交
├── Makefile             # 本地便捷构建命令
└── README.md
```

## 实现说明

SakuraCom 使用单进程架构：

- `main.cpp` 创建 FTXUI 全屏界面和组件树。
- 工作线程每 100 ms 刷新一次串口列表并触发界面事件。
- UI 事件循环从已打开的串口读取当前可用数据。
- `serial.cpp` 使用 libserialport 设置串口参数，并通过
  `fcntl` 将文件描述符设置为非阻塞模式。
- `ui.cpp` 负责串口配置菜单、ASCII/HEX 输入输出、滚动和清空。

旧版 C/Rust 双进程实现保存在 Git 标签 `v0.1-c-rust` 中。该版本通过管道在
C 后端与 Rust TUI 前端之间通信，并包含 `-extra` 和 `-log` 参数。

## 已知限制

- 尚未实现日志保存，也没有对应的时间戳日志文件。
- 当前没有自动化单元测试；CTest 中注册的 smoke test 会启动交互式终端程序，
  不适合在无终端环境中执行。
- 尚未验证 Windows 构建。
- 界面布局按照终端尺寸动态计算，过小的终端可能导致内容显示不完整。

## 后续计划

日志保存是重构后尚未迁移的主要功能。后续实现时应继续沿用旧版约定的收发
日志形式：

```text
YYYYMMDD_HHMMSS_send.log
YYYYMMDD_HHMMSS_recv.log
```

日志内容应同时包含原始数据的十六进制表示和可读的 ASCII 表示，写入按时间戳
命名的日志文件。

## 参考资料

- [FTXUI 官方文档](https://arthursonzogni.github.io/FTXUI/)
- [FTXUI 简体中文文档](https://arthursonzogni.github.io/FTXUI/zh-CH/index.html)
- [libserialport 项目与文档](https://sigrok.org/wiki/Libserialport)
- [libserialport API 文档](https://sigrok.org/api/libserialport/unstable/index.html)
