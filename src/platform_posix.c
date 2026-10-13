#define _GNU_SOURCE
#include "platform.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Local definition avoids Linux termios2/termios.h header collisions. */
struct termios2 {
    tcflag_t c_iflag,c_oflag,c_cflag,c_lflag;
    cc_t c_line,c_cc[19];
    speed_t c_ispeed,c_ospeed;
};
#ifndef TCGETS2
#define TCGETS2 _IOR('T',0x2A,struct termios2)
#endif
#ifndef TCSETS2
#define TCSETS2 _IOW('T',0x2B,struct termios2)
#endif
#ifndef BOTHER
#define BOTHER 0010000
#endif
void platform_mutex_lock(platform_mutex_t *mutex) { pthread_mutex_lock(mutex); }
void platform_mutex_unlock(platform_mutex_t *mutex) { pthread_mutex_unlock(mutex); }
bool platform_flag_get(platform_flag_t *flag) { return atomic_load(flag); }
void platform_flag_set(platform_flag_t *flag,bool value) { atomic_store(flag,value); }
int platform_thread_start(platform_thread_t *thread,void *(*entry)(void *),void *arg) {
    int result=pthread_create(thread,NULL,entry,arg);
    if(result)errno=result;
    return result ? -1 : 0;
}
void platform_thread_join(platform_thread_t thread) { pthread_join(thread,NULL); }
uint64_t platform_monotonic_ms(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000;
}
void platform_sleep_ms(unsigned ms) {
    struct timespec delay={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};
    while(nanosleep(&delay,&delay)<0 && errno==EINTR) {}
}
void platform_timestamp(char *text,size_t capacity) {
    struct timespec now;struct tm local;
    clock_gettime(CLOCK_REALTIME,&now);localtime_r(&now.tv_sec,&local);
    char date[32];strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",&local);
    snprintf(text,capacity,"%s.%03u",date,(unsigned)(now.tv_nsec/1000000));
}
const char *platform_error(void) { return strerror(errno); }
int platform_make_directory(const char *path) {
    if(mkdir(path,0755)==0)return 0;
    if(errno==EEXIST) {
        struct stat info;
        if(stat(path,&info)==0 && S_ISDIR(info.st_mode))return 0;
    }
    return -1;
}
static int serial_configure(int fd,const config_t *cfg) {
    struct termios tty;
    if(tcgetattr(fd,&tty)<0)return -1;
    cfmakeraw(&tty);
    tty.c_cflag &= ~(CSIZE|PARENB|PARODD|CSTOPB|CRTSCTS);
    switch(cfg->data_bits) {
        case 5:tty.c_cflag|=CS5;break;
        case 6:tty.c_cflag|=CS6;break;
        case 7:tty.c_cflag|=CS7;break;
        default:tty.c_cflag|=CS8;break;
    }
    if(cfg->parity=='E')tty.c_cflag|=PARENB;
    if(cfg->parity=='O')tty.c_cflag|=PARENB|PARODD;
    if(cfg->stop_bits==2)tty.c_cflag|=CSTOPB;
    tty.c_cflag|=CLOCAL|CREAD;
    tty.c_cc[VMIN]=0;tty.c_cc[VTIME]=0; /* poll controls each read timeout */
    speed_t speed;
    switch(cfg->baud_rate) {
        case 2400:speed=B2400;break;
        case 9600:speed=B9600;break;
        case 19200:speed=B19200;break;
        case 38400:speed=B38400;break;
        case 57600:speed=B57600;break;
        case 115200:speed=B115200;break;
        default:speed=B38400;break; /* termios2 sets custom rates below */
    }
    if(cfsetispeed(&tty,speed)<0 || cfsetospeed(&tty,speed)<0 || tcsetattr(fd,TCSANOW,&tty)<0)return -1;
    struct termios2 tty2;
    if(ioctl(fd,TCGETS2,&tty2)<0)return -1;
    tty2.c_cflag=(tty2.c_cflag & ~CBAUD)|BOTHER;
    tty2.c_ispeed=(speed_t)cfg->baud_rate;tty2.c_ospeed=(speed_t)cfg->baud_rate;
    if(ioctl(fd,TCSETS2,&tty2)<0)return -1;
    platform_serial_flush(fd);
    return 0;
}
platform_serial_t platform_serial_open(const config_t *cfg) {
    int fd=open(cfg->serial_port,O_RDWR|O_NOCTTY|O_SYNC|O_NONBLOCK);
    if(fd<0)return PLATFORM_SERIAL_INVALID;
    if(serial_configure(fd,cfg)<0) { int error=errno;close(fd);errno=error;return PLATFORM_SERIAL_INVALID; }
    return fd;
}
bool platform_serial_valid(platform_serial_t serial) { return serial>=0; }
void platform_serial_close(platform_serial_t serial) { if(serial>=0)close(serial); }
int platform_serial_write(platform_serial_t serial,const uint8_t *bytes,size_t length) {
    /* Commands are six bytes; reject partial writes rather than silently accepting them. */
    ssize_t n;
    do { n=write(serial,bytes,length); } while(n<0 && errno==EINTR);
    if(n<0)return -1;
    if((size_t)n!=length) { errno=EIO;return -1; }
    return tcdrain(serial)<0 ? -1 : 0;
}
int platform_serial_read(platform_serial_t serial,uint8_t *bytes,size_t capacity,int timeout_ms) {
    struct pollfd fd={.fd=serial,.events=POLLIN};
    int result=poll(&fd,1,timeout_ms);
    if(result<0)return errno==EINTR ? 0 : -1;
    if(result==0)return 0;
    if(fd.revents&(POLLERR|POLLHUP|POLLNVAL)) { errno=EIO;return -1; }
    if(!(fd.revents&POLLIN))return 0;
    ssize_t n=read(serial,bytes,capacity);
    if(n<0 && (errno==EAGAIN || errno==EINTR))return 0;
    return (int)n;
}
void platform_serial_flush(platform_serial_t serial) { tcflush(serial,TCIFLUSH); }
bool platform_ipv4_valid(const char *address) { struct in_addr ip;return inet_pton(AF_INET,address,&ip)==1; }
int platform_network_init(void) { return 0; }
void platform_network_cleanup(void) {}
platform_socket_t platform_udp_open(const char *address,int port) {
    struct sockaddr_in local={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
    if(inet_pton(AF_INET,address,&local.sin_addr)!=1) { errno=EINVAL;return PLATFORM_SOCKET_INVALID; }
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0)return PLATFORM_SOCKET_INVALID;
    if(bind(fd,(struct sockaddr *)&local,sizeof(local))<0) {
        int error=errno;close(fd);errno=error;return PLATFORM_SOCKET_INVALID;
    }
    return fd;
}
void platform_udp_close(platform_socket_t socket) { close(socket); }
int platform_udp_receive(platform_socket_t socket,uint8_t *bytes,size_t capacity,int timeout_ms,struct sockaddr_in *peer) {
    struct pollfd fd={.fd=socket,.events=POLLIN};
    if(poll(&fd,1,timeout_ms)<=0 || !(fd.revents&POLLIN))return 0;
    socklen_t size=sizeof(*peer);
    return (int)recvfrom(socket,bytes,capacity,0,(struct sockaddr *)peer,&size);
}
void platform_udp_send(platform_socket_t socket,const uint8_t *bytes,size_t length,const struct sockaddr_in *peer) {
    sendto(socket,bytes,length,0,(const struct sockaddr *)peer,sizeof(*peer));
}
