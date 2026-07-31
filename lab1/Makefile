CROSS_COMPILE := aarch64-linux-gnu-

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
READELF := $(CROSS_COMPILE)readelf
NM      := $(CROSS_COMPILE)nm
OBJDUMP := $(CROSS_COMPILE)objdump

# Directory layout
SRC_DIR     := src
INCLUDE_DIR := include
LINKER_DIR  := linker
BUILD_DIR   := build

# -Wall:
#   啟用常用警告。
#
# -Wextra:
#   啟用額外警告。
#
# -ffreestanding:
#   表示這不是由一般作業系統與完整 C runtime 支援的 hosted 程式。
#
# -fno-stack-protector:
#   避免 compiler 插入目前尚未提供的 stack protector runtime symbols。
#
# -fno-pie / -fno-pic:
#   關閉 position-independent executable/code。
#
# -I$(INCLUDE_DIR):
#   讓 #include "uart.h" 可以從 include/ 找到 uart.h。

CFLAGS := -Wall -Wextra \
          -O0 -g \
          -ffreestanding \
          -fno-stack-protector \
          -fno-pie \
          -fno-pic \
          -I$(INCLUDE_DIR)

ASFLAGS := -g

LINKER_SCRIPT := $(LINKER_DIR)/linker.ld

LDFLAGS := -T $(LINKER_SCRIPT) \
           -Map=$(BUILD_DIR)/kernel.map

OBJS := $(BUILD_DIR)/boot.o \
        $(BUILD_DIR)/kernel.o \
        $(BUILD_DIR)/uart.o \
		$(BUILD_DIR)/shell.o \
		$(BUILD_DIR)/mailbox.o

KERNEL_ELF := $(BUILD_DIR)/kernel8.elf
KERNEL_IMG := $(BUILD_DIR)/kernel8.img

.PHONY: all inspect clean

all: $(KERNEL_IMG)

# 建立 build/ 資料夾。
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# src/boot.S -> build/boot.o
$(BUILD_DIR)/boot.o: $(SRC_DIR)/boot.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# src/uart.c -> build/uart.o
$(BUILD_DIR)/uart.o: $(SRC_DIR)/uart.c $(INCLUDE_DIR)/uart.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/shell.c -> build/shell.o
$(BUILD_DIR)/shell.o:  $(SRC_DIR)/shell.c $(INCLUDE_DIR)/shell.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/mailbox.c -> build/mailbox.o
$(BUILD_DIR)/mailbox.o: $(SRC_DIR)/mailbox.c $(INCLUDE_DIR)/mailbox.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/kernel.c -> build/kernel.o
$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel.c $(INCLUDE_DIR)/uart.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# object files -> build/kernel8.elf
$(KERNEL_ELF): $(OBJS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)


# build/kernel8.elf -> build/kernel8.img
$(KERNEL_IMG): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@


# 查看 ELF header、sections、symbols 與反組譯結果。
inspect: $(KERNEL_ELF)
	$(READELF) -h $(KERNEL_ELF)
	$(READELF) -S $(KERNEL_ELF)
	$(NM) -n $(KERNEL_ELF)
	$(OBJDUMP) -d $(KERNEL_ELF)


clean:
	rm -rf $(BUILD_DIR)