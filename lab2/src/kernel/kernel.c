#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include "cpio.h"
#include "dtb.h"
#include <stddef.h>

// static unsigned int algin4(unsigned int value){
//     return (value+3)|(~3);
// }

void kernel_main(void *dtb)
{

    //===================
    uart_init();
    uart_puts("Welcome to my shell!\n");

    // ==================
    // dtb
    dtb_parse(dtb);

    // ================================================
    // cpio test 
    // cpio_list();
    uart_puts("==================================\r\n");
    uart_puts("TEST CPIO FIND\r\n");
    char *data;
    unsigned int size;

    if (cpio_find_file("hello.txt", &data, &size)) {
        for (unsigned int i = 0; i < size; i++) {
            uart_send(data[i]);
        }
    }
    else {
        uart_puts("file didn't exit\r\n");
    }
    uart_puts("\r\n");
    // ================================================

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
    
    shell_run(dtb);
}
