#ifndef UART_H
#define UART_H
#include<stdint.h>

void uart_init(void);
void uart_send(char c);
void uart_puts(const char *str);
char uart_recv(void);
void uart_hex(uint32_t value);

#endif