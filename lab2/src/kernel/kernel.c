#include "uart.h"
#include "shell.h"
#include "mailbox.h"
#include "cpio.h"
#include "dtb.h"
#include <stddef.h>

struct initramfs_info {
    const void *dtb;
    uint64_t start;
    uint64_t end;
    int found;
};

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

static int find_initramfs(
    uint32_t node_offset,
    const char *node_name,
    int depth,
    void *context
){
    struct initramfs_info *info = (struct initramfs_info *)context;

    if(depth == 1 && str_equal(node_name, "chosen")){
        uint32_t start_len = 0, end_len;

        const uint8_t *start_value = dtb_getprop(
            info->dtb,
            node_offset,
            "linux,initrd-start",
            &start_len 
        );

        const uint8_t *end_value = dtb_getprop(
            info->dtb,
            node_offset,
            "linux,initrd-end",
            &end_len
        );

        if (start_value != NULL && end_value != NULL) {
            // get initrd_start address from former 32 bits and end 32 bits
            uint64_t initrd_start_addr_front32 = 0;
            uint64_t initrd_start_addr_end32 = 0;
            uint64_t initrd_start_addr = 0;
            if(start_len == 4){
                initrd_start_addr = dtb_read_be32(start_value);
                uart_hex(initrd_start_addr);
                uart_puts("\r\n");
            }
            else if(start_len == 8){
                initrd_start_addr_front32 = (uint64_t)dtb_read_be32(start_value);
                initrd_start_addr_end32 = (uint64_t)dtb_read_be32(start_value + 4);
                initrd_start_addr = (initrd_start_addr_front32<<32) | initrd_start_addr_end32;
            }
            else {
                uart_puts("Unsupported initrd address length\r\n");
                return 0;
            }
            
            // get initrd_end address from former 32 bits and end 32 bits
            uint64_t initrd_end_addr_front32 = 0;
            uint64_t initrd_end_addr_end32 = 0;
            uint64_t initrd_end_addr = 0;
            if(end_len == 4){
                initrd_end_addr = dtb_read_be32(end_value);
                uart_hex(initrd_end_addr);
                uart_puts("\r\n");
            }
            else if(end_len == 8){
                initrd_end_addr_front32 = (uint64_t)dtb_read_be32(end_value);
                initrd_end_addr_end32 = (uint64_t)dtb_read_be32(end_value + 4);
                initrd_end_addr = (initrd_end_addr_front32<<32) | initrd_end_addr_end32;
            }
            else {
                uart_puts("Unsupported initrd address length\r\n");
                return 0;
            }

            info->start = initrd_start_addr;
            info->end = initrd_end_addr;
            info->found = 1;
        }
        else {
            uart_puts("linux,initrd-start not found\r\n");
        }
    }
    return 0;
}

void kernel_main(void *dtb)
{

    //===================
    uart_init();
    uart_puts("Welcome to my shell!\n");

    // ==================
    // dtb
    struct initramfs_info ramdisk_info = {
        .dtb = dtb,
        .start = 0,
        .end = 0,
        .found = 0,
    };
    dtb_traverse(dtb, find_initramfs, &ramdisk_info);

    if (ramdisk_info.found) {
        uart_puts("Initramfs Start: ");
        uart_hex((uint32_t)ramdisk_info.start);
        uart_puts("\r\n");
        
        uart_puts("Initramfs End: ");
        uart_hex((uint32_t)ramdisk_info.end);
        uart_puts("\r\n");
    }
    else {
        uart_puts("Failed to find Initramfs Address\r\n");
    }

    // ================================================
    // cpio test 
    // cpio_list();
    uart_puts("==================================\r\n");
    uart_puts("TEST CPIO FIND\r\n");
    char *data;
    unsigned int size;

    if(!ramdisk_info.found || ramdisk_info.end <= ramdisk_info.start) {
        uart_puts("Initramfs unavailable\r\n");
    }
    else if(cpio_find_file(
        (void *)(uintptr_t)ramdisk_info.start,
        "hello.txt",
        &data,
        &size
    )){
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
