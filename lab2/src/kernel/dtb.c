#include "dtb.h"

static unsigned int algin4(unsigned int value){
    return (value+3U)&(~3U);
}

uint32_t dtb_read_be32(const uint8_t *ptr) {
    uint32_t data = 0;
    for(int i=0;i<4;i++) {
        data <<= 8;
        data |= ptr[i];
    }
    return data;
}

void dtb_parse(const void *dtb){
    const uint8_t* base = (const uint8_t*) dtb;

    uint32_t magic = dtb_read_be32(base);

    uart_puts("DTB address: ");
    uart_hex((uint32_t)(uintptr_t)dtb);
    uart_puts("\r\n");

    uart_puts("Actual DTB magic: ");
    uart_hex(magic);
    uart_puts("\r\n");

    if(magic != 0xD00DFEED){
        uart_puts("Invalid DTB magic!\r\n");
        return;
    }

    uint32_t total_size = dtb_read_be32(base + 4);

    uart_puts("DTB magic: ");
    uart_hex(magic);
    uart_puts("\r\n");

    uart_puts("DTB total size: ");
    uart_hex(total_size);
    uart_puts("\r\n");
    // const uint8_t *dtb_base = (const uint8_t *) dtb;
    // uart_puts("DTB magic: ");
    // uart_hex(dtb_read_be32(dtb_base));
    // uart_puts("\r\n");

    // uart_puts("DTB total size: ");
    // uart_hex(dtb_read_be32(dtb_base + 4));
    // uart_puts("\r\n");
    
    // uint32_t struct_offset = dtb_read_be32(dtb_base + 8);
    // const uint8_t *struct_pointer = dtb_base + struct_offset;

    // uint32_t token = dtb_read_be32(struct_pointer);

    // uart_puts("!!!First FDT token: ");
    // uart_hex(token);
    // uart_puts("\r\n");

    // //====================================
    // const uint8_t* node_name = struct_pointer + 4; // because First FDT token tage is 4 bytes 
    // unsigned int name_len = 0;
    
    // while(node_name[name_len] != '\0'){
    //     name_len++;
    // }

    // uart_puts("Node name length: ");
    // uart_hex(name_len);
    // uart_puts("\r\n");

    // //====================================
    // unsigned int used_bytes = 4 + name_len + 1;
    // unsigned int next_offset = (used_bytes + 3U) & ~3U; // previous token + previous name len + '\0'
    // uint8_t* next_token_ptr = struct_pointer + next_offset;
    // uint32_t next_token = dtb_read_be32(next_token_ptr);
    // uart_puts("!!!second FDT token: ");
    // uart_hex(next_token);
    // uart_puts("\r\n");

}