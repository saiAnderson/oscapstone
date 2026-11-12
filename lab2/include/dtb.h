#ifndef DTB_H 
#define DTB_H

#include <stdint.h>

uint32_t dtb_read_be32(const uint8_t *ptr);

typedef int (*dtb_node_callback_t) (
    uint32_t node_offset,
    const char *node_name,
    int depth,
    void *context
);

void dtb_traverse(
    const void *dtb, 
    dtb_node_callback_t callback,
    void *context
);

const uint8_t *dtb_getprop(
    const void *dtb,
    uint32_t node_offset,
    const char *property_name,
    uint32_t *len
);

#endif