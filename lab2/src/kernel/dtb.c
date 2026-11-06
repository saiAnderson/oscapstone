#include "dtb.h"
#define FDT_BEGIN_NODE  0x00000001U
#define FDT_END_NODE    0x00000002U
#define FDT_PROP        0x00000003U
#define FDT_NOP         0x00000004U
#define FDT_END         0x00000009U

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

void dtb_traverse(const void *dtb){
    const uint8_t* base = (const uint8_t*) dtb;

    uint32_t magic = dtb_read_be32(base);

    if(magic != 0xD00DFEED){
        uart_puts("Invalid DTB magic!\r\n");
        return;
    }

    uint32_t total_size = dtb_read_be32(base + 4);
    uint32_t off_dt_struct = dtb_read_be32(base + 8);
    uint32_t off_dt_strings = dtb_read_be32(base + 12);
    uint32_t size_dt_struct = dtb_read_be32(base + 36);

    // record the end of the structure section in device tree
    // base is head of dtb, then point to structure address , finally add the size of sturcture
    const uint8_t *struct_end = base + off_dt_struct + size_dt_struct;

    uart_puts("DTB magic: ");
    uart_hex(magic);
    uart_puts("\r\n");

    uart_puts("DTB total size: ");
    uart_hex(total_size);
    uart_puts("\r\n");

    const uint8_t* cursor = base + off_dt_struct;

    // dtb structure section
    while (cursor < struct_end) {
        uint32_t token = dtb_read_be32(cursor);
        cursor += 4;

        if (token == FDT_BEGIN_NODE) {
            // [FDT_BEGIN_NODE (4 bytes)] [node name (unknown bytes)] [padding]...
            //                          ^
            //                       current
            uart_puts("FDT_BEGIN_NODE: ");
            unsigned int node_len=0;
            while(cursor[node_len] != '\0'){
                uart_send(cursor[node_len]);
                node_len++;
            }
            uart_puts("\r\n");
            // after we get the node name, next step is calculate the padding
            cursor += algin4(node_len + 1);
        } 
        else if (token == FDT_PROP) {
            // [FDT_PROP (4 bytes)][len (4 bytes)][nameoff(4 bytes)][property value (unknown bytes)][padding]
            //                     ^
            //                     current
            // ! property name is at string section (off_dt_strings)
            uart_puts("FDT_PROP: ");
            uint32_t property_len = dtb_read_be32(cursor);
            uint32_t property_nameoff = dtb_read_be32(cursor + 4);

            // after we get property data move the cursor
            cursor += 8;

            // First get property name
            // point to string section
            uint8_t *ptr_str_section = base + off_dt_strings + property_nameoff;
            uart_puts(ptr_str_section);
            uart_puts(" = ");

            // Second get property value
            uint32_t i = 0;
            while(i < property_len) {
                uart_send((char)cursor[i]);
                i++;
            }
            uart_puts("\r\n");

            // Third move cursor from begining of property value
            cursor += algin4(property_len);
        }
        else if (token == FDT_END_NODE) {
            uart_puts("FDT_END_NODE \r\n");
        }
        else if (token == FDT_NOP) {
            uart_puts("FDT_NOP \r\n");
        }
        else if (token == FDT_END) {
            uart_puts("FDT_END \r\n");
            break;
        }
        else {
            uart_puts("Unknown FDT Token: ");
            uart_hex(token);
            uart_puts("\r\n");
            return;
        }
    }
}