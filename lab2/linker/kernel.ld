ENTRY(_start)

SECTIONS
{
  /*
  * Kernel 預期載入位置。
  */
  . = 0x80000;
  __kernel_start = .;


  /*
  * Boot code 放在 image 最前面，
  * 其他一般程式碼接在後面。
  */
  . = ALIGN(16);
  .text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
  }

  /*
  * 唯讀資料，例如字串與 const。
  */
  . = ALIGN(16);
  .rodata : { 
    *(.rodata .rodata.*) 
  }

  /*
  * 有非零初始值的可寫全域資料。
  */
  . = ALIGN(16);
  .data : { 
    *(.data .data.*) 
  }
  
  /*
  * 未初始化或零初始化的全域資料。
  * 只保留記憶體空間，不把大量零放進 image。
  */
  . = ALIGN(16);
  .bss(NOLOAD) : {
    __bss_start = .;

    *(.bss .bss.*)
    *(COMMON)

    . = ALIGN(16);
    __bss_end = .;
  }


  /*
  * 預留單核心 16 KiB stack。
  */
  . = ALIGN(16);
  .stack (NOLOAD) : {
    __stack_bottom = .;
    . += 16K;
    __stack_top = .;
  }

  __kernel_end = .;
}
