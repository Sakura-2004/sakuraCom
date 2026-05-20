# Ccom - Serial port debugging tool
# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -I./Ccom/inc $(shell pkg-config --cflags libserialport)
LDFLAGS = $(shell pkg-config --libs libserialport)

# Directories
SRC_DIR = Ccom/src
INC_DIR = Ccom/inc
BUILD_DIR = CcomCompile

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Target (output to root directory)
TARGET = sakuraCom

.PHONY: all clean

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)