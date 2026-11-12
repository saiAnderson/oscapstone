#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include "cpio.h"
#include "dtb.h"
#include <stddef.h>

static int str_equal(char *s1, char *s2){
    while( *s1!='\0' && *s2!='\0'){
        if(*s1 != *s2){
            return 0;
        }
        s1++;
        s2++;
    }
    return (*s1=='\0'&&*s2=='\0');
}

static int print_node(
    uint32_t node_offset,
    const char *node_name,
    int depth,
    void *context
){
    if(depth == 1 && str_equal(node_name, "chosen")){
        uint32_t len = 0;

        const uint8_t *value = dtb_getprop(
            context,
            node_offset,
            "stdout-path",
            &len 
        );

        if (value != NULL) {
            uart_puts("Found stdout-path!\r\n");

            uart_puts("Property Length: ");
            uart_hex(len);
            uart_puts("\r\n");
        }
        else {
            uart_puts("stdout-path not found\r\n");
        }
    }
    return 0;
}

void kernel_main(void *dtb)
{

    //===================
    uart_init();
    uart_puts("Welcome to my shell!\n");

    // TEST
    uint32_t len = 0;

const uint8_t *value = dtb_getprop(
    dtb,
    0,              // Root Node Offset
    "compatible",
    &len
);

if (value != NULL) {
    uart_puts("Root compatible found! Length: ");
    uart_hex(len);
    uart_puts("\r\n");
}
else {
    uart_puts("Root compatible not found!\r\n");
}
    //=======

    // ==================
    // dtb
    dtb_traverse(dtb, print_node, dtb);

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
