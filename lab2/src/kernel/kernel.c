#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include "cpio.h"
#include <stddef.h>

void kernel_main(void)
{
    uart_init();
    uart_puts("Welcome to my shell!\n");
    cpio_list();
    HardwareInfo info;
    uart_puts("mailbox TEST\r\n");
    uart_puts("==================================\r\n");
    if(get_hardware_info(&info)){
        uart_puts("Board revision: ");
        uart_hex(info.board_revision);

        uart_puts("\r\nARM memory base: ");
        uart_hex(info.memory_base);

        uart_puts("\r\nARM memory size: ");
        uart_hex(info.memory_size);

        uart_puts("\r\n");
    }
    else {
        uart_puts("Mailbox request failed\r\n");
    }
    uart_puts("==================================\r\n");
    
    shell_run();
}
