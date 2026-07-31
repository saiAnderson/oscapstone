# Basic Exercise 1 - Basic Initialization

## 目標

Raspberry Pi 3 的 bootloader 會將 `kernel8.img` 載入至實體記憶體位址 `0x80000`，並從該位置開始執行。

在進入 C 程式之前，本練習完成以下初始化工作：

* 將 kernel 各 section 配置到正確的記憶體位置。
* 將 boot code 放在 kernel image 的最前面。
* 只允許 Core 0 執行 kernel 初始化。
* 為 Core 0 設定有效的 Stack Pointer。
* 將 `.bss` section 初始化為 0。
* 呼叫 C 語言的 `kernel_main()`。
* 在 secondary cores 或 `kernel_main()` 返回時進入等待迴圈。

---

## 專案檔案

```text
lab1/
├── boot.S
├── kernel.c
├── linker.ld
├── Makefile
├── kernel8.elf
└── kernel8.img
```

各檔案的用途如下：

| 檔案            | 用途                                                 |
| ------------- | -------------------------------------------------- |
| `boot.S`      | CPU core 篩選、Stack Pointer 設定、`.bss` 清零及呼叫 C kernel |
| `kernel.c`    | 提供 `kernel_main()`，作為 C 程式入口                       |
| `linker.ld`   | 決定 kernel sections 的記憶體配置                          |
| `Makefile`    | 自動完成組譯、編譯、連結及 image 產生                             |
| `kernel8.elf` | 包含 symbol、section 與 debug 資訊的 ELF 檔案               |
| `kernel8.img` | 提供 Raspberry Pi 3 或 QEMU 載入的 raw binary            |

---

## 啟動流程

```text
Raspberry Pi 3 bootloader
        │
        │ 載入 kernel8.img 至 0x80000
        ▼
      _start
        │
        ├── 讀取 MPIDR_EL1，取得目前 Core ID
        │
        ├── Core 1～3 進入等待迴圈
        │
        └── Core 0 繼續執行
                │
                ├── 設定 Stack Pointer
                ├── 清除 .bss
                ├── 呼叫 kernel_main()
                └── kernel_main 返回後等待
```

---

## `boot.S`

```asm
.section ".text.boot", "ax"

.global _start
.extern __stack_top
.extern __bss_start
.extern __bss_end
.extern kernel_main

_start:
    /*
     * 讀取 MPIDR_EL1，取得目前 CPU core 的 affinity 資訊。
     */
    mrs x0, mpidr_el1

    /*
     * Aff0 位於最低 8 bits。
     * Core 0～3 的 Aff0 通常分別為 0～3。
     */
    and x0, x0, #0xff

    /*
     * 只有 Core 0 繼續進行初始化。
     */
    cbz x0, primary_core

secondary_core_hang:
    /*
     * Core 1～3 停在等待迴圈。
     */
    wfe
    b secondary_core_hang

primary_core:
    /*
     * AArch64 stack 向低位址成長，
     * 因此初始 SP 設為 linker 配置的 __stack_top。
     */
    ldr x12, =__stack_top
    mov sp, x12

    /*
     * 載入 .bss 的開始與結束地址。
     */
    ldr x9, =__bss_start
    ldr x10, =__bss_end

clear_bss:
    /*
     * 如果目前位置已到達或超過結束地址，
     * 代表 .bss 已清除完成。
     */
    cmp x9, x10
    b.hs clear_bss_done

    /*
     * 一次寫入兩個 64-bit zero registers，
     * 共清除 16 bytes，並使 x9 自動增加 16。
     */
    stp xzr, xzr, [x9], #16
    b clear_bss

clear_bss_done:
    /*
     * 進入 C kernel。
     */
    bl kernel_main

hang:
    /*
     * kernel_main 返回時進入安全等待迴圈。
     */
    wfe
    b hang
```

---

## 多核心處理

Raspberry Pi 3 使用四核心 Cortex-A53。

若四個 cores 都繼續執行初始化，它們可能會：

* 使用相同的 Stack Pointer。
* 同時清除 `.bss`。
* 同時呼叫 `kernel_main()`。
* 互相覆蓋 stack frame、返回地址與區域變數。

目前僅配置一組 stack，因此透過 `MPIDR_EL1` 的 Aff0 欄位取得 Core ID，只允許 Core 0 執行初始化。

```asm
mrs x0, mpidr_el1
and x0, x0, #0xff
cbz x0, primary_core
```

Core 1～3 則進入：

```asm
secondary_core_hang:
    wfe
    b secondary_core_hang
```

---

## `.bss` 初始化

`.bss` 通常包含未初始化或初始化為 0 的全域及 `static` 變數，例如：

```c
int counter;
static char buffer[128];
int status = 0;
```

依照 C 語言規則，這些資料在程式開始執行時必須為 0。

Linker Script 提供：

```text
__bss_start
__bss_end
```

`boot.S` 清除以下半開區間：

```text
[__bss_start, __bss_end)
```

清零指令為：

```asm
stp xzr, xzr, [x9], #16
```

一個 `x` register 為 64 bits，也就是 8 bytes，因此兩個 `xzr` 共寫入：

```text
8 bytes + 8 bytes = 16 bytes
```

Linker Script 會將 `.bss` 開始與結束位置對齊至 16 bytes，讓清零迴圈不需要額外處理剩餘 bytes。

---

## `linker.ld`

```ld
ENTRY(_start)

SECTIONS
{
    /*
     * Raspberry Pi 3 bootloader 預期將 kernel
     * 載入實體記憶體位址 0x80000。
     */
    . = 0x80000;
    __kernel_start = .;

    /*
     * Boot code 位於 image 最前面，
     * 其他一般程式碼接在後方。
     */
    . = ALIGN(16);

    .text :
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    /*
     * 唯讀資料，例如 const 變數及字串常數。
     */
    . = ALIGN(16);

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    /*
     * 有非零初始值的可寫全域及 static 變數。
     */
    . = ALIGN(16);

    .data :
    {
        *(.data .data.*)
    }

    /*
     * 未初始化或零初始化的全域及 static 變數。
     *
     * NOLOAD 表示只保留執行時的記憶體空間，
     * 不在 kernel image 中存放大量的零。
     */
    . = ALIGN(16);

    .bss (NOLOAD) :
    {
        __bss_start = .;

        *(.bss .bss.*)
        *(COMMON)

        . = ALIGN(16);
        __bss_end = .;
    }

    /*
     * 為 Core 0 預留 16 KiB stack。
     */
    . = ALIGN(16);

    .stack (NOLOAD) :
    {
        __stack_bottom = .;
        . += 16K;
        __stack_top = .;
    }

    __kernel_end = .;
}
```

---

## Linker Script 設計說明

### Kernel 載入地址

```ld
. = 0x80000;
```

`.` 是 Linker Script 的 Location Counter，代表 linker 目前配置到的地址。

此設定讓第一個 output section 從 `0x80000` 開始。

---

### ELF Entry Point

```ld
ENTRY(_start)
```

`ENTRY(_start)` 會將 `_start` 的地址寫入 ELF Header 的 Entry Point 欄位。

可透過以下指令確認：

```bash
aarch64-linux-gnu-readelf -h kernel8.elf
```

預期結果：

```text
Entry point address: 0x80000
```

實際載入 raw `kernel8.img` 時，CPU 的起始 PC 仍由 bootloader 或 QEMU 的板級啟動流程決定。

---

### Boot Section

`boot.S` 使用：

```asm
.section ".text.boot", "ax"
```

建立 `.text.boot` input section。

Linker Script 使用：

```ld
KEEP(*(.text.boot))
```

將 boot code 放在 output `.text` 的最前面，並防止它在使用 `--gc-sections` 時被移除。

---

### `.bss (NOLOAD)`

`.bss` 在執行時需要記憶體空間，但不需要讓 `kernel8.img` 實際存放大量的 `0x00`。

因此使用：

```ld
.bss (NOLOAD)
```

在 image 中省略 `.bss` 的實際內容，再由 `boot.S` 於開機時清零。

---

### Stack 配置

```ld
.stack (NOLOAD) :
{
    __stack_bottom = .;
    . += 16K;
    __stack_top = .;
}
```

`. += 16K` 會將 Location Counter 往後移動 16 KiB：

```text
16 KiB = 16384 bytes = 0x4000
```

因此：

```text
__stack_top - __stack_bottom = 0x4000
```

AArch64 stack 向低位址成長，所以初始 Stack Pointer 設定為：

```asm
ldr x12, =__stack_top
mov sp, x12
```

---

## 記憶體配置

```text
低位址

0x80000
+----------------------------------+
| .text.boot                       |
| _start                           |
+----------------------------------+
| .text                            |
| kernel_main 與其他程式碼         |
+----------------------------------+
| .rodata                          |
| const 與字串常數                 |
+----------------------------------+
| .data                            |
| 有非零初始值的全域變數           |
+----------------------------------+ ← __bss_start
| .bss                             |
| 未初始化／零初始化全域變數       |
+----------------------------------+ ← __bss_end
                                     ← __stack_bottom
|                                  |
|        16 KiB Stack              |
|        向低位址成長              |
|                                  |
+----------------------------------+ ← __stack_top
                                     ← __kernel_end

高位址
```

---

## 建置流程

```text
boot.S
  │
  │ assembler
  ▼
boot.o

kernel.c
  │
  │ compiler
  ▼
kernel.o

boot.o + kernel.o + linker.ld
  │
  │ linker
  ▼
kernel8.elf

kernel8.elf
  │
  │ objcopy
  ▼
kernel8.img
```

執行：

```bash
make
```

預期產生：

```text
boot.o
kernel.o
kernel.map
kernel8.elf
kernel8.img
```

重新完整建置：

```bash
make clean
make
```

---

## 驗證方式

### 檢查 ELF Header

```bash
aarch64-linux-gnu-readelf -h kernel8.elf
```

確認：

```text
Class: ELF64
Machine: AArch64
Entry point address: 0x80000
```

---

### 檢查 Sections

```bash
aarch64-linux-gnu-readelf -S kernel8.elf
```

確認：

* `.text` 從 `0x80000` 開始。
* `.bss` 的 Type 為 `NOBITS`。
* `.stack` 的 Type 為 `NOBITS`。
* `.stack` 的 Size 為 `0x4000`。

---

### 檢查 Symbols

```bash
aarch64-linux-gnu-nm -n kernel8.elf
```

確認存在：

```text
_start
kernel_main
__kernel_start
__bss_start
__bss_end
__stack_bottom
__stack_top
__kernel_end
```

並確認：

```text
_start = 0x80000

__bss_start % 16 = 0
__bss_end   % 16 = 0

__stack_top - __stack_bottom = 0x4000
```

---

### 檢查反組譯

```bash
aarch64-linux-gnu-objdump -d kernel8.elf
```

確認 `_start` 中包含：

* 讀取 `MPIDR_EL1`。
* 判斷目前 Core ID。
* 設定 Stack Pointer。
* 清除 `.bss`。
* 呼叫 `kernel_main()`。
* 等待迴圈。

---

### 使用 QEMU 啟動

```bash
qemu-system-aarch64 \
    -M raspi3b \
    -kernel kernel8.img \
    -display none \
    -d in_asm \
    -D qemu.log
```

檢查執行紀錄：

```bash
grep -n "0x00080000" qemu.log
```

若能找到 `0x00080000` 的指令紀錄，表示 QEMU 已從預期的 kernel 載入地址開始執行。

---

# Basic Exercise 2：Mini UART

## 目標

在 Raspberry Pi 3 bare-metal kernel 中初始化 Mini UART，讓 kernel 可以和開發電腦進行字元傳輸。

目前採用：

- Raspberry Pi 3 Mini UART（UART1）
- Polling I/O
- 115200 baud
- 8-bit data
- No parity
- 1 stop bit
- 不使用 UART interrupt

在 QEMU 中，Mini UART 透過 `-serial stdio` 連接到目前的 terminal。

---

## 檔案結構

```text
boot.S
kernel.c
uart.c
uart.h
linker.ld
Makefile
````

各檔案用途：

* `uart.h`：宣告 UART 對外提供的函式。
* `uart.c`：實作 MMIO、GPIO 設定、UART 初始化與收發。
* `kernel.c`：呼叫 UART API，進行輸出或 echo 測試。

---

## UART API

`uart.h` 提供以下介面：

```c
void uart_init(void);
void uart_send(char c);
char uart_recv(void);
void uart_puts(const char *str);
```

### `uart_init()`

初始化 GPIO14、GPIO15 與 Mini UART。

### `uart_send()`

使用 polling 等待 TX FIFO 有空間，再傳送一個 byte。

### `uart_recv()`

使用 polling 等待 RX FIFO 有資料，再接收一個 byte。

### `uart_puts()`

逐字呼叫 `uart_send()` 輸出字串，並將 `\n` 轉換成 `\r\n`。

---

## MMIO

Raspberry Pi 使用 Memory-Mapped I/O 控制硬體。

CPU 對特定 physical address 執行 load/store，就等同於讀寫硬體 register。

```c
static void mmio_write(uintptr_t reg, uint32_t value)
{
    *(volatile uint32_t *)reg = value;
}

static uint32_t mmio_read(uintptr_t reg)
{
    return *(volatile uint32_t *)reg;
}
```

`uintptr_t` 用來保存 address 數值。在 AArch64 上通常是 8 bytes。

`uint32_t` 表示每次以 32-bit，也就是 4 bytes，存取 MMIO register。

`volatile` 要求 compiler 保留每一次實際 MMIO 存取，不能使用先前讀取的值取代新的讀取。

---

## Physical address

Broadcom datasheet 主要使用 `0x7E...` bus address。

Raspberry Pi 3 的 ARM CPU 存取 peripheral 時使用 `0x3F...` physical address。

例如：

```text
Bus address:     0x7E215040
Physical address: 0x3F215040
```

程式中使用的是 physical address。

---

## 使用到的 GPIO registers

```c
#define GPFSEL1   0x3F200004
#define GPPUD     0x3F200094
#define GPPUDCLK0 0x3F200098
```

### `GPFSEL1`

控制 GPIO10～GPIO19 的功能。

每個 GPIO 使用 3 bits：

```text
000 = Input
001 = Output
100 = ALT0
101 = ALT1
110 = ALT2
111 = ALT3
011 = ALT4
010 = ALT5
```

Mini UART 使用：

```text
GPIO14 ALT5 = TXD1
GPIO15 ALT5 = RXD1
```

GPIO14 對應 `GPFSEL1` bits 14:12。

GPIO15 對應 `GPFSEL1` bits 17:15。

設定時使用 Read-Modify-Write，避免改到同一個 register 裡其他 GPIO 的設定。

---

## GPIO pull-up/down

`GPPUD` 決定要套用的 pull 設定：

```text
00 = 關閉 pull-up/down
01 = pull-down
10 = pull-up
```

`GPPUDCLK0` 決定哪些 GPIO 接受設定：

```text
bit 14 = GPIO14
bit 15 = GPIO15
```

設定流程：

1. 對 `GPPUD` 寫入 0，選擇關閉 pull-up/down。
2. 等待至少 150 cycles。
3. 對 `GPPUDCLK0` 的 bit 14、15 寫入 1。
4. 再等待至少 150 cycles。
5. 清除 `GPPUD`。
6. 清除 `GPPUDCLK0`。

---

## Mini UART registers

```c
#define AUX_ENABLES      0x3F215004
#define AUX_MU_IO_REG    0x3F215040
#define AUX_MU_IER_REG   0x3F215044
#define AUX_MU_IIR_REG   0x3F215048
#define AUX_MU_LCR_REG   0x3F21504C
#define AUX_MU_MCR_REG   0x3F215050
#define AUX_MU_LSR_REG   0x3F215054
#define AUX_MU_CNTL_REG  0x3F215060
#define AUX_MU_BAUD_REG  0x3F215068
```

---

## 初始化流程

Mini UART 初始化順序：

1. 將 GPIO14、GPIO15 設為 ALT5。
2. 關閉 GPIO14、GPIO15 pull-up/down。
3. 使用 `AUX_ENABLES` bit 0 啟用 Mini UART。
4. 將 `AUX_MU_CNTL_REG` 設為 0，初始化期間關閉 TX/RX。
5. 將 `AUX_MU_IER_REG` 設為 0，關閉 interrupt。
6. 將 `AUX_MU_LCR_REG` 設為 3，使用 8-bit data。
7. 將 `AUX_MU_MCR_REG` 設為 0，不使用 RTS flow control。
8. 將 `AUX_MU_BAUD_REG` 設為 270，取得約 115200 baud。
9. 將 `AUX_MU_IIR_REG` 設為 6，清除 RX/TX FIFO。
10. 將 `AUX_MU_CNTL_REG` 設為 3，啟用 TX/RX。

Baud rate 計算：

```text
baud = system_clock / (8 × (baud_register + 1))

     = 250000000 / (8 × (270 + 1))

     ≈ 115314
```

---

## 傳送資料

傳送前檢查：

```text
AUX_MU_LSR_REG bit 5
```

bit 5 為 1，表示 TX FIFO 至少還能接受一個 byte。

流程：

```text
讀取 LSR bit 5
    ↓
沒有空間就持續等待
    ↓
寫入 AUX_MU_IO_REG
    ↓
字元進入 TX FIFO
```

`AUX_MU_IO_REG` 只使用最低 8 bits 作為傳送資料。

---

## 接收資料

接收前檢查：

```text
AUX_MU_LSR_REG bit 0
```

bit 0 為 1，表示 RX FIFO 至少有一個 byte。

流程：

```text
讀取 LSR bit 0
    ↓
沒有資料就持續等待
    ↓
讀取 AUX_MU_IO_REG
    ↓
取得最低 8 bits
```

---

## QEMU 資料流

啟動指令：

```bash
qemu-system-aarch64 \
    -M raspi3b \
    -kernel kernel8.img \
    -display none \
    -serial null \
    -serial stdio
```

Mini UART 是第二個 UART，所以使用：

```text
第一個 UART → null
第二個 UART → stdio
```

傳送方向：

```text
Guest kernel
    ↓
uart_send()
    ↓
寫入 AUX_MU_IO_REG
    ↓
QEMU Mini UART 模型
    ↓
QEMU stdout
    ↓
WSL terminal
```

接收方向：

```text
WSL 鍵盤
    ↓
QEMU stdin
    ↓
QEMU Mini UART RX FIFO
    ↓
uart_recv()
    ↓
Guest kernel
```

QEMU 不會產生真實 GPIO 電壓，而是直接把模擬 UART 的 byte 轉送到 terminal。

---

## TX 測試

```c
void kernel_main(void)
{
    uart_init();

    uart_puts("Hello Mini UART!\n");

    while (1) {
    }
}
```

成功輸出：

```text
Hello Mini UART!
```

這代表下列功能正常：

* kernel 成功執行
* GPIO 與 Mini UART 初始化成功
* UART TX polling 正常
* QEMU serial backend 設定正確

---

## RX/TX echo 測試

```c
void kernel_main(void)
{
    uart_init();

    uart_puts("UART echo test ready\n");

    while (1) {
        char c = uart_recv();

        uart_send('[');
        uart_send(c);
        uart_send(']');
    }
}
```

輸入 `a` 時，預期看到：

```text
[a]
```

使用括號可以避免將 terminal local echo 誤認為 kernel 回傳的字元。

---

## 換行處理

不同 terminal 對換行字元的處理可能不同：

```text
\r = Carriage Return，回到行首
\n = Line Feed，移到下一行
```

目前 `uart_puts()` 遇到 `\n` 時，會先傳送 `\r`：

```c
if (*str == '\n') {
    uart_send('\r');
}
```

因此輸出實際使用：

```text
\r\n
```

這可以避免後續文字逐行向右偏移。

---

## 目前限制

目前 UART 使用 polling：

* 等待 TX 時 CPU 會持續讀取 status register。
* 等待 RX 時 CPU 會停在 `uart_recv()`。
* 尚未使用 interrupt。
* 尚未使用 ring buffer。
* 尚未處理 receiver overrun。
* Mini UART baud rate 依賴 system clock。

Polling UART 足以支援下一個 Basic Exercise 的 Simple Shell。

```

目前這份 README 最後的驗證狀態，可以依你的實測結果調整：

- 已看到 `Hello Mini UART!`：可以確認 TX 已驗證。
- 已使用 `[c]` 測試成功：才寫 RX/TX echo 已驗證。
- 只看到鍵盤輸入本身：不要先宣稱 RX 已驗證，因為可能是 terminal local echo。
```
# Basic Exercise 4：Mailbox

## 目標

透過 Raspberry Pi 的 Mailbox 機制，讓 ARM CPU 向 VideoCore firmware 查詢硬體資訊，並使用 Mini UART 印出：

* Board revision
* ARM memory base address
* ARM memory size

預期輸出範例：

```text
Board revision: 0x00A02082
ARM memory base: 0x00000000
ARM memory size: 0x3C000000
```

---

## 1. Mailbox 是什麼？

Raspberry Pi 中包含兩個主要處理端：

* ARM CPU：執行我們撰寫的 bare-metal kernel
* VideoCore：執行 Raspberry Pi firmware，負責部分硬體資訊與周邊設定

ARM CPU 無法直接呼叫 VideoCore 的函式，因此需要透過 **Mailbox** 進行溝通。

Mailbox 可以用來：

* 查詢 Board revision
* 查詢 ARM memory
* 查詢或設定 clock rate
* 設定 framebuffer
* 查詢板子序號或 MAC address

本次 Exercise 使用 Mailbox Property Interface，對應的 channel 為：

```c
#define MAILBOX_CHANNEL_PROPERTY 8
```

---

## 2. Mailbox 的通訊方式

Mailbox 不會直接將完整的 request 內容逐筆寫入 MMIO register。

ARM 會先在一般 RAM 中建立一個 message buffer：

```c
static volatile uint32_t mailbox[12]
    __attribute__((aligned(16)));
```

接著透過 Mailbox MMIO register 傳送：

```text
message buffer address + channel
```

VideoCore 收到後，會根據 address 找到 `mailbox[]`，讀取 request，並將結果寫回同一個 buffer。

整體流程如下：

```text
ARM 建立 mailbox[] message
        ↓
透過 MAILBOX_WRITE 傳送 buffer address + channel
        ↓
VideoCore 讀取 mailbox[]
        ↓
VideoCore 處理 Property Tags
        ↓
VideoCore 將結果寫回 mailbox[]
        ↓
ARM 從 MAILBOX_READ 收到完成通知
        ↓
ARM 讀取 mailbox[] 中的結果
```

---

## 3. Mailbox MMIO Registers

Raspberry Pi 3 的 Peripheral MMIO base address 為：

```c
#define MMIO_BASE       0x3F000000u
#define MAILBOX_BASE    (MMIO_BASE + 0xB880u)
```

本次使用三個 Mailbox registers：

```c
#define MAILBOX_READ    (MAILBOX_BASE + 0x00u)
#define MAILBOX_STATUS  (MAILBOX_BASE + 0x18u)
#define MAILBOX_WRITE   (MAILBOX_BASE + 0x20u)
```

### `MAILBOX_WRITE`

ARM 將 message buffer address 與 channel 寫入此 register，通知 VideoCore 處理 request。

```c
mmio_write(MAILBOX_WRITE, request);
```

### `MAILBOX_STATUS`

用來查看 Mailbox FIFO 是否可以讀取或寫入。

```c
#define MAILBOX_EMPTY 0x40000000u
#define MAILBOX_FULL  0x80000000u
```

* `MAILBOX_FULL`：bit 31，表示目前不能寫入
* `MAILBOX_EMPTY`：bit 30，表示目前沒有 response 可以讀取

等待可以寫入：

```c
while (mmio_read(MAILBOX_STATUS) & MAILBOX_FULL) {
}
```

等待可以讀取：

```c
while (mmio_read(MAILBOX_STATUS) & MAILBOX_EMPTY) {
}
```

### `MAILBOX_READ`

ARM 從此 register 讀取 VideoCore 傳回的完成通知。

```c
uint32_t response = mmio_read(MAILBOX_READ);
```

讀到的內容主要是：

```text
message buffer address + channel
```

真正的 Board revision 與 ARM memory 資訊仍然存放在 `mailbox[]` 中。

---

## 4. MMIO Read / Write

Mailbox registers 使用 MMIO 存取：

```c
static void mmio_write(uintptr_t reg, uint32_t value)
{
    *(volatile uint32_t *)reg = value;
}

static uint32_t mmio_read(uintptr_t reg)
{
    return *(volatile uint32_t *)reg;
}
```

這裡使用 `volatile`，是因為 MMIO register 的內容可能由硬體自行改變。

Compiler 必須真的執行每一次 register read/write，不能直接使用之前暫存在 CPU register 中的舊值。

---

## 5. Message Buffer 格式

Mailbox Property Interface 的 message buffer 格式為：

```text
u32：整個 buffer 的大小
u32：buffer request/response code

一個或多個 Property Tags

u32：End Tag
```

其中：

```text
u8  = unsigned 8-bit integer
u32 = unsigned 32-bit integer
```

在 C 中可分別對應：

```c
uint8_t
uint32_t
```

---

## 6. Request 與 Response Code

```c
#define REQUEST_CODE     0x00000000u
#define REQUEST_SUCCEED  0x80000000u
#define REQUEST_FAILED   0x80000001u
```

送出 request 前：

```c
mailbox[1] = REQUEST_CODE;
```

VideoCore 處理成功後，會改寫為：

```c
mailbox[1] = REQUEST_SUCCEED;
```

如果 request buffer 格式解析失敗，可能改寫為：

```c
mailbox[1] = REQUEST_FAILED;
```

---

## 7. Property Tag 格式

每一個 tag 的格式為：

```text
u32：Tag identifier
u32：Value buffer size
u32：Tag request/response code
u8...：Value buffer
```

Tag request code：

```c
#define TAG_REQUEST_CODE 0x00000000u
```

送出前 bit 31 為 0，代表這是一個 request。

VideoCore 處理後，bit 31 會被設為 1，其餘 bits 表示實際回傳的資料長度。

例如：

```text
0x80000004
```

表示：

```text
bit 31 = 1：這是一個 response
length = 4 bytes
```

---

## 8. 查詢 Board Revision

Board revision 的 Property Tag ID：

```c
#define GET_BOARD_REVISION 0x00010002u
```

它不需要 request 輸入參數，並回傳一個 `uint32_t`，因此 Value buffer 大小為 4 bytes。

```c
mailbox[2] = GET_BOARD_REVISION;
mailbox[3] = 4;
mailbox[4] = TAG_REQUEST_CODE;
mailbox[5] = 0;
```

成功後：

```c
mailbox[5]
```

會存放 Board revision。

---

## 9. 查詢 ARM Memory

ARM memory 的 Property Tag ID：

```c
#define GET_ARM_MEMORY 0x00010005u
```

它會回傳兩個 `uint32_t`：

```text
ARM memory base address
ARM memory size
```

因此 Value buffer 大小為：

```text
2 × 4 bytes = 8 bytes
```

Message 內容：

```c
mailbox[6] = GET_ARM_MEMORY;
mailbox[7] = 8;
mailbox[8] = TAG_REQUEST_CODE;
mailbox[9] = 0;
mailbox[10] = 0;
```

成功後：

```c
mailbox[9]   // ARM memory base
mailbox[10]  // ARM memory size
```

---

## 10. 將兩個 Tags 放在同一個 Message

Mailbox Property Interface 允許在同一個 message 中連續放入多個 tags。

本次 message 結構為：

```text
Buffer header        2 個 uint32_t
Board Revision tag   4 個 uint32_t
ARM Memory tag       5 個 uint32_t
End tag              1 個 uint32_t
--------------------------------
總共                12 個 uint32_t
```

所以 buffer 宣告為：

```c
static volatile uint32_t mailbox[12]
    __attribute__((aligned(16)));
```

完整 message：

```c
mailbox[0] = 12 * 4;
mailbox[1] = REQUEST_CODE;

/* Tag 1：Board Revision */
mailbox[2] = GET_BOARD_REVISION;
mailbox[3] = 4;
mailbox[4] = TAG_REQUEST_CODE;
mailbox[5] = 0;

/* Tag 2：ARM Memory */
mailbox[6] = GET_ARM_MEMORY;
mailbox[7] = 8;
mailbox[8] = TAG_REQUEST_CODE;
mailbox[9] = 0;
mailbox[10] = 0;

mailbox[11] = END_TAG;
```

---

## 11. 為什麼需要 16-byte Alignment？

Mailbox register 的 32-bit 資料格式為：

```text
31                           4 3          0
+-----------------------------+------------+
| Message buffer address      | Channel ID |
+-----------------------------+------------+
```

低 4 bits 用來存放 channel，因此 message buffer address 的低 4 bits 必須為零。

```text
2⁴ = 16
```

所以 buffer 必須放在 16-byte aligned 的地址：

```c
__attribute__((aligned(16)))
```

這代表 buffer 起始位址必須是 16 的倍數，並不是預留 16 bytes 不使用。

---

## 12. 組合 Buffer Address 與 Channel

函式接收到 message buffer pointer：

```c
volatile uint32_t *message
```

先將 pointer 轉成可以保存完整地址的整數：

```c
uintptr_t mailbox_address = (uintptr_t)message;
```

接著建立要寫入 32-bit Mailbox register 的 request：

```c
uint32_t request =
    ((uint32_t)mailbox_address & 0xFFFFFFF0u)
    | MAILBOX_CHANNEL_PROPERTY;
```

其中：

```text
& 0xFFFFFFF0
```

清除地址最低 4 bits。

```text
| MAILBOX_CHANNEL_PROPERTY
```

將 channel 8 放入最低 4 bits。

例如：

```text
Buffer address：0x00081000
Channel：       0x00000008
Request：       0x00081008
```

---

## 13. `mailbox_call()`

`mailbox_call()` 負責通用的 Mailbox 傳輸，不負責解讀特定硬體資訊。

```c
static int mailbox_call(volatile uint32_t *message)
{
    uintptr_t mailbox_address = (uintptr_t)message;

    uint32_t request =
        ((uint32_t)mailbox_address & 0xFFFFFFF0u)
        | MAILBOX_CHANNEL_PROPERTY;

    /* 等待 Mailbox 可以寫入 */
    while (mmio_read(MAILBOX_STATUS) & MAILBOX_FULL) {
    }

    /* 通知 VideoCore 處理 message */
    mmio_write(MAILBOX_WRITE, request);

    while (1) {
        /* 等待有 response 可以讀 */
        while (mmio_read(MAILBOX_STATUS) & MAILBOX_EMPTY) {
        }

        uint32_t response = mmio_read(MAILBOX_READ);

        /* 確認這是目前 request 的完成通知 */
        if (response == request) {
            return message[1] == REQUEST_SUCCEED;
        }
    }
}
```

回傳值：

```text
1：VideoCore 成功處理 message
0：message 處理失敗
```

真正的硬體資訊仍然由 VideoCore 寫回 `message[]`。

---

## 14. HardwareInfo 結構

為了將三個結果一起回傳給 `kernel.c`，定義：

```c
typedef struct {
    uint32_t board_revision;
    uint32_t memory_base;
    uint32_t memory_size;
} HardwareInfo;
```

此結構應放在 `mailbox.h`，讓 `mailbox.c` 與 `kernel.c` 都能使用相同的型別。

### `mailbox.h`

```c
#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>

typedef struct {
    uint32_t board_revision;
    uint32_t memory_base;
    uint32_t memory_size;
} HardwareInfo;

int get_hardware_info(HardwareInfo *info);

#endif
```

---

## 15. 取得硬體資訊

```c
int get_hardware_info(HardwareInfo *info)
{
    if (info == 0) {
        return 0;
    }

    mailbox[0] = 12 * 4;
    mailbox[1] = REQUEST_CODE;

    /* Board Revision */
    mailbox[2] = GET_BOARD_REVISION;
    mailbox[3] = 4;
    mailbox[4] = TAG_REQUEST_CODE;
    mailbox[5] = 0;

    /* ARM Memory */
    mailbox[6] = GET_ARM_MEMORY;
    mailbox[7] = 8;
    mailbox[8] = TAG_REQUEST_CODE;
    mailbox[9] = 0;
    mailbox[10] = 0;

    mailbox[11] = END_TAG;

    if (!mailbox_call(mailbox)) {
        return 0;
    }

    info->board_revision = mailbox[5];
    info->memory_base = mailbox[9];
    info->memory_size = mailbox[10];

    return 1;
}
```

---

## 16. 在 `kernel_main()` 印出結果

```c
void kernel_main(void)
{
    uart_init();
    uart_puts("Welcome to my shell!\n");

    HardwareInfo info;

    if (get_hardware_info(&info)) {
        uart_puts("Board revision: ");
        uart_hex(info.board_revision);

        uart_puts("\nARM memory base: ");
        uart_hex(info.memory_base);

        uart_puts("\nARM memory size: ");
        uart_hex(info.memory_size);

        uart_puts("\n");
    } else {
        uart_puts("Mailbox request failed\n");
    }

    shell_run();
}
```

---

## 17. 執行結果

使用 QEMU Raspberry Pi 3 Model B：

```bash
qemu-system-aarch64 \
    -M raspi3b \
    -kernel kernel8.img \
    -display none \
    -serial null \
    -serial stdio
```

輸出：

```text
Welcome to my shell!
Board revision: 0x00A02082
ARM memory base: 0x00000000
ARM memory size: 0x3C000000
#
```

結果說明：

```text
Board revision：0xA02082
```

對應 Raspberry Pi 3 Model B。

```text
ARM memory base：0x00000000
```

ARM memory 從實體位址 0 開始。

```text
ARM memory size：0x3C000000
```

換算為：

```text
960 MiB
```

QEMU 的 Raspberry Pi 3 模型具有 1 GiB RAM，其中約 64 MiB 保留給 VideoCore，因此 ARM 可使用約 960 MiB。

---

## 18. 常見錯誤

### MMIO register 沒有填入地址

錯誤：

```c
#define MAILBOX_READ
```

正確：

```c
#define MAILBOX_READ (MAILBOX_BASE + 0x00u)
```

### 將 flag 當成 MMIO address

錯誤：

```c
mmio_read(MAILBOX_EMPTY);
```

正確：

```c
mmio_read(MAILBOX_STATUS) & MAILBOX_EMPTY;
```

`MAILBOX_STATUS` 是 register address，`MAILBOX_EMPTY` 是 bit mask。

### 使用 `&&` 檢查 flag

錯誤：

```c
status && MAILBOX_EMPTY
```

正確：

```c
status & MAILBOX_EMPTY
```

`&` 用來檢查特定位元；`&&` 只會判斷兩個數值是否非零。

### Buffer 大小錯誤

如果使用 `mailbox[0]` 到 `mailbox[11]`，陣列必須宣告成：

```c
uint32_t mailbox[12];
```

不能仍然使用：

```c
uint32_t mailbox[7];
```

否則會發生越界寫入。

### 忘記將 `mailbox.c` 加入 Makefile

如果 linker 出現：

```text
undefined reference to `get_hardware_info'
```

需確認：

* `mailbox.c` 中函式名稱與 header 一致
* 公開函式沒有加上 `static`
* `mailbox.c` 有被編譯成 `mailbox.o`
* `mailbox.o` 有被加入 linker object list

---

## 總結

本 Exercise 完成了以下功能：

1. 建立符合 Mailbox Property Interface 格式的 message buffer。
2. 使用 Channel 8 與 VideoCore firmware 通訊。
3. 在同一個 message 中放入兩個 Property Tags。
4. 查詢 Board revision。
5. 查詢 ARM memory base address 與 size。
6. 使用 Mini UART 印出查詢結果。

Mailbox 的核心概念是：

> ARM 將 request 寫入一般 RAM 的 message buffer，再透過 MMIO 傳送 buffer address 與 channel；VideoCore 處理後將 response 寫回同一個 buffer。

---

# Lab 1 觀念與口試複習筆記

## 1. 從 source code 到 Raspberry Pi 開機

整體流程：

```text
boot.S
  ↓ assembler
boot.o

kernel.c / uart.c / shell.c / mailbox.c
  ↓ AArch64 cross compiler
kernel.o / uart.o / shell.o / mailbox.o

所有 .o + linker.ld
  ↓ linker
kernel8.elf

kernel8.elf
  ↓ objcopy -O binary
kernel8.img

kernel8.img
  ↓ Raspberry Pi firmware
載入實體記憶體 0x80000，從該位置開始執行
```

### Object file

assembler 將 `boot.S` 組譯成 object file；cross compiler 將各個 C source file 編譯成 object file。

Object file 包含：

- machine code；
- `.text`、`.data`、`.bss` 等 input sections；
- 尚未解析完成的 symbol reference；
- relocation information。

此時不同 object file 之間的 function 和 symbol 位址尚未完全決定。

### Linker 與 linker script

`linker.ld` 是給 linker 使用的 memory-layout 規格，不是給 Raspberry Pi firmware 使用。

Linker 根據它：

- 決定各個 output section 的順序與位址；
- 解析不同 `.o` 之間的 symbol reference；
- 計算 `_start`、`kernel_main`、`__bss_start`、`__stack_top` 等 symbol 的位址；
- 產生 `kernel8.elf`。

### ELF 與 raw image

`kernel8.elf` 包含：

- machine code；
- section table；
- program header；
- symbol table；
- entry point；
- debug information。

`objcopy -O binary` 會將 ELF 轉為 Raspberry Pi firmware 可以直接載入的 raw `kernel8.img`。Raw image 不含 ELF header、symbol table 和 debug metadata。

Raspberry Pi firmware 不會讀取 `linker.ld`，也不會從 raw image 中尋找 `_start` symbol；它只是把 image 的第一個 byte 放到 `0x80000`，再把 PC 設到該位置開始執行。

---

## 2. 為什麼 linker script 從 `0x80000` 開始

Raspberry Pi 3 firmware 會把 `kernel8.img` 載入實體位址 `0x80000`：

```ld
. = 0x80000;
```

這行告訴 linker：

> 請將接下來的 section 和 symbol 當作位於 `0x80000` 之後。

Firmware 的實際載入位址與 linker 假設的位址必須一致。

若 linker 假設從 `0x90000` 開始，但 firmware 仍載入 `0x80000`：

- image 第一條指令可能仍暫時可以執行；
- PC-relative branch 也可能因相對距離不變而暫時正常；
- 但 `__stack_top`、`__bss_start`、`__bss_end` 等 linker symbol 會差 `0x10000`；
- stack pointer 會指向錯誤區域；
- boot code 會清除錯誤的 BSS 範圍；
- global/static variables 無法保證正確初始化。

因此 linker script 的起點不是單純的註解，而是 linker 計算所有位址的基準。

---

## 3. `ENTRY(_start)` 與 `.text.boot`

```ld
ENTRY(_start)
```

`ENTRY(_start)` 是給 linker 使用的。Linker 會把 `_start` 寫入 ELF header 的 entry-point 欄位。

ELF-aware debugger 或 loader 可以讀取這個 entry point，但 Raspberry Pi firmware 使用的是 raw `kernel8.img`，不會讀取 ELF entry point。

因此還必須確保 `_start` 的 machine code 實際位於 raw image 的第一個位置：

```ld
.text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
}
```

### 為什麼 `.text.boot` 要放最前面

Firmware 從 `0x80000` 直接執行第一條指令，所以 image 最前面必須是 `_start`，不能是一般 C function。

若一般 `.text` 排在 `.text.boot` 前面，CPU 可能直接進入 `kernel_main`、`uart_init` 或其他 C function，此時：

- `sp` 尚未設定；
- `.bss` 尚未清零；
- secondary cores 尚未停止；
- C function 一旦使用 stack 或 global data 就可能出錯。

### `KEEP` 的用途

`KEEP(*(.text.boot))` 是 link-time 的保險：

> 即使 linker 啟用 section garbage collection，也不要移除 `.text.boot`。

Boot code 通常只在每次開機時執行一次，但它仍必須存在於 image 中。`KEEP` 不是讓它重複執行，也不是 runtime 的設定。

---

## 4. 多核心初始化

Raspberry Pi 3 有四個 ARM cores。開機時需要判斷目前是哪個 core：

```asm
mrs x0, mpidr_el1
and x0, x0, #0xff
cbz x0, primary_core
```

### `MPIDR_EL1`

`MPIDR_EL1` 是 ARM 的 Multiprocessor Affinity Register，包含目前 CPU core 的 affinity 資訊。

最低 8 bits 是 `Aff0`。在這個 Lab 的環境中：

```text
Core 0 → Aff0 = 0
Core 1 → Aff0 = 1
Core 2 → Aff0 = 2
Core 3 → Aff0 = 3
```

因此：

```asm
and x0, x0, #0xff
```

取出 `Aff0`；`cbz` 則讓 Aff0 為 0 的 Core 0 進入 primary initialization。

### 為什麼只有 Core 0 初始化

若所有 core 都繼續執行：

- 多個 core 可能同時清除 `.bss`；
- Core 0 已經寫入的 global data 可能被其他 core 再次清成 0；
- 所有 core 都可能使用同一個 `__stack_top`；
- UART、mailbox 與 shell 可能同時被多個 core 操作。

雖然每個 core 都有自己的 `sp` register，但如果它們都把 `sp` 設成同一個數值，就會使用相同的 RAM stack：

```text
Core 0 push stack frame → 寫入 0x84bf0
Core 1 push stack frame → 也寫入 0x84bf0
```

local variables、saved registers 和 return address 會互相覆寫。

Secondary cores 因此停在：

```asm
secondary_core_hang:
    wfe
    b secondary_core_hang
```

`wfe` 是 Wait For Event，讓 core 進入等待狀態，避免持續 busy waiting。

---

## 5. 為什麼必須先設定 stack pointer

進入 C function 前必須先設定：

```asm
ldr x12, =__stack_top
mov sp, x12
```

C function 可能需要 stack 來保存：

- return address；
- callee-saved registers；
- local variables；
- function arguments；
- compiler 產生的 temporary values。

即使 `kernel_main()` 本身看起來簡單，它呼叫其他 function 時也可能立刻使用 stack。

AArch64 ABI 要求 stack pointer 在 function-call boundary 維持 16-byte alignment，因此 linker script 也要對齊 stack：

```ld
. = ALIGN(16);

.stack (NOLOAD) : {
    __stack_bottom = .;
    . += 16K;
    __stack_top = .;
}
```

`16K` 是保留的 stack 容量，不代表 stack 必須清成 0。

---

## 6. `.bss (NOLOAD)` 的真正意思

`.bss` 放置沒有明確非零初始值的 global/static variables，例如：

```c
int counter;
static char buffer[128];
static unsigned int index;
```

C 語言要求這些變數在程式開始時初始值為 0。

### `NOLOAD` 與 image 的關係

```ld
.bss (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
}
```

`NOLOAD` 表示：

- `.bss` 有 runtime RAM 位址；
- `.bss` 有大小；
- 但沒有對應的初始資料 bytes 存放在 `kernel8.img` 中。

流程是：

```text
Linker 為 BSS 保留 RAM 位址
        ↓
kernel8.img 不放大量的 0 bytes
        ↓
Firmware 載入 image 時不會寫入那段 BSS RAM
        ↓
該 RAM 原始內容未知
        ↓
boot.S 手動清成 0
```

這能縮小 image，減少 firmware 需要複製的 bytes；但清零本身仍會花費 CPU 時間。

### 為什麼 linker 不會自動清 BSS

Linker 是 build-time 工具，只能：

- 配置 section；
- 計算位址；
- 產生 image。

Linker 不會在 Raspberry Pi runtime 執行，因此不能在開機後替 CPU 寫 RAM。一般作業系統或 C runtime 會提供 startup code；bare-metal kernel 必須自己實作這一段。

---

## 7. 一次清除 16 bytes 為什麼安全

Boot code 使用：

```asm
stp xzr, xzr, [x9], #16
```

一個 `x` register 是 64 bits，也就是 8 bytes：

```text
xzr + xzr = 8 + 8 = 16 bytes
```

每次 `stp`：

1. 將 16 bytes 的 0 寫入 `[x9]`；
2. 將 `x9` 增加 16。

這要求：

- `__bss_start` 16-byte aligned；
- `__bss_end` 16-byte aligned；
- BSS 大小是 16 的倍數。

Linker script 在 BSS 前後都進行 alignment，因此 `x9` 會剛好到達 `__bss_end`，不會清到下一個 section。

---

## 8. MMIO 與 address space

MMIO（Memory-Mapped I/O）讓 CPU 透過 load/store instruction 操作硬體 register：

```c
*(volatile uint32_t *)reg = value;
```

該 address 看起來像 memory address，但實際對應的是 UART、GPIO 或 mailbox register，而不是普通 RAM。

### Bus address 與 ARM physical address

BCM2837 datasheet 常使用 bus address：

```text
Mini UART IO bus address：0x7E215040
```

但 ARM CPU 在這個 bare-metal Lab 中直接使用 physical address：

```text
Mini UART IO physical address：0x3F215040
```

RPi3 的 mapping：

```text
0x7E000000 bus address
        ↓
0x3F000000 ARM physical address
```

因此程式中的 MMIO register 必須使用 `0x3F...`，不能直接把 datasheet 的 `0x7E...` address 當作 CPU physical address。

---

## 9. `volatile` 的用途

如果 MMIO pointer 沒有 `volatile`，compiler 看見：

```c
while ((*(uint32_t *)LSR & (1u << 5)) == 0) {
}
```

可能推論：

> 程式本身沒有修改 `*LSR`，所以它的值不會改變。

因此 compiler 可能只讀一次：

```text
讀取 LSR 一次
如果 bit 5 是 0
就進入永遠不再讀 LSR 的迴圈
```

但 UART hardware 會自行更新 LSR。即使 CPU 程式沒有寫它，bit 5 仍可能稍後從 0 變成 1。

使用：

```c
*(volatile uint32_t *)reg
```

是在告訴 compiler：

> 每次存取都必須真的執行，不得省略、快取或把讀取搬出 polling loop。

在 AArch64 上，polling loop 的概念會是：

```asm
loop:
    ldr w0, [x0]
    tbz w0, #5, loop
```

注意：這裡所說的「compiler 快取」通常是把值保留在 CPU general-purpose register 中，不等於 CPU hardware cache。

---

## 10. `volatile` 與 memory barrier 的差異

`volatile` 解決的是 compiler optimization：

```text
確保 compiler 真的產生每一次 read/write。
```

它不保證 CPU、cache、interconnect 和不同硬體裝置觀察到 memory operation 的順序。

以 mailbox 為例，程式希望：

```text
1. CPU 先把完整 request 寫到 mailbox[] RAM
2. CPU 寫 MAILBOX_WRITE register 通知 GPU
3. GPU 再讀 mailbox[]
```

若缺乏適當的 memory ordering，理論上 GPU 可能先看見通知，再看見完整的 RAM 更新。

ARM memory barrier，例如 `dmb`，處理的是：

```text
先前 memory access 與後續 memory access 的硬體可見順序。
```

簡單記法：

```text
volatile → 管 compiler 是否真的執行存取
dmb      → 管 CPU／硬體觀察 memory access 的順序
```

Lab 1 的簡化環境可能在沒有明確 barrier 時仍能正常執行，但兩者的概念不能混在一起。

---

## 11. GPIO14、GPIO15 與 ALT5

GPIO pin 可以透過 pin multiplexing 連接不同的 hardware peripheral。

對 Raspberry Pi 3 mini UART：

```text
GPIO14 ALT5 → TXD1 → mini UART transmit
GPIO15 ALT5 → RXD1 → mini UART receive
```

因此要修改 `GPFSEL1`：

```text
GPIO14：bits 14:12 = 010（ALT5）
GPIO15：bits 17:15 = 010（ALT5）
```

ALT5 不是因為「GPIO 沒有其他用途」，而是 Raspberry Pi hardware 明確定義 ALT5 對應 mini UART。

### 為什麼先設定 GPIO 再 enable UART

Mini UART 一旦 enable，就可能開始接收 RX pin 的訊號。

若 GPIO 尚未切到正確 function、RX pin 浮動或處於低電位，UART 可能把它誤認為 start bit，收到 garbage byte，甚至塞滿 RX FIFO。

因此合理順序是：

```text
設定 GPIO function
→ 關閉 pull-up/down
→ enable mini UART module
→ 設定 UART registers
→ 最後開啟 TX/RX
```

---

## 12. Mini UART 初始化概念

主要設定：

| Register | 設定 | 用途 |
|---|---:|---|
| `AUX_ENABLES` | bit 0 = 1 | 啟用 mini UART，允許存取其 registers |
| `AUX_MU_CNTL_REG` | 0 | 設定期間先關閉 transmitter/receiver |
| `AUX_MU_IER_REG` | 0 | 關閉 UART interrupts，使用 polling |
| `AUX_MU_LCR_REG` | 3 | 設定 8-bit data |
| `AUX_MU_MCR_REG` | 0 | 不使用 RTS/flow-control 相關功能 |
| `AUX_MU_BAUD_REG` | 270 | 約 115200 baud |
| `AUX_MU_IIR_REG` | 6 | 清除 RX/TX FIFO |
| `AUX_MU_CNTL_REG` | 3 | 最後啟用 transmitter 和 receiver |

設定期間先關閉 TX/RX，是為了避免 UART 在 baud rate、data format 和 FIFO 尚未設定完成時收發資料。

### Baud rate

Mini UART baud rate：

```text
baud = system_clock / (8 × (baud_register + 1))
```

系統時脈為 250 MHz：

```text
250000000 / (8 × (270 + 1))
≈ 115313
```

接近目標 115200 baud。

---

## 13. UART polling send/receive

### 傳送

`AUX_MU_LSR_REG` bit 5 表示：

> TX FIFO 至少還可以接受一個 byte。

```c
while ((mmio_read(AUX_MU_LSR_REG) & (1u << 5)) == 0) {
}
mmio_write(AUX_MU_IO_REG, c);
```

注意 bit 5 不代表整個 transmitter 已完全 idle；它只代表 FIFO 有空間可以接受下一個 byte。

### 接收

`AUX_MU_LSR_REG` bit 0 表示：

> RX FIFO 至少有一個可讀 byte。

```c
while ((mmio_read(AUX_MU_LSR_REG) & 1u) == 0) {
}
return mmio_read(AUX_MU_IO_REG) & 0xff;
```

這種作法是 polling：

- CPU 持續檢查 status；
- 沒有使用 interrupt；
- 實作簡單，但等待期間 CPU 無法做其他工作。

---

## 14. QEMU 為什麼要兩個 `-serial`

QEMU 的 `raspi3b` 有兩個 UART：

```text
UART0 → PL011
UART1 → mini UART
```

只寫：

```bash
-serial stdio
```

通常會把 terminal 接到第一個 UART，也就是 PL011。

本 Lab 使用 mini UART，因此應使用：

```bash
-serial null -serial stdio
```

意思是：

```text
UART0 / PL011 → null
UART1 / mini UART → stdio
```

完整指令：

```bash
qemu-system-aarch64 \
    -M raspi3b \
    -kernel build/kernel8.img \
    -display none \
    -serial null \
    -serial stdio
```

---

## 15. `\r`、`\n` 與 CRLF

```text
\r：Carriage Return，游標回到目前這一行的最左側
\n：Line Feed，游標移到下一行
```

傳統 terminal 通常使用：

```text
\r\n
```

表示完整換行。

### 輸出處理

`uart_puts()` 遇到 `\n` 時先傳送 `\r`：

```c
if (*str == '\n') {
    uart_send('\r');
}
uart_send(*str);
```

因此 caller 應只傳入：

```c
uart_puts("\n");
```

不應傳入：

```c
uart_puts("\r\n");
```

否則實際會輸出：

```text
\r\r\n
```

目前 `kernel.c` 中若存在 `uart_puts("\r\n...")`，應注意會與 `uart_puts()` 的自動轉換重複。

### 輸入處理

不同 terminal 按下 Enter 時可能送：

```text
\r
```

或：

```text
\n
```

或：

```text
\r\n
```

若 shell 將 `\r\n` 當成兩次 Enter，就會執行一次 command 後，再處理一次空 command，額外印出 prompt。

因此 shell 使用 `is_prev_r`：

```text
收到 \r → 執行命令，記錄上一個是 \r
下一個是 \n → 視為同一次 Enter 的第二半，直接忽略
```

---

## 16. Shell command 處理流程

使用者輸入：

```text
hello<Enter>
```

流程：

```text
uart_recv() 取得 h
→ uart_send('h') echo
→ buffer[0] = 'h'

依序接收 e、l、l、o
→ echo
→ 存入 buffer

收到 \r 或 \n
→ 輸出換行
→ buffer[index] = '\0'
→ 比對 command
→ 輸出結果
→ 顯示下一個 prompt
```

### 為什麼需要 `'\0'`

UART 收到的是獨立 byte，不會自動產生 C string terminator。

因此 Enter 時必須：

```c
buffer[index] = '\0';
```

否則 `str_equal()` 會繼續讀取 command 後方的舊資料甚至越界，造成比對錯誤或 undefined behavior。

### 為什麼限制到 `sizeof(buffer) - 1`

```c
if (index < sizeof(buffer) - 1)
```

128-byte buffer 最多只能存 127 個輸入字元，最後一格必須保留給 `'\0'`：

```text
127 command bytes + 1 terminator = 128 bytes
```

---

## 17. Mailbox 的用途

Mailbox 是 ARM CPU 與 VideoCore/GPU firmware 之間的溝通機制。

ARM 可以透過 property interface 查詢：

- board revision；
- ARM memory base；
- ARM memory size；
- clock rate；
- framebuffer configuration；
- 其他由 firmware 管理的硬體資訊。

Channel 8 是 property interface channel。

---

## 18. Mailbox address 與 channel

Mailbox register 的 32-bit request word：

```text
31                         4 3            0
+---------------------------+--------------+
| message buffer address    | channel ID   |
+---------------------------+--------------+
```

低 4 bits 用於 channel，因此 message buffer 必須 16-byte aligned：

```c
static volatile uint32_t mailbox[12]
    __attribute__((aligned(16)));
```

16-byte alignment 保證 pointer 的低 4 bits 原本都是 0。

Request：

```c
uint32_t request =
    ((uint32_t)mailbox_address & 0xfffffff0u)
    | MAILBOX_CHANNEL_PROPERTY;
```

其中：

```text
& 0xfffffff0 → 清除低 4 bits
| 8          → 放入 property channel 8
```

如果 buffer 沒有 16-byte aligned，address 的低位會被 channel 覆蓋，GPU 將讀取錯誤的 buffer address。

---

## 19. Mailbox message layout

目前 message 有 12 個 `uint32_t`：

```text
Message header              2 words
Board revision tag          4 words
ARM memory tag              5 words
End tag                     1 word
-----------------------------------
Total                      12 words
```

一個 word 是 4 bytes：

```text
12 × 4 = 48 bytes
```

詳細排列：

| Index | 內容 |
|---:|---|
| `[0]` | 整個 message 大小，48 bytes |
| `[1]` | 整體 request/response code |
| `[2]` | `GET_BOARD_REVISION` tag |
| `[3]` | board revision value buffer 大小，4 bytes |
| `[4]` | tag request/response code |
| `[5]` | GPU 寫回 board revision |
| `[6]` | `GET_ARM_MEMORY` tag |
| `[7]` | ARM memory value buffer 大小，8 bytes |
| `[8]` | tag request/response code |
| `[9]` | GPU 寫回 ARM memory base |
| `[10]` | GPU 寫回 ARM memory size |
| `[11]` | end tag |

即使是 GET request，仍要先提供 value buffer 的容量與空間，讓 GPU 知道最多能寫回多少資料。

---

## 20. Mailbox call 流程

```text
1. 準備好 mailbox[] request
2. 等待 MAILBOX_STATUS 不再 FULL
3. 將 buffer address + channel 寫入 MAILBOX_WRITE
4. 等待 MAILBOX_STATUS 不再 EMPTY
5. 從 MAILBOX_READ 取得 response
6. 確認 response == request
7. 確認 message[1] == REQUEST_SUCCEED
8. 讀取 GPU 寫回的 values
```

### `response == request`

這不是在判斷 property operation 是否成功，而是在確認：

> 目前從 FIFO 讀到的 response 屬於同一個 message buffer 和 channel。

Mailbox FIFO 可能含有其他 message；若不相等，就必須繼續等待。

### `message[1] == REQUEST_SUCCEED`

這才是在確認：

> GPU firmware 已成功解析並處理整個 property message。

即使 `response == request`，`message[1]` 仍可能表示失敗。此時 `[5]`、`[9]`、`[10]` 可能是舊值、request placeholder 或無效資料，不能相信。

---

## 21. Mailbox 卡住時如何判斷

若成功輸出：

```text
Welcome to my shell!
```

但沒有 board revision，也沒有 shell prompt，代表 UART 已正常，問題很可能發生在 mailbox。

可能卡住的位置：

### 一直 FULL

```c
while (mmio_read(MAILBOX_STATUS) & MAILBOX_FULL) {
}
```

代表 CPU 尚不能寫入 request。

需要檢查：

- mailbox status address；
- `MAILBOX_FULL` mask；
- MMIO base address。

### 一直 EMPTY

```c
while (mmio_read(MAILBOX_STATUS) & MAILBOX_EMPTY) {
}
```

代表 CPU 已送出 request，但尚未收到 GPU response。

需要檢查：

- channel；
- buffer alignment；
- request address；
- message layout；
- property tags。

### 一直 `response != request`

代表讀到的 response 不屬於目前送出的 buffer/channel，或 request packing 有誤。

需要檢查：

```text
buffer 是否 16-byte aligned
address mask 是否為 0xfffffff0
channel 是否為 8
```

---

## 22. ARM memory 為什麼是 960 MiB

QEMU 可能回傳：

```text
ARM memory base: 0x00000000
ARM memory size: 0x3C000000
```

計算：

```text
1 GiB       = 0x40000000
ARM memory  = 0x3C000000
差額        = 0x04000000 = 64 MiB
```

少掉的 64 MiB 是 VideoCore/GPU firmware 保留的 RAM，不是 ROM。

Kernel 不應直接 hard-code `0x3C000000`，因為 ARM/GPU memory split 可能受：

- Raspberry Pi 型號；
- firmware configuration；
- GPU memory 設定；
- 執行環境；

影響。Mailbox 回傳的是 firmware 當下認定可供 ARM 使用的 memory configuration。

---

## 23. 常見除錯順序

### 完全沒有 UART 輸出

依序檢查：

1. QEMU 是否使用 `-serial null -serial stdio`；
2. GPIO14/15 是否設定 ALT5；
3. physical address 是否為 `0x3F...`；
4. `AUX_ENABLES` 是否啟用 mini UART；
5. `CNTL_REG` 最後是否設為 3；
6. `uart_send()` 是否等待 LSR bit 5；
7. MMIO pointer 是否為 `volatile`。

### 可以輸出，但不能輸入

檢查：

1. GPIO15 是否為 ALT5；
2. RX 是否 enable；
3. `uart_recv()` 是否等待 LSR bit 0；
4. QEMU stdin 是否連到 mini UART；
5. terminal 傳入的是 `\r`、`\n` 或 `\r\n`。

### 每行錯位或多出奇怪換行

檢查：

1. `uart_puts()` 是否已經轉換 `\n → \r\n`；
2. caller 是否又手動傳入 `\r\n`；
3. 是否實際送出 `\r\r\n`；
4. shell 是否把輸入的 `\r\n` 當成兩次 Enter。

### Mailbox 永遠卡住

依序定位：

1. 卡在 FULL；
2. 卡在 EMPTY；
3. 一直 response mismatch；
4. 收到相同 response，但 `message[1]` 表示失敗。

不要只說「mailbox 壞了」，要先定位是哪一個 polling condition 無法結束。

---

## 24. 最後應能完整說明的因果鏈

```text
Firmware 將 raw image 載到 0x80000
→ .text.boot 位於 image 第一段
→ _start 判斷目前 CPU core
→ Secondary cores 進入 WFE
→ Core 0 設定 16-byte aligned stack
→ 依 linker symbols 清除 BSS
→ 進入 kernel_main
→ GPIO14/15 切到 ALT5
→ 初始化 mini UART
→ UART 透過 polling 收發 byte
→ Mailbox 透過 channel 8 向 GPU firmware 查詢硬體資訊
→ Shell 收集輸入、處理 CRLF、比對 help/hello
```

只要能不看程式，用自己的話完整解釋這條流程，以及每一步如果省略會發生什麼問題，就代表已經真正理解 Lab 1。
````