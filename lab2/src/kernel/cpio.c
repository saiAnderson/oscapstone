#include "uart.h"
#include "cpio.h"
#include <stddef.h>

void cpio_test(void) {
    struct cpio_newc_header *header = (struct cpio_newc_header *) 0x08000000;
    for(int i=0;i<6;i++){
        uart_send(header->c_magic[i]);
    }
    uart_puts("\r\n");
}


