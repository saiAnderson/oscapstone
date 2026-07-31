#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include <stddef.h>

void kernel_main(void)
{
    uart_init();
    uart_puts("Welcome to my shell!\n");
    HardwareInfo info;
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
    
    shell_run();
}
