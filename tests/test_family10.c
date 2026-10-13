#include "family10.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Check failed at %d: %s\n",__LINE__,#x);exit(1); } } while (0)
typedef struct {
    uint8_t input[8];size_t length,offset,fragment;
    uint8_t sent[6];bool fail_send;
} fake_serial;
static int send_bytes(void *ctx,const uint8_t *data,size_t length) {
    fake_serial *s=ctx;
    CHECK(length==6);memcpy(s->sent,data,length);
    return s->fail_send ? -1 : 0;
}
static int receive_bytes(void *ctx,uint8_t *data,size_t capacity,int timeout_ms) {
    fake_serial *s=ctx;CHECK(timeout_ms>0);
    size_t n=s->length-s->offset;
    if(n>s->fragment)n=s->fragment;
    if(n>capacity)n=capacity;
    memcpy(data,s->input+s->offset,n);s->offset+=n;return (int)n;
}
static int read_fake(fake_serial *s,uint16_t address,uint8_t count,uint8_t *raw,size_t *length) {
    return family10_read_registers(s,send_bytes,receive_bytes,address,count,raw,length,100);
}
int main(void) {
    uint8_t command[6];
    const uint8_t poll[]={0xAA,4,0,0x80,0x1E,0x9E};
    const uint8_t calib[]={0xAA,4,0,0xF3,1,0xF4};
    const uint8_t osc[]={0xAA,4,2,2,2,6};
    family10_read_command(0x80,30,command);CHECK(!memcmp(command,poll,6));
    family10_read_command(0xF3,1,command);CHECK(!memcmp(command,calib,6));
    family10_read_command(0x202,2,command);CHECK(!memcmp(command,osc,6));
    uint8_t raw[31];size_t length;
    fake_serial s={.input={0xAA,42},.length=2,.fragment=1};
    CHECK(read_fake(&s,0xF3,1,raw,&length)==0 && length==2 && raw[1]==42);
    CHECK(!memcmp(s.sent,calib,6));
    s=(fake_serial){.input={0xAA,253},.length=2,.fragment=1};
    CHECK(read_fake(&s,0xF3,1,raw,&length)==0 && raw[1]==253);
    s=(fake_serial){.input={0xAA,30,40},.length=3,.fragment=1};
    CHECK(read_fake(&s,0x202,2,raw,&length)==0 && raw[1]==30 && raw[2]==40);
    CHECK(!memcmp(s.sent,osc,6));
    s=(fake_serial){.input={0xFD},.length=1,.fragment=1};
    CHECK(read_fake(&s,0xF3,1,raw,&length)==-3 && length==1);
    s=(fake_serial){.input={0xAA},.length=1,.fragment=1};
    CHECK(read_fake(&s,0xF3,1,raw,&length)==-2);
    s=(fake_serial){.input={0xAA,3,0,0xF3,1,42,0},.length=7,.fragment=1};
    CHECK(read_fake(&s,0xF3,1,raw,&length)==-3);
    s=(fake_serial){.fragment=1};CHECK(read_fake(&s,0xF3,1,raw,&length)==-2);
    s=(fake_serial){.fragment=1,.fail_send=true};CHECK(read_fake(&s,0xF3,1,raw,&length)==-1);
    CHECK(family10_model(2)->current_factor==4.3f);
    /* Vendor samples: raw 30 and 31, IOUTCALIB 53. */
    float current_factor=family10_model(2)->current_factor;
    CHECK(fabsf(30.0f*current_factor/53.0f-2.433962345123291f)<0.000001f);
    CHECK(fabsf(31.0f*current_factor/53.0f-2.51509428024292f)<0.000001f);
    CHECK(fabsf(31.0f*current_factor/53.0f*109.89f-276.3837f)<0.0001f);
    CHECK(family10_model(19)!=NULL && family10_model(255)==NULL);
    CHECK(fabsf(family10_frequency(46,30,40,true)-59.70764f)<0.0001f);
    CHECK(isnan(family10_frequency(46,30,30,true)));
    CHECK(isnan(family10_frequency(46,30,40,false)));
    /* Output is compared independently with XML defaults and overrides by Python. */
    for(unsigned i=0;i<20;i++) {
        const family10_model_t *m=family10_model((uint8_t)i);
        CHECK(m!=NULL);
        printf("%u %.4f %.4f %.4f\n",i,m->current_factor,m->output_voltage_factor,m->battery_voltage_factor);
    }
    return 0;
}
