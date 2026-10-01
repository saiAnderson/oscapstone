.section ".text.boot", "ax"

.global _start
.extern __stack_top
.extern __bss_start
.extern __bss_end
.extern kernel_main

_start:
  /*
   * TODO 1：
   * 讀取目前 CPU core 的 MPIDR_EL1，
   * 將結果放入一個通用暫存器。
   */
   mrs x0, mpidr_el1 

  /*
   * TODO 2：
   * 使用 bitmask，只保留 MPIDR_EL1 的 Aff0，
   * 也就是最低 8 bits，取得 core ID。
   */
  and x0, x0, #0xff

  /*
   * TODO 3：
   * 如果 core ID 等於 0，
   * 跳到 primary_core。
   */
   cbz x0, primary_core

secondary_core_hang:
  /*
   * TODO 4：
   * Core 1、2、3 在這裡等待。
   * 使用 wfe，醒來後再跳回 secondary_core_hang。
   */
  wfe
  b secondary_core_hang


primary_core:
  /* 步驟 1：設定 sp = __stack_top */
  ldr x12, =__stack_top
  mov sp, x12

  /* 步驟 2：載入 __bss_start 和 __bss_end */
  ldr x9, =__bss_start
  ldr x10, =__bss_end

clear_bss:
  /* 步驟 3：比較目前位置與結束位置 */
  cmp x9, x10
  b.hs clear_bss_done

  /* 步驟 4：一次清 16 bytes */
  stp xzr, xzr, [x9], #16

  /* 步驟 5：回到 clear_bss */
  b clear_bss

clear_bss_done:
  /* 步驟 6：呼叫 kernel_main */
  bl kernel_main

hang:
  wfe
  b hang