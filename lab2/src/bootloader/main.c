#include "uart.h"
#include <stddef.h>
#define KERNEL_ADDRESS 0x80000

// static void jump_to_kernel(){
//     __asm__ volatile(
//         "ldr x0, =0x80000\n"
//         "br x0\n"
//     );
// }

// 將 KERNEL_ADDRESS 視為 kernel 的 function entry。
// 呼叫 function pointer 後，CPU 會跳到該位址開始執行 kernel。
static void jump_to_kernel(void){
    void (*kernel_entry) (void) = (void(*)(void))KERNEL_ADDRESS;
    kernel_entry();
}


void bootloader_main(void){ 
    uart_init();

    uart_puts("Waiting for kernel8.img......\n");

    // char 1 byte 
    uint32_t kernel_size = 0; // little endient
    for(int i=0;i<4;i++){
        uint8_t c = (uint8_t)uart_recv();
        kernel_size |= ((uint32_t)c << (8*i));
    }
    uart_puts("success receive kernel8.img size: ");
    uart_hex(kernel_size);
    uart_puts("\n");
    
    // get kernel8.img 
    uint8_t *kernel = (uint8_t*) KERNEL_ADDRESS;
    for(uint32_t i=0;i<kernel_size;i++){

        // 位址實際增加量 i × sizeof(*kernel)
        //*(kernel + i) = (uint8_t)uart_recv();
        kernel[i] = (uint8_t)uart_recv();
    }
    uart_puts("Success load kernel8.img into memory\n\n");

    // change PC 
    jump_to_kernel();
}