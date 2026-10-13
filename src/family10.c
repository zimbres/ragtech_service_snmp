#define _POSIX_C_SOURCE 200809L
#include "family10.h"
#include <math.h>
#include "platform.h"

/* Family 10 defaults plus the selected model's explicit variable overrides.
 * The vendor config.so readDevice path applies default vars then model vars;
 * from="0" must not replace model 2's current formula with model 0's override. */
static const family10_model_t models[] = {
    {0, "EASY 600 TI", 3.2000f, 0.5550f, 0.0670f},
    {1, "EASY 600 M2", 1.7000f, 1.0300f, 0.0670f},
    {2, "EASY 700 TI", 4.3000f, 0.5550f, 0.0670f},
    {3, "EASY 700 M2", 2.3000f, 1.0300f, 0.0670f},
    {4, "EASY 900 TI", 4.9400f, 0.5550f, 0.0670f},
    {5, "EASY 900 M2", 2.6400f, 1.0300f, 0.0670f},
    {6, "EASY 1200 TI", 6.3700f, 0.5550f, 0.0670f},
    {7, "EASY 1200 M2", 3.4100f, 1.0300f, 0.0670f},
    {8, "EASY 1300 TI", 9.5600f, 0.5550f, 0.1340f},
    {9, "EASY 1300 M2", 5.1100f, 1.0300f, 0.1340f},
    {10, "EASY 1400 TI", 11.1500f, 0.5550f, 0.1340f},
    {11, "EASY 1400 M2", 5.9600f, 1.0300f, 0.1340f},
    {12, "EASY 1600 TI", 12.8000f, 0.5550f, 0.1340f},
    {13, "EASY 1600 M2", 6.8000f, 1.0300f, 0.1340f},
    {14, "EASY 1800 TI", 14.4000f, 0.5550f, 0.1340f},
    {15, "EASY 1800 M2", 7.6500f, 1.0300f, 0.1340f},
    {16, "EASY 2000 TI", 15.9300f, 0.5550f, 0.1340f},
    {17, "EASY 2000 M2", 8.5200f, 1.0300f, 0.1340f},
    {18, "EASY 2200 TI", 17.5200f, 0.5550f, 0.1340f},
    {19, "EASY 2200 M2", 9.3700f, 1.0300f, 0.1340f},
};
const family10_model_t *family10_model(uint8_t id) {
    for (size_t i=0; i<sizeof(models)/sizeof(models[0]); i++)
        if (models[i].id==id) return &models[i];
    return NULL;
}
float family10_frequency(uint8_t raw, uint8_t osc53, uint8_t osc57, bool valid) {
    if (!valid || osc53==osc57) return NAN;
    return (53.0f + 4.0f * ((float)raw - osc53) / ((float)osc57 - osc53)) * 0.9806f + 1.46f;
}
void family10_read_command(uint16_t address, uint8_t count, uint8_t command[6]) {
    command[0]=0xAA; command[1]=0x04;
    command[2]=(uint8_t)(address >> 8); command[3]=(uint8_t)address;
    command[4]=count;
    command[5]=(uint8_t)(command[2]+command[3]+command[4]);
}
static long long milliseconds(void) {
    return (long long)platform_monotonic_ms();
}
int family10_read_registers(void *context, family10_send_fn send,
    family10_receive_fn receive, uint16_t address, uint8_t count,
    uint8_t *raw, size_t *raw_length, int timeout_ms) {
    *raw_length=0;
    if (!count || count>30 || timeout_ms<=0) return -3;
    uint8_t command[6]; family10_read_command(address,count,command);
    if (send(context,command,sizeof(command))<0) return -1;
    long long deadline=milliseconds()+timeout_ms;
    size_t expected=(size_t)count+1;
    while (*raw_length<expected) {
        long long remaining=deadline-milliseconds();
        if (remaining<=0) return -2;
        int n=receive(context,raw+*raw_length,expected-*raw_length,(int)remaining);
        if (n<0) return -1;
        if (!n) return -2;
        if ((size_t)n>expected-*raw_length) return -3;
        *raw_length+=(size_t)n;
        if (raw[0]!=0xAA) return -3;
    }
    /* Reject extra bytes rather than mistaking an address-bearing packet for
     * the compact response used by this device. Allow inter-byte delivery. */
    uint8_t extra;
    int n=receive(context,&extra,1,20);
    return n<0 ? -1 : n>0 ? -3 : 0;
}
