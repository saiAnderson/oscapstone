# Lab 2 - UART Bootloader

## 目標

本 Lab 實作一個簡單的 **UART Bootloader**。

Bootloader 啟動後會：

1. 初始化 UART。
2. 等待 Host 端傳送 `kernel8.img`。
3. 先接收 kernel image 的大小。
4. 透過 UART 接收完整的 kernel binary。
5. 將 kernel 寫入指定的 RAM address `0x80000`。
6. 跳到 `0x80000`，開始執行 kernel。
7. Kernel 啟動後，再透過 UART 與 shell 互動。

在 QEMU 中，Host 與 Guest UART 之間使用 **Pseudo Terminal (PTY)** 模擬實體 UART 連線。

---

# 1. Raspberry Pi 3 Boot Flow

## 1.1 實體 Raspberry Pi 3

Raspberry Pi 3 上電後，大致經過：

```text
Power On
   ↓
Boot ROM
   ↓
bootcode.bin
   ↓
初始化 SDRAM
   ↓
start.elf
   ↓
讀取 config.txt
   ↓
載入指定 image
   ↓
啟動 ARM CPU
```

剛上電時 SDRAM 尚未初始化，因此不能直接把大型 kernel 放到 SDRAM 執行。

第一階段程式會先利用 SoC 內可直接使用的 ROM / cache 等資源，接著 `bootcode.bin` 完成 SDRAM 初始化。

SDRAM 可以使用後，`start.elf` 再根據 `config.txt` 載入下一個 image。

若要讓自己的 bootloader 位於 `0x60000`，實體 Raspberry Pi 可以設定：

```text
kernel_address=0x60000
kernel=bootloader.img
arm_64bit=1
```

注意：

> 不需要修改 `start.elf` 本身，只需要透過 `config.txt` 告訴 firmware 要載入哪個 image，以及載入到哪個 address。

---

# 2. QEMU 與實體 Raspberry Pi 的差異

目前實驗使用 QEMU，因此不會真的執行：

```text
Boot ROM
→ bootcode.bin
→ start.elf
```

而是直接利用 QEMU Generic Loader：

```text
bootloader.img
      ↓
load 到 0x60000
      ↓
CPU0 PC = 0x60000
      ↓
開始執行 bootloader::_start
```

QEMU command：

```bash
-device loader,file=build/bootloader/bootloader.img,addr=0x60000,cpu-num=0,force-raw=on
```

因此可以讓 QEMU 與實體板使用相同的 memory layout：

```text
0x60000
┌─────────────────────────┐
│ bootloader.img          │
└─────────────────────────┘

0x80000
┌─────────────────────────┐
│ kernel8.img             │
└─────────────────────────┘
```

這樣 bootloader 在接收 kernel 時，不會把自己覆蓋掉。

---

# 3. Project Structure

目前專案結構：

```text
.
├── Makefile
├── README.md
├── include
│   ├── mailbox.h
│   ├── shell.h
│   └── uart.h
├── linker
│   ├── bootloader.ld
│   └── kernel.ld
├── src
│   ├── bootloader
│   │   ├── boot.S
│   │   └── main.c
│   ├── kernel
│   │   ├── boot.S
│   │   ├── kernel.c
│   │   ├── mailbox.c
│   │   └── shell.c
│   └── uart.c
├── tools
│   ├── capture_pty.sh
│   └── send_kernel.py
└── build
    ├── bootloader
    │   ├── boot.o
    │   ├── main.o
    │   ├── uart.o
    │   ├── bootloader.elf
    │   └── bootloader.img
    ├── kernel
    │   ├── boot.o
    │   ├── kernel.o
    │   ├── mailbox.o
    │   ├── shell.o
    │   ├── uart.o
    │   ├── kernel8.elf
    │   └── kernel8.img
    └── uart_pty.txt
```

`uart.c` 放在：

```text
src/uart.c
```

是因為：

```text
bootloader
    ↓
需要 UART 接收 kernel

kernel
    ↓
需要 UART shell / print
```

兩邊都可以在編譯時使用同一份 source。

而兩個：

```text
src/bootloader/boot.S
src/kernel/boot.S
```

可以同名，因為最後 object 分別放在：

```text
build/bootloader/boot.o
build/kernel/boot.o
```

因此不會衝突。

---

# 4. Memory Layout

目前採用：

```text
Bootloader : 0x60000
Kernel     : 0x80000
```

概念如下：

```text
RAM

0x60000
┌─────────────────────────────┐
│ Bootloader                  │
│                             │
│ _start                      │
│ bootloader_main()           │
│ uart_recv()                 │
│ jump_to_kernel()            │
└─────────────────────────────┘


0x80000
┌─────────────────────────────┐
│ Kernel                      │
│                             │
│ kernel::_start              │
│ kernel_main()               │
│ shell                       │
└─────────────────────────────┘
```

Bootloader 與 kernel 必須放在不同位置。

如果 bootloader 和 kernel 都從：

```text
0x80000
```

開始，bootloader 在接收 kernel 時：

```text
kernel byte 0 → 0x80000
kernel byte 1 → 0x80001
...
```

就會逐漸覆蓋目前正在執行的 bootloader。

因此 Basic Exercise 採用不同 address 避免 overlap。

---

# 5. Bootloader Entry

Bootloader 的 `boot.S`：

```asm
.section ".text.bootloader", "ax"

.global _start
.extern __stack_top
.extern __bss_start
.extern __bss_end
.extern bootloader_main

_start:
    /* 取得目前 CPU core ID */
    mrs x0, mpidr_el1
    and x0, x0, #0xff

    /* 只有 Core 0 繼續執行 */
    cbz x0, primary_core

secondary_core_hang:
    wfe
    b secondary_core_hang

primary_core:
    /* 設定 stack pointer */
    ldr x12, =__stack_top
    mov sp, x12

    /* 清除 .bss */
    ldr x9, =__bss_start
    ldr x10, =__bss_end

clear_bss:
    cmp x9, x10
    b.hs clear_bss_done

    stp xzr, xzr, [x9], #16
    b clear_bss

clear_bss_done:
    bl bootloader_main

hang:
    wfe
    b hang
```

---

# 6. `.text.bootloader`

這行：

```asm
.section ".text.bootloader", "ax"
```

建立一個名稱為：

```text
.text.bootloader
```

的 section。

其中：

```text
a = allocatable
x = executable
```

表示：

- 執行時要配置到 memory。
- 裡面的內容是 executable instructions。

Linker script 再使用：

```ld
.text : {
    KEEP(*(.text.bootloader))
    *(.text .text.*)
}
```

將 boot entry code 放在 `.text` 最前面。

因此 image 被放到：

```text
0x60000
```

後，第一段 machine code 就是：

```text
_start
```

---

# 7. Bootloader Linker Script

Bootloader 從：

```text
0x60000
```

開始：

```ld
ENTRY(_start)

SECTIONS
{
    . = 0x60000;
    __bootloader_start = .;

    . = ALIGN(16);
    .text : {
        KEEP(*(.text.bootloader))
        *(.text .text.*)
    }

    . = ALIGN(16);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(16);
    .data : {
        *(.data .data.*)
    }

    . = ALIGN(16);
    .bss(NOLOAD) : {
        __bss_start = .;

        *(.bss .bss.*)
        *(COMMON)

        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(16);
    .stack (NOLOAD) : {
        __stack_bottom = .;
        . += 16K;
        __stack_top = .;
    }

    __bootloader_end = .;
}
```

`.bss` 和 `.stack` 使用：

```text
NOLOAD
```

代表這些區域需要保留 memory address，但不需要真的把大量 zero 塞入 raw image。

---

# 8. UART Bootloader Protocol

Host 與 Bootloader 必須約定傳輸格式。

目前採用最簡單的 protocol：

```text
┌───────────────────┬─────────────────────────┐
│ kernel size       │ kernel image            │
│ 4 bytes           │ N bytes                 │
└───────────────────┴─────────────────────────┘
```

流程：

```text
Python
   ↓
傳 4-byte kernel size
   ↓
Bootloader
   ↓
知道接下來需要收多少 bytes
   ↓
Python 傳 kernel8.img
```

Binary image 本身不能靠：

```text
'\0'
```

判斷結束，因為 `0x00` 本身可能就是正常 machine code 的一部分。

所以必須先傳檔案大小。

---

# 9. Kernel Size 與 Little Endian

假設：

```text
kernel8.img = 2896 bytes
```

十進位：

```text
2896
```

十六進位：

```text
0x00000B50
```

Python：

```python
kernel_size = len(kernel)
```

得到：

```text
2896
```

接著：

```python
size_bytes = struct.pack("<I", kernel_size)
```

其中：

```text
< = little endian
I = unsigned 32-bit integer
```

所以：

```text
0x00000B50
```

在 UART 上實際傳送：

```text
50 0B 00 00
```

Python 顯示可能是：

```text
b'P\x0b\x00\x00'
```

因為：

```text
0x50 = ASCII 'P'
```

---

# 10. Bootloader 接收 Kernel Size

Bootloader：

```c
uint32_t kernel_size = 0;

for (int i = 0; i < 4; i++) {
    uint8_t c = (uint8_t)uart_recv();

    kernel_size |= ((uint32_t)c << (8 * i));
}
```

假設收到：

```text
byte 0 = 0x50
byte 1 = 0x0B
byte 2 = 0x00
byte 3 = 0x00
```

則：

```text
0x50
|
0x0B << 8
|
0x00 << 16
|
0x00 << 24

=

0x00000B50
```

即：

```text
2896 bytes
```

注意：

```c
uint32_t kernel_size = 0;
```

一定要初始化為 `0`，因為後面使用的是 OR operation。

---

# 11. 接收 Kernel Image

定義 kernel loading address：

```c
#define KERNEL_ADDRESS 0x80000UL
```

然後：

```c
uint8_t *kernel = (uint8_t *)KERNEL_ADDRESS;

for (uint32_t i = 0; i < kernel_size; i++) {
    kernel[i] = (uint8_t)uart_recv();
}
```

`kernel` 是：

```c
uint8_t *
```

所以：

```text
sizeof(*kernel) = 1 byte
```

因此：

```text
kernel[0] → 0x80000
kernel[1] → 0x80001
kernel[2] → 0x80002
...
```

Pointer arithmetic 的規則為：

```text
pointer + i
=
base_address + i × sizeof(pointer 指向的型別)
```

因為這裡是 `uint8_t *`：

```text
address = 0x80000 + i
```

注意，不應寫成：

```c
kernel[(uint8_t)i]
```

因為 `uint8_t` 最大只有：

```text
255
```

當：

```text
i = 256
```

cast 後會變回：

```text
0
```

造成前面的 kernel data 被重新覆蓋。

---

# 12. Bare-Metal Direct Memory Access

這行：

```c
uint8_t *kernel = (uint8_t *)KERNEL_ADDRESS;
```

表示：

> 將 `0x80000` 視為一個指向 `uint8_t` 的 memory pointer。

因此：

```c
kernel[0] = 0x12;
```

實際上就是對：

```text
0x80000
```

進行 memory store。

在目前 bare-metal 環境中沒有一般 user-space process 的 virtual memory abstraction，因此可以根據 SoC memory map 直接操作已知有效的 system address。

這和 UART MMIO 的概念相同：

```text
RAM address
    ↓
CPU load/store

Peripheral MMIO address
    ↓
CPU load/store
```

只是 address 最後對應到不同的硬體。

---

# 13. Jump to Kernel

Kernel image 完整放到：

```text
0x80000
```

後，需要將 CPU control flow 交給 kernel。

目前使用 function pointer：

```c
static void jump_to_kernel(void)
{
    void (*kernel_entry)(void) =
        (void (*)(void))KERNEL_ADDRESS;

    kernel_entry();
}
```

其中：

```c
void (*kernel_entry)(void)
```

表示：

> `kernel_entry` 是一個 function pointer，它指向一個不接受參數、也沒有 return value 的 function。

這段：

```c
(void (*)(void))KERNEL_ADDRESS
```

則是把：

```text
0x80000
```

cast 成 function pointer。

因此：

```c
kernel_entry();
```

就類似呼叫一個 procedure。

CPU 在 function call 時會將 control flow 跳到 function pointer 指向的位置：

```text
bootloader
PC ≈ 0x6xxxx
      ↓
kernel_entry()
      ↓
PC = 0x80000
      ↓
kernel::_start
```

所以：

```text
uint8_t *kernel
```

是把 `0x80000` 當成「資料位址」。

而：

```text
void (*kernel_entry)(void)
```

是把 `0x80000` 當成「程式入口」。

---

# 14. `bootloader_main()`

Bootloader 主要流程：

```c
void bootloader_main(void)
{
    uart_init();

    uart_puts("Waiting for kernel8.img......\n");

    uint32_t kernel_size = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)uart_recv();

        kernel_size |= ((uint32_t)c << (8 * i));
    }

    uart_hex(kernel_size);

    uint8_t *kernel = (uint8_t *)KERNEL_ADDRESS;

    for (uint32_t i = 0; i < kernel_size; i++) {
        kernel[i] = (uint8_t)uart_recv();
    }

    uart_puts("Success load kernel8.img into memory\n");

    jump_to_kernel();
}
```

完整 control flow：

```text
bootloader::_start
       ↓
bootloader_main()
       ↓
uart_init()
       ↓
等待 Host
       ↓
收 kernel_size
       ↓
收 kernel8.img
       ↓
寫入 0x80000
       ↓
jump_to_kernel()
       ↓
kernel::_start
       ↓
kernel_main()
```

---

# 15. Python Kernel Sender

Python 是 Host-side tool。

它不需要了解 ARM instruction，只負責：

```text
讀 kernel8.img
      ↓
計算 image size
      ↓
傳 4-byte size
      ↓
傳 binary image
```

目前 sender 的核心：

```python
from pathlib import Path
import struct

PROJECT_ROOT = Path(__file__).resolve().parents[1]

KERNEL_PATH = PROJECT_ROOT / "build/kernel/kernel8.img"
PTY_FILE = PROJECT_ROOT / "build/uart_pty.txt"

if not PTY_FILE.exists():
    raise SystemExit(
        f"PTY file not found: {PTY_FILE}. Run 'make run' first."
    )

uart_path = PTY_FILE.read_text().strip()

with open(KERNEL_PATH, "rb") as f:
    kernel = f.read()

kernel_size = len(kernel)

print(type(kernel))
print(kernel_size)
print(hex(kernel_size))

size_bytes = struct.pack("<I", kernel_size)

print("UART PTY:", uart_path)
print("send kernel size:", kernel_size)

with open(uart_path, "wb", buffering=0) as tty:
    tty.write(size_bytes)
    tty.write(kernel)
```

注意：

```python
kernel = f.read()
```

本身就已經是：

```text
bytes
```

因此可以直接：

```python
tty.write(kernel)
```

不需要：

```python
struct.pack("<s", kernel)
```

`struct.pack()` 只用來把：

```text
kernel_size
```

這個 Python integer 轉成固定 4-byte binary。

---

# 16. PTY 是什麼？

PTY：

```text
Pseudo Terminal
```

可以理解成：

> Linux 用軟體建立的虛擬 terminal endpoint。

它不是實體 UART port，也沒有：

```text
TX
RX
電壓訊號
PCB connector
```

---

## 實體 Raspberry Pi

實際可能是：

```text
PC
 │
 │ /dev/ttyUSB0
 ▼
USB-to-UART adapter
 │
 ├── TX
 ├── RX
 └── GND
 │
 ▼
Raspberry Pi UART
```

---

## QEMU

QEMU 沒有實體 UART wire，因此：

```text
Host
 │
 │ /dev/pts/N
 ▼
PTY
 │
 ▼
QEMU Virtual UART
 │
 ▼
Guest uart_recv()
```

例如：

```text
/dev/pts/7
```

代表 Linux 動態建立的一個 pseudo-terminal device。

數字 `7` 沒有特殊意義。

下一次可能變成：

```text
/dev/pts/9
```

---

# 17. 為什麼原本不需要 `socat`？

原本直接執行 kernel 時，QEMU 使用：

```text
-serial stdio
```

UART 直接接到目前 terminal：

```text
Keyboard
    ↕
Terminal
    ↕
QEMU UART
    ↕
Kernel
```

因此可以直接操作 shell。

但 UART bootloader 需要 Python 傳 binary，所以改成：

```text
-serial pty
```

此時：

```text
QEMU UART
    ↕
/dev/pts/N
```

UART 已經不再直接接 terminal。

因此 kernel 啟動之後，如果人要與 shell 互動，需要：

```text
Terminal
    ↕
socat
    ↕
/dev/pts/N
    ↕
QEMU UART
    ↕
Kernel
```

所以：

> `socat` 並不是 bootloader 必須使用的工具，而是因為 QEMU UART backend 從 `stdio` 改成 PTY 後，需要一個工具把 terminal 與 PTY 做雙向連接。

---

# 18. `socat`

`socat` 可以理解成：

> 將兩個 byte stream 做雙向連接。

目前使用：

```bash
socat -,rawer,escape=0x0f /dev/pts/N,rawer
```

其中：

```text
-
```

代表目前 terminal 的 STDIN / STDOUT。

```text
rawer
```

讓 terminal 接近 raw byte mode，避免一般 terminal line processing。

```text
escape=0x0f
```

設定：

```text
Ctrl + O
```

作為離開 `socat` 的快捷鍵。

因此：

```text
Keyboard
   ↓
socat
   ↓
PTY
   ↓
QEMU UART
   ↓
kernel uart_recv()
```

以及：

```text
kernel uart_send()
   ↓
QEMU UART
   ↓
PTY
   ↓
socat
   ↓
Screen
```

---

# 19. 為什麼 `/dev/pts/N` 不能寫死？

QEMU：

```text
-serial pty
```

每次啟動時 Linux 都可能分配不同 PTY。

例如：

```text
第一次：/dev/pts/7
第二次：/dev/pts/8
第三次：/dev/pts/12
```

因此 Python 不能直接寫：

```python
open("/dev/pts/7", ...)
```

否則下一次 QEMU 啟動後可能失效。

目前使用一個 helper script 自動擷取 QEMU output。

---

# 20. `capture_pty.sh`

QEMU 啟動時會輸出：

```text
char device redirected to /dev/pts/7 (label serial1)
```

`capture_pty.sh`：

```bash
#!/usr/bin/env bash

PTY_FILE="$1"

while IFS= read -r line; do
    # 保留 QEMU 原本的輸出
    printf '%s\n' "$line" >&2

    # 擷取 /dev/pts/N
    if [[ "$line" =~ (/dev/pts/[0-9]+) ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}" > "$PTY_FILE"
    fi
done
```

例如收到：

```text
char device redirected to /dev/pts/7
```

會建立：

```text
build/uart_pty.txt
```

內容：

```text
/dev/pts/7
```

下一次若 QEMU 取得：

```text
/dev/pts/12
```

檔案會自動改成：

```text
/dev/pts/12
```

---

# 21. QEMU Makefile Target

目前：

```makefile
SHELL := /bin/bash

UART_PTY_FILE := $(BUILD_DIR)/uart_pty.txt
PTY_CAPTURE   := tools/capture_pty.sh
```

QEMU：

```makefile
run: $(BOOT_IMG)
	rm -f $(UART_PTY_FILE)
	$(QEMU) \
		-M raspi3b \
		-device loader,file=$(BOOT_IMG),addr=0x60000,cpu-num=0,force-raw=on \
		-display none \
		-serial null \
		-serial pty \
		2>&1 | $(PTY_CAPTURE) $(UART_PTY_FILE)
```

這裡：

```bash
2>&1
```

表示將：

```text
stderr
```

合併到：

```text
stdout
```

所以：

```text
QEMU stdout ─┐
             ├── capture_pty.sh
QEMU stderr ─┘
```

不管 QEMU 將：

```text
char device redirected to /dev/pts/N
```

輸出到哪個 stream，都能被 helper script 捕捉。

---

# 22. `run-socat`

```makefile
# Connect the current terminal to QEMU UART PTY.
# rawer disables normal terminal processing and Ctrl+O exits socat.
run-socat:
	@test -f $(UART_PTY_FILE) || \
		(echo "PTY file not found. Run 'make run' first."; exit 1)
	@UART_PTY=$$(cat $(UART_PTY_FILE)); \
		echo "Connect to $$UART_PTY"; \
		socat -,rawer,escape=0x0f $$UART_PTY,rawer
```

因此不需要手動：

```bash
socat ... /dev/pts/7 ...
```

只需要：

```bash
make run-socat
```

Makefile 會自己讀：

```text
build/uart_pty.txt
```

---

# 23. 完整執行流程

## Terminal 1：啟動 QEMU Bootloader

```bash
make run
```

QEMU：

```text
bootloader.img
      ↓
0x60000
      ↓
bootloader::_start
      ↓
bootloader_main()
      ↓
Waiting for kernel8.img...
```

同時：

```text
QEMU
 ↓
建立 /dev/pts/N
 ↓
capture_pty.sh
 ↓
build/uart_pty.txt
```

---

## Terminal 2：傳送 Kernel

```bash
python3 tools/send_kernel.py
```

Python：

```text
讀 build/uart_pty.txt
      ↓
找到目前 /dev/pts/N
      ↓
讀 kernel8.img
      ↓
傳 kernel size
      ↓
傳 kernel bytes
```

Bootloader：

```text
UART 收 kernel size
      ↓
UART 收 kernel binary
      ↓
寫到 0x80000
      ↓
jump 0x80000
```

---

## Terminal 2：連接 Kernel Shell

等 Python sender 結束後：

```bash
make run-socat
```

此時：

```text
Terminal
   ↕
socat
   ↕
/dev/pts/N
   ↕
QEMU UART
   ↕
Kernel
```

可以正常執行：

```text
# help
# hello
```

使用：

```text
Ctrl + O
```

離開 `socat`。

---

# 24. 完整資料流

整個 Lab 最終可以整理成：

```text
                         Host

                    kernel8.img
                         │
                         ▼
                 send_kernel.py
                         │
                         │ size + binary
                         ▼
                    /dev/pts/N
                         │
                         │ PTY
                         ▼
                       QEMU
                         │
                         ▼
                 Virtual Mini UART
                         │
                         ▼

                        Guest

                 bootloader @ 0x60000
                         │
                         │ uart_recv()
                         ▼
                   kernel @ 0x80000
                         │
                         │ jump
                         ▼
                   kernel::_start
                         │
                         ▼
                    kernel_main()
                         │
                         ▼
                        shell
                         │
                         ▼

                  QEMU Virtual UART
                         ↕
                    /dev/pts/N
                         ↕
                       socat
                         ↕
                   User Terminal
```

---

# 25. 常見問題

## 25.1 `kernel_size` 沒初始化

錯誤：

```c
uint32_t kernel_size;
```

接著：

```c
kernel_size |= ...
```

可能把 stack 上原本的垃圾值一起 OR 進去。

應該：

```c
uint32_t kernel_size = 0;
```

---

## 25.2 Kernel index 被 cast 成 `uint8_t`

錯誤：

```c
kernel[(uint8_t)i]
```

因為：

```text
uint8_t = 0 ~ 255
```

`i = 256` 時會變回：

```text
0
```

應直接：

```c
kernel[i]
```

---

## 25.3 對 kernel binary 使用 `struct.pack("<s", kernel)`

不需要。

```python
kernel = f.read()
```

本身已經是：

```text
bytes
```

直接：

```python
tty.write(kernel)
```

即可。

---

## 25.4 `cat /dev/pts/N` 只能看 output

```bash
cat /dev/pts/N
```

主要只是：

```text
PTY → terminal
```

無法方便地建立完整雙向 interactive console。

Kernel shell 應改用：

```bash
socat -,rawer,escape=0x0f /dev/pts/N,rawer
```

---

## 25.5 `socat` 無法用 `Ctrl+C` 離開

因為：

```text
rawer
```

讓 terminal 進入 raw mode，`Ctrl+C` 可能直接變成 byte `0x03` 傳給 kernel。

因此指定：

```text
escape=0x0f
```

並使用：

```text
Ctrl + O
```

離開。

---

## 25.6 `/dev/pts/N` 每次不同

這是正常現象。

`N` 是 Linux 動態分配的 PTY 編號：

```text
/dev/pts/7
/dev/pts/9
/dev/pts/12
```

因此目前透過：

```text
capture_pty.sh
```

自動寫入：

```text
build/uart_pty.txt
```

Python 與 `socat` 都不再 hard-code `/dev/pts/N`。

---

# 26. 目前完成的功能

目前 UART Bootloader 已完成：

```text
Bootloader @ 0x60000
        ↓
UART initialization
        ↓
等待 Host
        ↓
接收 4-byte kernel size
        ↓
接收 kernel8.img
        ↓
寫入 RAM @ 0x80000
        ↓
function pointer jump
        ↓
Kernel::_start
        ↓
kernel_main()
        ↓
Shell
```

Host side：

```text
QEMU
 ↓
Dynamic PTY
 ↓
capture_pty.sh
 ↓
uart_pty.txt

send_kernel.py
 ↓
傳 kernel

socat
 ↓
interactive kernel shell
```

因此已完成 UART Bootloader 的核心流程：

> **在不重新直接載入 kernel image 的情況下，先執行固定 bootloader，再透過 UART 將新的 kernel image 傳入 RAM 並執行。**

# Basic Exercise 3 - Initial Ramdisk

## 目標

這個 Exercise 的目標是理解 **Kernel 在尚未具備完整 storage driver / filesystem 的情況下，如何先取得開機階段需要的檔案**。

本實作使用：

```text
CPIO newc archive
```

將多個檔案打包成：

```text
initramfs.cpio
```

並讓 QEMU 預先把 CPIO archive 放進 RAM。

Kernel 啟動之後，不需要先存取 SD card，而是直接解析 RAM 裡的 CPIO archive。

本 Exercise 最後需要做到：

```text
CPIO archive
      ↓
解析 Header
      ↓
取得 filename / filesize
      ↓
找到 file data
      ↓
走訪所有檔案
      ↓
根據 pathname 找到指定檔案
```

---

# 1. 為什麼需要 Initial Ramdisk？

Kernel 啟動之後，如果想直接讀：

```text
SD Card / Disk
```

需要先具備：

```text
Storage Driver
      ↓
Block Device
      ↓
Filesystem
      ↓
File
```

例如：

```text
Kernel
  ↓
SD Driver
  ↓
SD Card
  ↓
Filesystem
  ↓
hello.txt
```

但是在 Kernel 剛啟動的 early boot 階段，這些功能可能還沒有完成。

因此可以先讓 boot firmware / QEMU：

```text
把必要的檔案
      ↓
先放進 RAM
```

Kernel 就可以：

```text
Kernel
  ↓
直接讀 RAM
  ↓
initramfs.cpio
```

而不需要先完成 SD card driver。

---

# 2. 為什麼 boot firmware 可以讀 SD card？

讀 SD card 並不是「不需要 driver」。

差別在於：

```text
開機階段
→ Boot ROM / Boot Firmware 自己具有讀 boot device 的能力

Kernel 執行階段
→ Kernel 需要自己的 storage driver
```

實體 Raspberry Pi 3 的概念：

```text
Power On
   ↓
Boot ROM
   ↓
bootcode.bin
   ↓
start.elf / firmware
   ↓
讀 SD Card boot partition
   ↓
載入 Kernel / initramfs
   ↓
放進 RAM
   ↓
啟動 Kernel
```

所以：

```text
kernel8.img
initramfs.cpio
```

原本可以都放在 SD card 的 boot partition。

Kernel 開始執行之前，firmware 已經先把它們搬進 RAM。

---

# 3. UART Bootloader 與 Initramfs

目前專案使用自製 UART bootloader：

```text
0x60000
UART Bootloader
      ↓
UART 接收 kernel8.img
      ↓
0x80000
Kernel
```

因此目前 QEMU memory layout 可以理解成：

```text
RAM

0x00060000
┌─────────────────────┐
│ UART Bootloader     │
└─────────────────────┘

0x00080000
┌─────────────────────┐
│ kernel8.img         │
└─────────────────────┘

0x08000000
┌─────────────────────┐
│ initramfs.cpio      │
└─────────────────────┘
```

---

# 4. QEMU 載入 CPIO

Lab 原本使用：

```bash
-initrd initramfs.cpio
```

並且 QEMU 預設將 CPIO archive 放到：

```text
0x08000000
```

但是目前 UART bootloader 使用：

```bash
-device loader,file=...,addr=0x60000
```

而不是：

```bash
-kernel
```

因此 QEMU 會出現：

```text
-initrd only allowed with -kernel option
```

所以改成直接使用 generic loader：

```bash
-device loader,file=initramfs.cpio,addr=0x08000000,force-raw=on
```

注意這個 loader 不指定：

```text
cpu-num
```

因為 initramfs 只是資料，不是要執行的程式。

---

# 5. 建立 CPIO Archive

先建立：

```text
rootfs/
├── hello.txt
└── test.txt
```

例如：

```text
hello.txt
→ Hello from initramfs!

test.txt
→ This is a test file.
```

建立 `newc` 格式 archive：

```bash
cd rootfs
find . | cpio -o -H newc > ../initramfs.cpio
cd ..
```

檢查 archive：

```bash
cpio -it < initramfs.cpio
```

範例輸出：

```text
.
hello.txt
test.txt
2 blocks
```

代表 CPIO archive 已成功建立。

---

# 6. CPIO newc 的資料格式

CPIO 並不是單純：

```text
[header][data]
[header][data]
```

每一個 entry 的格式是：

```text
[Header]
[Filename]
[Padding]
[File Data]
[Padding]
```

然後才接下一個 entry：

```text
Entry 1
[Header][Filename][Padding][Data][Padding]

Entry 2
[Header][Filename][Padding][Data][Padding]

Entry 3
...
```

完整概念：

```text
Header
  ↓
Filename
  ↓
Align 4
  ↓
File Data
  ↓
Align 4
  ↓
Next Header
```

---

# 7. CPIO Header

`newc` header：

```c
struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
};
```

大小：

```text
6 + 13 × 8
= 110 bytes
```

目前主要使用：

```text
c_magic
c_namesize
c_filesize
```

---

# 8. Magic Number

`newc` CPIO 的 magic：

```text
070701
```

因此可以先測試：

```c
struct cpio_newc_header *header =
    (struct cpio_newc_header *)0x08000000;
```

印出：

```c
for (int i = 0; i < 6; i++) {
    uart_send(header->c_magic[i]);
}
```

成功輸出：

```text
070701
```

代表：

```text
QEMU 成功載入 initramfs.cpio
+
0x08000000 確實是 CPIO Header
```

不能直接：

```c
uart_puts(header->c_magic);
```

因為：

```c
char c_magic[6];
```

沒有保證後面有：

```text
'\0'
```

`uart_puts()` 會持續往後讀，造成：

```text
0707010000A813000041ED...
```

---

# 9. 為什麼 Header 裡的數字需要 hex_to_uint？

`newc` 格式中的數字不是 binary integer。

例如：

```c
char c_filesize[8];
```

裡面可能是 ASCII：

```text
0000001A
```

代表：

```text
0x1A
= 26
```

所以需要自己把 ASCII hexadecimal 轉成 integer。

實作：

```c
unsigned int hex_to_uint(const char *s, int len)
{
    unsigned int sum = 0;

    for (int i = 0; i < len; i++) {
        sum <<= 4;

        sum += ('A' <= s[i] && s[i] <= 'F')
            ? (s[i] - 'A' + 10)
            : (s[i] - '0');
    }

    return sum;
}
```

概念：

```text
"1A"

1
↓ × 16
16
↓ + 10
26
```

---

# 10. 為什麼需要 align4？

`newc` CPIO 規定資料需要按照 4-byte boundary 排列。

因此：

```text
filename 結束
↓
補 padding
↓
下一個 4-byte aligned address
↓
File Data
```

同樣：

```text
File Data 結束
↓
補 padding
↓
下一個 Header
```

實作：

```c
unsigned long align4(unsigned long n)
{
    return (n + 3) & ~3UL;
}
```

例如：

```text
align4(116) = 116
align4(117) = 120
align4(118) = 120
align4(119) = 120
align4(120) = 120
```

注意：

不是：

```c
filesize = align4(filesize);
```

而是對 **下一個 address** 做 alignment：

```text
data address
+
filesize
↓
align4
↓
next header address
```

---

# 11. Pointer Arithmetic

這裡需要注意 pointer 和 integer address 的差別。

例如：

```c
char *ptr;
ptr + 1;
```

因為：

```text
sizeof(char) = 1
```

所以前進：

```text
1 byte
```

但是：

```c
struct cpio_newc_header *header;
header + 1;
```

會前進：

```text
sizeof(struct cpio_newc_header)
= 110 bytes
```

如果：

```c
(unsigned long)header
```

它已經不是 pointer，而是單純的 address 數值。

所以：

```c
(unsigned long)header + 110
```

就是：

```text
address + 110 bytes
```

不會乘上：

```text
sizeof(unsigned long)
```

---

# 12. 找到 Filename

因為 CPIO 規格定義：

```text
Header
↓
Filename
```

所以：

```c
char *filename =
    (char *)header + sizeof(struct cpio_newc_header);
```

`c_namesize` 告訴我們 filename 的長度：

```c
unsigned int namesize =
    hex_to_uint(header->c_namesize, 8);
```

注意：

```text
namesize 包含最後的 '\0'
```

例如：

```text
hello.txt\0
```

總長：

```text
10 bytes
```

---

# 13. 找到 File Data

File Data 位在：

```text
Header
+
Header Size
+
Filename Size
+
Padding
```

所以：

```c
char *fileDataPtr = (char *)align4(
    (unsigned long)header
    + sizeof(struct cpio_newc_header)
    + namesize
);
```

可以理解成：

```text
Header Address
      +
110 bytes
      +
namesize
      ↓
Filename 結尾
      ↓
align4()
      ↓
File Data
```

---

# 14. 找到下一個 Header

Header 裡：

```c
header->c_filesize
```

可以取得 file data 的長度：

```c
unsigned int filesize =
    hex_to_uint(header->c_filesize, 8);
```

因此下一個 Header：

```c
header = (struct cpio_newc_header *)align4(
    (unsigned long)fileDataPtr + filesize
);
```

完整公式：

```text
filename
= header + sizeof(header)

file data
= align4(
    header + sizeof(header) + namesize
)

next header
= align4(
    file data + filesize
)
```

這三個公式就是目前 CPIO parser 的核心。

---

# 15. TRAILER!!!

怎麼知道整個 CPIO archive 已經結束？

最後會有一個特殊 entry：

```text
TRAILER!!!
```

所以 parser：

```c
if (str_equal(filename, "TRAILER!!!")) {
    break;
}
```

因此：

```text
單一檔案的結尾
→ c_filesize

整個 Archive 的結尾
→ TRAILER!!!
```

---

# 16. cpio_list()

`cpio_list()` 負責從第一個 Header 開始，一路走訪整個 CPIO archive。

核心流程：

```text
Header
↓
取得 filename
↓
取得 namesize
↓
判斷 TRAILER!!!
↓
取得 filesize
↓
算 File Data
↓
算 Next Header
↓
繼續
```

實作概念：

```c
void cpio_list(void)
{
    struct cpio_newc_header *header =
        (struct cpio_newc_header *)CPIO_ADD;

    while (1) {
        char *filename =
            (char *)header
            + sizeof(struct cpio_newc_header);

        unsigned int namesize =
            hex_to_uint(header->c_namesize, 8);

        if (str_equal(filename, "TRAILER!!!")) {
            break;
        }

        uart_puts(filename);
        uart_puts("\r\n");

        unsigned int filesize =
            hex_to_uint(header->c_filesize, 8);

        char *fileDataPtr = (char *)align4(
            (unsigned long)header
            + sizeof(struct cpio_newc_header)
            + namesize
        );

        header =
            (struct cpio_newc_header *)align4(
                (unsigned long)fileDataPtr
                + filesize
            );
    }
}
```

測試結果：

```text
CPIO list file
.
hello.txt
test.txt
TRAILER!!! STOP!!!
```

---

# 17. 不要用 uart_puts() 直接印 File Data

`uart_puts()` 需要：

```text
'\0'
```

作為結尾。

但是 CPIO 的 file data 是：

```text
filesize bytes
```

不保證最後有：

```text
'\0'
```

因此不要：

```c
uart_puts(fileDataPtr);
```

應該：

```c
for (unsigned int i = 0; i < filesize; i++) {
    uart_send(fileDataPtr[i]);
}
```

這樣只會讀指定的：

```text
filesize bytes
```

---

# 18. cpio_find_file()

單純 `cpio_list()` 只能證明：

```text
可以走訪 archive
```

真正的 Initial Ramdisk 功能還應該能做到：

```text
給 pathname
↓
找到對應檔案
↓
取得 data
↓
取得 size
```

介面：

```c
int cpio_find_file(
    const char *target,
    char **data,
    unsigned int *size
);
```

三個參數：

```text
target
→ Input
→ 我要找哪個檔案

data
→ Output
→ 找到後 File Data 在哪個 address

size
→ Output
→ 找到後 File Data 有多大
```

例如：

```c
char *data;
unsigned int size;

if (cpio_find_file("hello.txt", &data, &size)) {
    ...
}
```

---

# 19. 為什麼 data 是 char **？

caller：

```c
char *data;
```

如果 function 要修改 caller 的：

```text
data pointer
```

就需要傳：

```c
&data
```

型別就是：

```c
char **
```

因此 callee：

```c
*data = fileDataPtr;
```

就能讓 caller 最後：

```text
data
 ↓
hello.txt 的內容
```

---

# 20. 為什麼 size 是 unsigned int *？

C function argument 本質上都是 pass by value。

如果：

```c
void test(unsigned int size)
{
    size = 20;
}
```

caller：

```c
unsigned int size = 0;
test(size);
```

執行後：

```text
size 還是 0
```

因為修改的是 copy。

所以：

```c
void test(unsigned int *size)
{
    *size = 20;
}
```

caller：

```c
test(&size);
```

才可以修改 caller 的變數。

因此：

```c
unsigned int *size
```

是 output parameter。

---

# 21. cpio_find_file() 實作

核心流程：

```text
從第一個 Header 開始
↓
取得 filename
↓
如果 TRAILER!!! → 找不到
↓
算 filesize
↓
算 fileDataPtr
↓
filename == target ?
    ↓ Yes
回傳：
data
size
↓
No
↓
Next Header
```

實作：

```c
int cpio_find_file(
    const char *target,
    char **data,
    unsigned int *size
)
{
    struct cpio_newc_header *header =
        (struct cpio_newc_header *)CPIO_ADD;

    while (1) {
        char *filename =
            (char *)header
            + sizeof(struct cpio_newc_header);

        unsigned int namesize =
            hex_to_uint(header->c_namesize, 8);

        if (str_equal(filename, "TRAILER!!!")) {
            break;
        }

        unsigned int filesize =
            hex_to_uint(header->c_filesize, 8);

        char *fileDataPtr = (char *)align4(
            (unsigned long)header
            + sizeof(struct cpio_newc_header)
            + namesize
        );

        if (str_equal(filename, target)) {
            *size = filesize;
            *data = fileDataPtr;

            return 1;
        }

        header =
            (struct cpio_newc_header *)align4(
                (unsigned long)fileDataPtr
                + filesize
            );
    }

    return 0;
}
```

目前定義：

```text
return 1
→ 找到

return 0
→ 找不到
```

---

# 22. Magic Sanity Check

為了避免 address 計算錯誤後，繼續把垃圾 memory 當作 CPIO Header，可以每次進 loop 先檢查：

```text
070701
```

例如：

```c
static int cpio_valid_magic(
    struct cpio_newc_header *header
)
{
    const char magic[6] =
        {'0', '7', '0', '7', '0', '1'};

    for (int i = 0; i < 6; i++) {
        if (header->c_magic[i] != magic[i]) {
            return 0;
        }
    }

    return 1;
}
```

使用：

```c
if (!cpio_valid_magic(header)) {
    uart_puts("Invalid CPIO magic\r\n");
    return 0;
}
```

這是很好用的 debug invariant：

> 每一個合法的 newc entry 開頭都應該是 `070701`。

如果突然不是：

```text
070701
```

通常代表：

```text
Next Header Address 算錯
```

---

# 23. CPIO Debug 方法

如果 parser 出現亂碼，不要第一時間懷疑 encoding。

通常優先懷疑：

```text
Pointer / Address Calculation
```

建議逐步印：

```text
Header Address
Magic
namesize
filesize
Filename Address
File Data Address
Next Header Address
```

最重要的 invariant：

```text
每次進入新的 Header
↓
c_magic 都應該是 070701
```

如果：

```text
Entry 1 magic = 070701
Entry 2 magic = 070701
Entry 3 magic = hello.
```

代表：

```text
Entry 2
↓
Entry 3
```

的 address calculation 出錯。

---

# 24. UART 的 \r 與 \n

曾經出現：

```text
070701
      Board revision...
```

原因是：

```c
uart_send('\n');
```

只有 Line Feed：

```text
往下一行
```

但是沒有：

```text
Carriage Return
```

所以游標沒有回到最左邊。

正確：

```c
uart_puts("\r\n");
```

其中：

```text
\r
→ 回到行首

\n
→ 下一行
```

---

# 25. Reboot / Reload Kernel

由於目前使用 QEMU，Lab 提供的 Raspberry Pi watchdog reboot 不適用。

目前 memory：

```text
0x60000
UART Bootloader

0x80000
Kernel
```

因此可以在 Kernel 做：

```c
void reboot(void)
{
    void (*bootloader)(void) =
        (void (*)(void))0x60000;

    bootloader();

    while (1) {
    }
}
```

這不是完整硬體 reset，而是：

```text
Kernel
↓
jump 0x60000
↓
重新進入 UART Bootloader
↓
重新接收 kernel8.img
```

這樣測試時不需要每次關閉 QEMU。

---

# 26. 最終測試

離開 Basic Exercise 3 前，至少確認：

## Test 1：List Files

```text
.
hello.txt
test.txt
```

## Test 2：Find Existing File

```c
cpio_find_file("hello.txt", ...)
```

結果：

```text
return 1
```

並且：

```text
data → Hello from initramfs!
size → 正確 filesize
```

## Test 3：Find Missing File

```c
cpio_find_file("not_exist.txt", ...)
```

結果：

```text
return 0
```

而且：

```text
不 crash
不繼續亂讀 memory
```

---

# 27. 本 Exercise 最後理解

Initial Ramdisk 的核心不是：

```text
做一個真正的 filesystem
```

也不是：

```text
實作 SD card driver
```

而是：

```text
Boot Firmware / QEMU
↓
先把一包檔案放進 RAM
↓
Kernel
↓
解析 CPIO
↓
取得 early boot 所需要的檔案
```

目前完成的是：

```text
✅ 建立 initramfs.cpio
✅ QEMU 載入 CPIO 到 RAM
✅ 驗證 newc magic
✅ ASCII Hex 解析
✅ 4-byte alignment
✅ 走訪 CPIO Entry
✅ cpio_list()
✅ cpio_find_file()
✅ TRAILER!!! 判斷
✅ CPIO Header sanity check
```

因此：

```text
Basic Exercise 3 - Initial Ramdisk
→ Completed
```

下一個 Exercise：

```text
Basic Exercise 4 - Simple Allocator
```

---

# 核心公式整理

最後只要記住這三條：

```c
filename =
    (char *)header
    + sizeof(struct cpio_newc_header);
```

```c
fileDataPtr =
    (char *)align4(
        (unsigned long)header
        + sizeof(struct cpio_newc_header)
        + namesize
    );
```

```c
nextHeader =
    (struct cpio_newc_header *)align4(
        (unsigned long)fileDataPtr
        + filesize
    );
```

整個 parser 本質就是：

```text
Header
  ↓
Filename
  ↓
align4
  ↓
File Data
  ↓
align4
  ↓
Next Header
  ↓
重複
  ↓
TRAILER!!!
  ↓
STOP
```