# SakuraCom - Serial port debugging tool
# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -I./Ccom/inc $(shell pkg-config --cflags libserialport)
LDFLAGS = $(shell pkg-config --libs libserialport) -lpthread

# Directories
SRC_DIR = Ccom/src
INC_DIR = Ccom/inc
BUILD_DIR = CcomCompile

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Targets
TARGET = sakuraCom
RUST_TARGET = UITest
RUSTUI_OUT = RustUI/UI

.PHONY: all rust ccom clean

# 默认目标：编译 RustUI 为 RustUI/UI，同时编译 Ccom 为 sakuraCom
all: $(RUSTUI_OUT) $(TARGET)
	rm -rf $(BUILD_DIR)

# 编译 RustUI 项目，生成 RustUI/UI 可执行文件
$(RUSTUI_OUT):
	cd RustUI && cargo build --release
	cp RustUICompile/release/RustUI $(RUSTUI_OUT)

# 仅编译 Ccom 部分
ccom: $(BUILD_DIR) $(TARGET)
	rm -rf $(BUILD_DIR)

# 仅编译 RustUI，输出到当前目录下的 UITest（保留原有接口）
rust:
	cd RustUI && cargo build --release
	cp RustUICompile/release/RustUI ./$(RUST_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(RUST_TARGET) $(RUSTUI_OUT)
	rm -rf RustUICompile
