#include <stdint.h>
#include "mailbox.h"
#include "uart.h"

// position of MMIO register 
#define MMIO_BASE       0x3F000000
#define MAILBOX_BASE    (MMIO_BASE + 0xB880)
#define MAILBOX_READ (MAILBOX_BASE + 0x00)/* ARM 讀取 VideoCore 傳回的一筆 mailbox 完成通知 */
#define MAILBOX_STATUS (MAILBOX_BASE + 0x18)/* 查看 Mailbox 是否 full 或 empty，判斷能否寫入或讀取 */
#define MAILBOX_WRITE (MAILBOX_BASE + 0x20)/* ARM 寫入 message buffer address + channel，通知 VideoCore */

// flag not register (bit mask)
#define MAILBOX_EMPTY 0x40000000
#define MAILBOX_FULL  0x80000000

#define MAILBOX_CHANNEL_PROPERTY 8 /* ARM 使用 Property Interface 向 VideoCore 發出 request */
#define GET_BOARD_REVISION 0x00010002 /* 表示這個 tag 要取得 board revision */
#define GET_ARM_MEMORY 0x00010005

#define REQUEST_CODE 0x00000000 /* 整個 message buffer 是待處理的 request */
#define REQUEST_SUCCEED 0x80000000 /* 整個 message buffer 已成功處理 */
#define REQUEST_FAILED 0x80000001 /* VideoCore 解析 message buffer 時發生錯誤 */
#define TAG_REQUEST_CODE 0x00000000 /* 這個 tag 是 request；bit 31 尚未設為 response */
#define END_TAG 0x00000000 /* message 中的 tag sequence 結束 */

static void mmio_write(uintptr_t reg, uint32_t value){
    *(volatile uint32_t *) reg = value;
}

static uint32_t mmio_read(uintptr_t reg){
    return *(volatile uint32_t *) reg;
}

static volatile uint32_t mailbox[12] __attribute__((aligned(16)));

static int mailbox_call(volatile uint32_t *message){
    // 1: success | 0: fail

    uintptr_t mailbox_address = (uintptr_t) message;

    uint32_t request = ((uint32_t)mailbox_address & 0xFFFFFFF0u) | MAILBOX_CHANNEL_PROPERTY;

    // 1. 等待 mailbox 非 full
    while(mmio_read(MAILBOX_STATUS) & MAILBOX_FULL){
    }

    // 2. 寫入 request
    mmio_write(MAILBOX_WRITE, request);

    while(1) {
        // 3. 等待 mailbox 非 empty
        while(mmio_read(MAILBOX_STATUS) & MAILBOX_EMPTY){
        }

        // 4. 讀取 response
        uint32_t response = mmio_read(MAILBOX_READ);

        if(response == request){
            return message[1] == REQUEST_SUCCEED;
        }
    }
}

int get_hardware_info(HardwareInfo *info){
    if(info == 0) return 0;
    mailbox[0] = 12*4; /* 整個 message 的大小，單位 bytes */
    mailbox[1] = REQUEST_CODE; /* 整個 buffer 是 request */
    // tags begin
    // board revision
    mailbox[2] = GET_BOARD_REVISION; /* Get Board Revision 的 tag ID */
    mailbox[3] = 4; /* value buffer 的總容量是 4 bytes。 */
    mailbox[4] = TAG_REQUEST_CODE; /* 這個 tag 是 request */
    mailbox[5] = 0;/* 預留給 VideoCore 寫入答案 */

    // ARM memory base address and size 
    mailbox[6] = GET_ARM_MEMORY;  /* Tag identifier */
    mailbox[7] = 8; /* Value buffer 容量：8 bytes */
    mailbox[8] = TAG_REQUEST_CODE; /* Tag 是 request */
    mailbox[9] = 0;  /* 預留 ARM memory base */
    mailbox[10] = 0;  /* 預留 ARM memory size */

    // tags end
    mailbox[11] = END_TAG; /* End tag */

    if(mailbox_call(mailbox)) {
        info->board_revision  = mailbox[5];
        info->memory_base = mailbox[9];
        info->memory_size = mailbox[10];
        return 1;
    }
    return 0;
}

