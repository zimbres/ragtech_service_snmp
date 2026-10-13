#include "platform.h"
#include "family10.h"
#include <stdio.h>
#include <string.h>
static int send_bytes(void *context,const uint8_t *bytes,size_t length) {
    return platform_serial_write(*(platform_serial_t *)context,bytes,length);
}
static int receive_bytes(void *context,uint8_t *bytes,size_t capacity,int timeout_ms) {
    return platform_serial_read(*(platform_serial_t *)context,bytes,capacity,timeout_ms);
}
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    config_t cfg={.baud_rate=2560,.data_bits=8,.parity='N',.stop_bits=1,.read_timeout_ms=1000};
    if(strlen(argv[1])>=sizeof(cfg.serial_port))return 1;
    strcpy(cfg.serial_port,argv[1]);
    platform_serial_t serial=platform_serial_open(&cfg);
    if(!platform_serial_valid(serial)) { fprintf(stderr,"%s\n",platform_error());return 2; }
    puts("ready");fflush(stdout);
    const uint16_t addresses[]={0xF3,0x202,0x80};const uint8_t counts[]={1,2,30};
    for(size_t i=0;i<3;i++) {
        uint8_t raw[31];size_t length;
        if(family10_read_registers(&serial,send_bytes,receive_bytes,addresses[i],counts[i],raw,&length,1000)<0) {
            platform_serial_close(serial);return 3;
        }
        for(size_t j=0;j<length;j++)printf("%02X",raw[j]);
        putchar('\n');fflush(stdout);
    }
    platform_serial_close(serial);return 0;
}
