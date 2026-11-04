#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include "cpio.h"
#include "dtb.h"
#include <stddef.h>

static unsigned int algin4(unsigned int value){
    return (value+3)|(~3);
}

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
    
    uint32_t struct_offset = dtb_read_be32(dtb_base + 8);
    const uint8_t *struct_pointer = dtb_base + struct_offset;

    uint32_t token = dtb_read_be32(struct_pointer);

    uart_puts("!!!First FDT token: ");
    uart_hex(token);
    uart_puts("\r\n");

    //====================================
    const uint8_t* node_name = struct_pointer + 4; // because First FDT token tage is 4 bytes 
    unsigned int name_len = 0;
    
    while(node_name[name_len] != '\0'){
        name_len++;
    }

    uart_puts("Node name length: ");
    uart_hex(name_len);
    uart_puts("\r\n");

    //====================================
    unsigned int next_offset = algin4(4 + name_len + 1); // previous token + previous name len + '\0'
    uint8_t* next_token_ptr = struct_pointer + next_offset;
    uint32_t next_token = dtb_read_be32(next_token_ptr);
    uart_puts("!!!second FDT token: ");
    uart_hex(next_token);
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
