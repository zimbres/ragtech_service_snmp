#include "ups_snmp.h"
#include "ups.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
static bool connected;
static int mode;
void ups_get_snapshot(ups_snapshot_t *s) {
    memset(s,0,sizeof(*s));
    s->connected=connected;
    strcpy(s->model,"EASY 700 TI");
    s->metrics=(ups_metrics_t){.voltage_in=127,.voltage_out=120,.current_out=2.5,
        .power_out=300,.power_in=317.5,.frequency=59.8,.battery_voltage=12.3,
        .battery_state=75,.battery_runtime=12.4,.power_out_percent=42,
        .temperature=30,.energy_in=1.5,.energy_out=1.2};
    if(mode==4) {
        s->metrics.battery_voltage=13.60099983215332f;
        s->metrics.frequency=59.7076416015625f;
        s->metrics.current_out=2.51509428024292f;
        s->metrics.voltage_out=109.88999938964844f;
        s->metrics.power_out=s->metrics.current_out*s->metrics.voltage_out;
        s->metrics.power_out_percent=32;
    }
    if(mode==5)s->metrics.frequency=NAN;
    s->status.op_battery=mode!=0 && mode!=4 && mode!=5;
    s->status.lo_battery=mode==2;
    s->status.fail_end_battery=mode==3;
    s->seconds_on_battery=mode ? 123 : 0;
    s->raw[0]=0xaa;s->raw[1]=0x80;s->raw_len=2;
}
int main(int argc,char **argv) {
    if(argc!=3 && argc!=4)return 1;
    config_t c;config_get_defaults(&c);
    connected=atoi(argv[1]);mode=atoi(argv[2]);
    if(argc==4) {
        c.snmp_port=atoi(argv[3]);strcpy(c.snmp_bind_address,"127.0.0.1");
        if(snmp_init(&c)<0)return 3;
        puts("ready");fflush(stdout);
        for(;;)snmp_poll(100);
    }
    char line[18000];
    while(fgets(line,sizeof(line),stdin)) {
        uint8_t request[9000],response[8192];size_t n=0;
        for(size_t i=0;line[i] && line[i]!='\n';i+=2) {
            unsigned byte;
            if(sscanf(line+i,"%2x",&byte)!=1)return 2;
            request[n++]=(uint8_t)byte;
        }
        size_t size=snmp_response(&c,request,n,response,sizeof(response));
        for(size_t i=0;i<size;i++)printf("%02x",response[i]);
        putchar('\n');fflush(stdout);
    }
    return 0;
}
