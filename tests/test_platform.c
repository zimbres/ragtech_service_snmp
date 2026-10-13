#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Check failed at %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static platform_mutex_t lock=PLATFORM_MUTEX_INITIALIZER;
static platform_flag_t started=0;
static unsigned count;
static void *worker(void *unused) {
    (void)unused;platform_flag_set(&started,true);
    for(unsigned i=0;i<10000;i++) {
        platform_mutex_lock(&lock);count++;platform_mutex_unlock(&lock);
    }
    return NULL;
}
int main(void) {
    platform_thread_t first,second;
    CHECK(platform_thread_start(&first,worker,NULL)==0);
    CHECK(platform_thread_start(&second,worker,NULL)==0);
    platform_thread_join(first);platform_thread_join(second);
    CHECK(count==20000 && platform_flag_get(&started));
    platform_flag_set(&started,false);CHECK(!platform_flag_get(&started));
    uint64_t before=platform_monotonic_ms();platform_sleep_ms(32);
    CHECK(platform_monotonic_ms()>before);
    char timestamp[40];platform_timestamp(timestamp,sizeof(timestamp));CHECK(strlen(timestamp)==23);
    CHECK(platform_ipv4_valid("127.0.0.1"));CHECK(platform_ipv4_valid("0.0.0.0"));
    CHECK(!platform_ipv4_valid("256.0.0.1"));CHECK(!platform_ipv4_valid("::1"));CHECK(!platform_ipv4_valid("invalid"));
    puts("Native threads, synchronization, clocks and IPv4 validation pass.");return 0;
}
