CROSS_COMPILE := aarch64-linux-gnu-

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
READELF := $(CROSS_COMPILE)readelf
NM      := $(CROSS_COMPILE)nm
OBJDUMP := $(CROSS_COMPILE)objdump

QEMU := qemu-system-aarch64

# ============================================================
# Directory layout
# ============================================================

SRC_DIR     := src
INCLUDE_DIR := include
LINKER_DIR  := linker
BUILD_DIR   := build

BOOT_SRC_DIR := $(SRC_DIR)/bootloader
KERNEL_SRC_DIR := $(SRC_DIR)/kernel

BOOT_BUILD_DIR   := $(BUILD_DIR)/bootloader
KERNEL_BUILD_DIR := $(BUILD_DIR)/kernel


# ============================================================
# Compiler flags
# ============================================================

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

# ============================================================
# Linker scripts
# ============================================================

BOOT_LINKER_SCRIPT := $(LINKER_DIR)/bootloader.ld
KERNEL_LINKER_SCRIPT := $(LINKER_DIR)/kernel.ld


# ============================================================
# Bootloader objects and outputs
# ============================================================

BOOT_OBJS := $(BOOT_BUILD_DIR)/boot.o \
			 $(BOOT_BUILD_DIR)/main.o \
			 $(BOOT_BUILD_DIR)/uart.o 

BOOT_ELF := $(BOOT_BUILD_DIR)/bootloader.elf
BOOT_IMG := $(BOOT_BUILD_DIR)/bootloader.img 
BOOT_MAP := $(BOOT_BUILD_DIR)/bootloader.map 

# ============================================================
# Kernel objects and outputs
# ============================================================

KERNEL_OBJS := $(KERNEL_BUILD_DIR)/boot.o \
               $(KERNEL_BUILD_DIR)/kernel.o \
               $(KERNEL_BUILD_DIR)/uart.o \
               $(KERNEL_BUILD_DIR)/shell.o \
               $(KERNEL_BUILD_DIR)/mailbox.o

KERNEL_ELF := $(KERNEL_BUILD_DIR)/kernel8.elf
KERNEL_IMG := $(KERNEL_BUILD_DIR)/kernel8.img
KERNEL_MAP := $(KERNEL_BUILD_DIR)/kernel.map

# ============================================================
# Main targets
# ============================================================

.PHONY: all bootloader kernel run \
        inspect inspect-bootloader inspect-kernel clean

all: bootloader kernel

bootloader: $(BOOT_IMG)

kernel: $(KERNEL_IMG)


# ============================================================
# Build directories
# ============================================================

$(BOOT_BUILD_DIR):
	mkdir -p $@

$(KERNEL_BUILD_DIR):
	mkdir -p $@

# ============================================================
# Bootloader compilation
# ============================================================

# src/bootloader/boot.S -> build/bootloader/boot.o
$(BOOT_BUILD_DIR)/boot.o: $(BOOT_SRC_DIR)/boot.S | $(BOOT_BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# src/bootloader/main.c -> build/bootloader/main.o
$(BOOT_BUILD_DIR)/main.o: \
	$(BOOT_SRC_DIR)/main.c \
	$(INCLUDE_DIR)/uart.h \
	| $(BOOT_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/uart.c -> build/bootloader/uart.o
$(BOOT_BUILD_DIR)/uart.o: \
	$(SRC_DIR)/uart.c \
	$(INCLUDE_DIR)/uart.h \
	| $(BOOT_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# Bootloader linking
# ============================================================

$(BOOT_ELF): $(BOOT_OBJS) $(BOOT_LINKER_SCRIPT)
	$(LD) \
		-T $(BOOT_LINKER_SCRIPT) \
		-Map=$(BOOT_MAP) \
		-o $@ \
		$(BOOT_OBJS)

$(BOOT_IMG): $(BOOT_ELF)
	$(OBJCOPY) -O binary $< $@



# ============================================================
# Kernel compilation
# ============================================================

# src/kernel/boot.S -> build/kernel/boot.o
$(KERNEL_BUILD_DIR)/boot.o: $(KERNEL_SRC_DIR)/boot.S | $(KERNEL_BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# src/kernel/kernel.c -> build/kernel/kernel.o
$(KERNEL_BUILD_DIR)/kernel.o: \
	$(KERNEL_SRC_DIR)/kernel.c \
	$(INCLUDE_DIR)/uart.h \
	$(INCLUDE_DIR)/shell.h \
	$(INCLUDE_DIR)/mailbox.h \
	| $(KERNEL_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/uart.c -> build/kernel/uart.o
$(KERNEL_BUILD_DIR)/uart.o: \
	$(SRC_DIR)/uart.c \
	$(INCLUDE_DIR)/uart.h \
	| $(KERNEL_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/kernel/shell.c -> build/kernel/shell.o
$(KERNEL_BUILD_DIR)/shell.o: \
	$(KERNEL_SRC_DIR)/shell.c \
	$(INCLUDE_DIR)/shell.h \
	$(INCLUDE_DIR)/uart.h \
	| $(KERNEL_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# src/kernel/mailbox.c -> build/kernel/mailbox.o
$(KERNEL_BUILD_DIR)/mailbox.o: \
	$(KERNEL_SRC_DIR)/mailbox.c \
	$(INCLUDE_DIR)/mailbox.h \
	| $(KERNEL_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


# ============================================================
# Kernel linking
# ============================================================

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_LINKER_SCRIPT)
	$(LD) \
		-T $(KERNEL_LINKER_SCRIPT) \
		-Map=$(KERNEL_MAP) \
		-o $@ \
		$(KERNEL_OBJS)

$(KERNEL_IMG): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@


# ============================================================
# QEMU
# ============================================================

run: $(BOOT_IMG)
	$(QEMU) \
		-M raspi3b \
		-kernel $(BOOT_IMG) \
		-display none \
		-serial null \
		-serial pty

# ============================================================
# Inspection
# ============================================================

inspect: inspect-bootloader inspect-kernel

inspect-bootloader: $(BOOT_ELF)
	$(READELF) -h $(BOOT_ELF)
	$(READELF) -S $(BOOT_ELF)
	$(NM) -n $(BOOT_ELF)
	$(OBJDUMP) -d $(BOOT_ELF)

inspect-kernel: $(KERNEL_ELF)
	$(READELF) -h $(KERNEL_ELF)
	$(READELF) -S $(KERNEL_ELF)
	$(NM) -n $(KERNEL_ELF)
	$(OBJDUMP) -d $(KERNEL_ELF)


# ============================================================
# Clean
# ============================================================

clean:
	rm -rf $(BUILD_DIR)