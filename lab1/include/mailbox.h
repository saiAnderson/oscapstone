#ifndef MAILBOX_H
#define MAILBOX_H

typedef struct {
    uint32_t board_revision ;
    uint32_t memory_base;
    uint32_t memory_size;
} HardwareInfo;

int get_hardware_info(HardwareInfo *info);

#endif