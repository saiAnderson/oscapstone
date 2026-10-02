#include "uart.h"
#include "cpio.h"
#include <stddef.h>
#define CPIO_ADD 0x08000000

// void cpio_test(void) {
//     struct cpio_newc_header *header = (struct cpio_newc_header *) 0x08000000;
//     for(int i=0;i<6;i++){
//         uart_send(header->c_magic[i]);
//     }
//     uart_puts("\r\n");
// }

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

unsigned int hex_to_uint(const char *s, int len){
    unsigned int sum = 0;
    //16^2 16^1 16^0
    for(int i=0;i<len;i++){
        sum <<= 4;
        if ('0' <= s[i] && s[i] <= '9') {
            sum += s[i] - '0';
        }
        else if ('A' <= s[i] && s[i] <= 'F') {
            sum += s[i] - 'A' + 10;
        }
        else if ('a' <= s[i] && s[i] <= 'f') {
            sum += s[i] - 'a' + 10;
        }
    }
    return sum;
}

unsigned long align4(unsigned long n) {
    return (n+3) & (~3);
}


// cpio_file := ALGN(4) + cpio_newc_header + filename + "\0" + ALGN(4) + data
void cpio_list(void) {
    // cpio message
    uart_puts("CPIO TEST\r\nCPIO list file\r\n");
    uart_puts("==================================\r\n");
    struct cpio_newc_header *header = (struct cpio_newc_header *) CPIO_ADD;

    while(1) {
        // get filename info (filename, filename size)
        char *c_filename = (char *)header + sizeof(struct cpio_newc_header);
        char *c_namesize = header->c_namesize;
        unsigned int namesize = hex_to_uint(c_namesize, 8);

        if(str_equal(c_filename, "TRAILER!!!")) {
            uart_puts("TRAILER!!! STOP!!!\r\n");
            break;
        }

        // don't send '\0'
        for(int i=0;i<namesize-1;i++){
            uart_send(*(c_filename + i));
        }
        uart_puts("\r\n");

        // get file data info
        char *c_filesize = header->c_filesize;
        unsigned int filesize = hex_to_uint(c_filesize, 8);

        // struct header + filename size
        // below both are same
        // char *fileDataPtr = (char *)align4((char *)header + sizeof(struct cpio_newc_header) + namesize);
        char *fileDataPtr = (char *)align4((unsigned long)header + sizeof(struct cpio_newc_header) + namesize);
        for(int i=0;i<filesize;i++){
            uart_send(*(fileDataPtr+i));
        }
        uart_puts("\r\n");

        // change header address
        header = (struct cpio_newc_header *) align4((unsigned long)fileDataPtr + filesize);
    }
    uart_puts("==================================\r\n");
}
