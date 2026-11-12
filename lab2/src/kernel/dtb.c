#include <stddef.h>
#include "dtb.h"
#include "uart.h"
#define FDT_BEGIN_NODE  0x00000001U
#define FDT_END_NODE    0x00000002U
#define FDT_PROP        0x00000003U
#define FDT_NOP         0x00000004U
#define FDT_END         0x00000009U

static unsigned int algin4(unsigned int value){
    return (value+3U)&(~3U);
}

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

uint32_t dtb_read_be32(const uint8_t *ptr) {
    uint32_t data = 0;
    for(int i=0;i<4;i++) {
        data <<= 8;
        data |= ptr[i];
    }
    return data;
}

void dtb_traverse(const void *dtb, dtb_node_callback_t callback,  void *context){
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

    // root node depth=0 
    int depth = -1;
    const uint8_t* cursor = base + off_dt_struct;

    // dtb structure section
    while (cursor < struct_end) {
        uint32_t token = dtb_read_be32(cursor);
        cursor += 4;

        if (token == FDT_BEGIN_NODE) {
            // [FDT_BEGIN_NODE (4 bytes)] [node name (unknown bytes)] [padding]...
            //                          ^
            //                       current (cursor)
            depth++;
            unsigned int node_len=0;
            const char *node_name = (const char *) cursor;
            while(cursor[node_len] != '\0'){
                node_len++;
            }

            const uint8_t *struct_start = base + off_dt_struct; 
            uint32_t node_offset = (uint32_t)((cursor - 4) - struct_start);

            callback(node_offset, node_name, depth, context);

            // after we get the node name, next step is calculate the padding
            cursor += algin4(node_len + 1);
        } 
        else if (token == FDT_PROP) {
            // [FDT_PROP (4 bytes)][len (4 bytes)][nameoff(4 bytes)][property value (unknown bytes)][padding]
            //                     ^
            //                      current (cursor)
            // ! property name is at string section (off_dt_strings)
            //uart_puts("FDT_PROP: ");
            uint32_t property_len = dtb_read_be32(cursor);
            uint32_t property_nameoff = dtb_read_be32(cursor + 4);

            // after we get property data move the cursor
            cursor += 8;

            // First get property name
            // point to string section
            uint8_t *ptr_str_section = base + off_dt_strings + property_nameoff;
            // uart_puts(ptr_str_section);
            // uart_puts(" = ");

            // Second get property value
            uint32_t i = 0;
            while(i < property_len) {
                // uart_send((char)cursor[i]);
                i++;
            }
            //uart_puts("\r\n");

            // Third move cursor from begining of property value
            cursor += algin4(property_len);
        }
        else if (token == FDT_END_NODE) {
            depth--;
        }
        else if (token == FDT_NOP) {
        }
        else if (token == FDT_END) {
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

const uint8_t *dtb_getprop(
    const void *dtb,
    uint32_t node_offset,
    const char *property_name,
    uint32_t *len
){
    *len = 0;
    const uint8_t* base = (const uint8_t*) dtb;

    uint32_t total_size = dtb_read_be32(base + 4);
    uint32_t off_dt_struct = dtb_read_be32(base + 8);
    uint32_t off_dt_strings = dtb_read_be32(base + 12);
    uint32_t size_dt_struct = dtb_read_be32(base + 36);
    
    // +4 for jump FDT_BEGIN_NODE
    const uint8_t *node_ptr = base + off_dt_struct + node_offset + 4;

    // calculate node_name
    unsigned int node_len=0;
    const char *node_name = (const char *) node_ptr;
    while(node_ptr[node_len] != '\0'){
        node_len++;
    }

    node_ptr += algin4(node_len + 1);

    // record the end of the structure section in device tree
    // base is head of dtb, then point to structure address , finally add the size of sturcture
    
    const uint8_t *struct_end = base + off_dt_struct + size_dt_struct;

    // dtb structure section
    while (node_ptr < struct_end) {
        uint32_t token = dtb_read_be32(node_ptr);
        node_ptr += 4;

        if (token == FDT_PROP) {
            // [FDT_PROP (4 bytes)][len (4 bytes)][nameoff(4 bytes)][property value (unknown bytes)][padding]
            //                     ^
            //                      current (cursor)
            // ! property name is at string section (off_dt_strings)
            //uart_puts("FDT_PROP: ");
            uint32_t property_len = dtb_read_be32(node_ptr);
            uint32_t property_nameoff = dtb_read_be32(node_ptr + 4);

            // after we get property data move the cursor
            node_ptr += 8;

            // First get property name
            // point to string section
            const char *ptr_str_section = (const char*)(base + off_dt_strings + property_nameoff);

            if(str_equal(property_name, ptr_str_section)) {
                *len = property_len;
                return node_ptr;
            }
            // Third move node_ptr from begining of property value
            node_ptr += algin4(property_len);
        }
        else if (token == FDT_NOP) {
        }
        else if (token == FDT_END || token == FDT_END_NODE || token == FDT_BEGIN_NODE) {
            break;
        }
        else {
            uart_puts("Unknown FDT Token: ");
            uart_hex(token);
            uart_puts("\r\n");
            break;
        }
    }
    return NULL;
}









