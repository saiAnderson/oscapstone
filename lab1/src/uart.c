#include <stdint.h>
#include "uart.h"

/*
 * GPIO registers
 */                                 // ex FSEL9 means funtion select for GPIO9
#define GPFSEL1          0x3F200004 // GPFSEL1 => GP=GPIO, FSEL=function select, 1=1st funtion select register
#define GPPUD            0x3F200094 // GPPUD：選擇要套用的設定
#define GPPUDCLK0        0x3F200098 // 選擇哪些 GPIO 接受設定

/*
 * Mini UART registers
 */
#define AUX_ENABLES      0x3F215004
#define AUX_MU_IO_REG    0x3F215040
#define AUX_MU_IER_REG   0x3F215044
#define AUX_MU_IIR_REG   0x3F215048
#define AUX_MU_LCR_REG   0x3F21504C
#define AUX_MU_MCR_REG   0x3F215050
#define AUX_MU_LSR_REG   0x3F215054
#define AUX_MU_CNTL_REG  0x3F215060
#define AUX_MU_BAUD_REG  0x3F215068

static void mmio_write(uintptr_t reg, uint32_t value) {
    *(volatile uint32_t *)reg = value; 
}

static uint32_t mmio_read(uintptr_t reg){
    return *(volatile uint32_t *) reg;
}

static void delay(uint32_t count){
    while(count > 0){
        __asm__ volatile("nop");
        count--;
    }
}

void uart_init(void)
{
    /*
     * TODO 1:
     * 設定 GPIO14、GPIO15 function 為 ALT5
     */
    uint32_t value = mmio_read(GPFSEL1);
    
    /* 清除 GPIO14 的 bits 14:12 */
    value &= ~(0b111u << 12);

    /* 清除 GPIO15 的 bits 17:15 */
    value &= ~(0b111u << 15);

    /* GPIO14 設定為 ALT5，也就是 010 */
    value |= (0b010u << 12);

    /* GPIO15 設定為 ALT5，也就是 010 */
    value |= (0b010u << 15);

    mmio_write(GPFSEL1, value);

    /*
     * TODO 2:
     * 關閉 GPIO14、GPIO15 pull-up/down
     */

    /* 選擇：關閉 pull-up/down */
    mmio_write(GPPUD, 0);
    
    delay(150);  // 等待 GPPUD 控制訊號穩定

    mmio_write(GPPUDCLK0, ((1u << 14) | (1u << 15))); //GPIO14：接受 GPPUD 的 off 設定 GPIO15：接受 GPPUD 的 off 設定 其他 GPIO：不修改

    delay(150); // 維持套用訊號，讓 GPIO pad 記住設定

    mmio_write(GPPUD, 0);
    mmio_write(GPPUDCLK0, 0);

    /*
     * TODO 3:
     * 啟用 Mini UART
     */
    uint32_t aux = mmio_read(AUX_ENABLES);
    aux |= 1u;
    mmio_write(AUX_ENABLES, aux);

    /*
     * TODO 4:
     * 設定 UART registers
     */
    /* 設定期間先關閉 TX、RX */
    mmio_write(AUX_MU_CNTL_REG, 0);

    /* 不使用 interrupt */
    mmio_write(AUX_MU_IER_REG, 0);

    /* 8-bit data */
    mmio_write(AUX_MU_LCR_REG, 3);

    /* 不使用 RTS flow control */
    mmio_write(AUX_MU_MCR_REG, 0);

    /* 250 MHz system clock 下，約為 115200 baud */
    mmio_write(AUX_MU_BAUD_REG, 270);

    /* 清除 RX FIFO 與 TX FIFO */
    mmio_write(AUX_MU_IIR_REG, 6);

    /* 最後才啟用 RX、TX */
    mmio_write(AUX_MU_CNTL_REG, 3);
}

char uart_recv(void) 
{
    while((mmio_read(AUX_MU_LSR_REG) & (1u)) == 0) {
    }    
    return (char) (mmio_read(AUX_MU_IO_REG)&(0xFFu)); 
}

void uart_send(char c)
{
    // 等待 AUX_MU_LSR_REG 表示 TX 可以接受資料
    while ((mmio_read(AUX_MU_LSR_REG) & ((1u << 5))) == 0) {
    }
    
    // 寫入 AUX_MU_IO_REG
    mmio_write(AUX_MU_IO_REG, (uint32_t)(uint8_t)c); 
}

const char hex_digits[] = "0123456789ABCDEF";
void uart_hex(uint32_t value) {
    uart_puts("0x");
    for(int i=0;i<8;i++){
        uint32_t tmp = (value & 0xF0000000)>> 28;
        uart_send(hex_digits[tmp]);
        value <<= 4;
    }
}


void uart_puts(const char *str)
{
    while (*str != '\0') {
        if (*str == '\n') {
            uart_send('\r');
        }

        uart_send(*str);
        str++;
    }
}