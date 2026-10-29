#include "dtb.h"

uint32_t dtb_read_be32(const uint8_t *ptr) {
    uint32_t data = 0;
    for(int i=0;i<4;i++) {
        data <<= 8;
        data |= ptr[i];
    }
    return data;
}