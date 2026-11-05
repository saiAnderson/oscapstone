#ifndef DTB_H 
#define DTB_H

#include <stdint.h>

uint32_t dtb_read_be32(const uint8_t *ptr);
void dtb_parse(const void *dtb);

#endif