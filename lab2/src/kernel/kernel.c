#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include "cpio.h"
#include <stddef.h>

void kernel_main(void *dtb)
{

    //===================
    uart_init();
    uart_puts("Welcome to my shell!\n");

    // ==================
    // dtb
    const uint8_t *dtb_base = (const uint8_t *) dtb;
    uart_puts("DTB magic: ");
    uart_hex(dtb_read_be32(dtb_base));
    uart_puts("\r\n");

    uart_puts("DTB total size: ");
    uart_hex(dtb_read_be32(dtb_base + 4));
    uart_puts("\r\n");
    
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
    
    shell_run();
}
