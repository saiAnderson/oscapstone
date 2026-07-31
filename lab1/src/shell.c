#include <stdint.h>
#include "uart.h"
#include "shell.h"

char buffer[128];
static unsigned int index;

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

void shell_run(void){
    uart_puts("# ");
    index = 0;
    int is_prev_r = 0;
    while(1){
        char c = uart_recv();
        if(c == '\n' && is_prev_r){
            is_prev_r = 0;
            continue;
        }
        if (c == '\r' || c == '\n') {
            is_prev_r = c=='\r';
            uart_puts("\n");
            buffer[index] = '\0';
            if(index == 0) {
                uart_puts("# ");
            }
            else {
                // string comepare 
                // if exit command 
                if(str_equal("help", buffer)){
                    uart_puts("help : print this help menu \nhello : Hello World!"); 
                }
                else if(str_equal("hello" ,buffer)){
                    uart_puts("Hello World!");
                }
                // not exit command
                else {
                    uart_puts("Unknown command: "); 
                    uart_puts(buffer); 
                }
                uart_puts("\n# ");
            }
            index = 0;
        }
        else {
            if(index*1ULL < sizeof(buffer)-1) {
                is_prev_r = 0;
                uart_send(c);
                buffer[index++] = c;
            }
        }
    }
}

