#include "platform.h"
#include <errno.h>
#include <limits.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_mutex_lock(platform_mutex_t *mutex) { AcquireSRWLockExclusive(mutex); }
void platform_mutex_unlock(platform_mutex_t *mutex) { ReleaseSRWLockExclusive(mutex); }
bool platform_flag_get(platform_flag_t *flag) { return InterlockedCompareExchange(flag,0,0)!=0; }
void platform_flag_set(platform_flag_t *flag,bool value) { InterlockedExchange(flag,value ? 1 : 0); }
typedef struct { void *(*entry)(void *); void *arg; } thread_start;
static unsigned __stdcall thread_entry(void *context) {
    thread_start start=*(thread_start *)context;free(context);
    start.entry(start.arg);return 0;
}
int platform_thread_start(platform_thread_t *thread,void *(*entry)(void *),void *arg) {
    thread_start *start=malloc(sizeof(*start));
    if(!start) { SetLastError(ERROR_NOT_ENOUGH_MEMORY);return -1; }
    start->entry=entry;start->arg=arg;
    uintptr_t handle=_beginthreadex(NULL,0,thread_entry,start,0,NULL);
    if(!handle) { free(start);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return -1; }
    *thread=(HANDLE)handle;return 0;
}
void platform_thread_join(platform_thread_t thread) { WaitForSingleObject(thread,INFINITE);CloseHandle(thread); }
uint64_t platform_monotonic_ms(void) { return (uint64_t)GetTickCount64(); }
void platform_sleep_ms(unsigned ms) { Sleep((DWORD)ms); }
void platform_timestamp(char *text,size_t capacity) {
    SYSTEMTIME now;GetLocalTime(&now);
    snprintf(text,capacity,"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        (unsigned)now.wYear,(unsigned)now.wMonth,(unsigned)now.wDay,
        (unsigned)now.wHour,(unsigned)now.wMinute,(unsigned)now.wSecond,(unsigned)now.wMilliseconds);
}
const char *platform_error(void) {
    /* One buffer per calling thread; capture GetLastError before formatting. */
#ifdef _MSC_VER
    static __declspec(thread) char text[256];
#else
    static _Thread_local char text[256];
#endif
    DWORD code=GetLastError();
    DWORD n=FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL,code,0,text,sizeof(text),NULL);
    if(!n)snprintf(text,sizeof(text),"Windows error %lu",(unsigned long)code);
    else while(n && (text[n-1]=='\r' || text[n-1]=='\n'))text[--n]='\0';
    return text;
}
int platform_make_directory(const char *path) {
    if(CreateDirectoryA(path,NULL))return 0;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        DWORD attributes=GetFileAttributesA(path);
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY))return 0;
    }
    return -1;
}
platform_serial_t platform_serial_open(const config_t *cfg) {
    char name[CONFIG_MAX_STRING+8];
    if(strncmp(cfg->serial_port,"\\\\.\\",4)==0)snprintf(name,sizeof(name),"%s",cfg->serial_port);
    else snprintf(name,sizeof(name),"\\\\.\\%s",cfg->serial_port);
    HANDLE serial=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(serial==INVALID_HANDLE_VALUE)return PLATFORM_SERIAL_INVALID;
    DCB dcb={0},actual={0};COMMTIMEOUTS timeouts={0};DWORD error;
    dcb.DCBlength=sizeof(dcb);
    if(!GetCommState(serial,&dcb))goto failure;
    dcb.BaudRate=(DWORD)cfg->baud_rate;dcb.ByteSize=(BYTE)cfg->data_bits;
    dcb.Parity=cfg->parity=='E' ? EVENPARITY : cfg->parity=='O' ? ODDPARITY : NOPARITY;
    dcb.StopBits=cfg->stop_bits==2 ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary=TRUE;dcb.fParity=cfg->parity!='N';
    dcb.fOutxCtsFlow=FALSE;dcb.fOutxDsrFlow=FALSE;dcb.fDsrSensitivity=FALSE;
    dcb.fOutX=FALSE;dcb.fInX=FALSE;dcb.fErrorChar=FALSE;dcb.fNull=FALSE;
    dcb.fAbortOnError=FALSE;dcb.fDtrControl=DTR_CONTROL_ENABLE;dcb.fRtsControl=RTS_CONTROL_ENABLE;
    if(!SetCommState(serial,&dcb))goto failure;
    /* Read back driver settings: fail if the requested custom rate was changed. */
    actual.DCBlength=sizeof(actual);
    if(!GetCommState(serial,&actual))goto failure;
    if(actual.BaudRate!=(DWORD)cfg->baud_rate || actual.ByteSize!=(BYTE)cfg->data_bits ||
       actual.Parity!=dcb.Parity || actual.StopBits!=dcb.StopBits) {
        SetLastError(ERROR_INVALID_PARAMETER);goto failure;
    }
    timeouts.ReadIntervalTimeout=MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier=MAXDWORD;
    timeouts.ReadTotalTimeoutConstant=(DWORD)cfg->read_timeout_ms;
    timeouts.WriteTotalTimeoutConstant=(DWORD)cfg->read_timeout_ms;
    if(!SetCommTimeouts(serial,&timeouts))goto failure;
    if(!PurgeComm(serial,PURGE_RXCLEAR|PURGE_TXCLEAR))goto failure;
    return serial;
failure:;
    error=GetLastError();CloseHandle(serial);SetLastError(error);
    return PLATFORM_SERIAL_INVALID;
}
bool platform_serial_valid(platform_serial_t serial) { return serial!=INVALID_HANDLE_VALUE && serial!=NULL; }
void platform_serial_close(platform_serial_t serial) { if(platform_serial_valid(serial))CloseHandle(serial); }
int platform_serial_write(platform_serial_t serial,const uint8_t *bytes,size_t length) {
    DWORD written=0;
    if(length>MAXDWORD) { SetLastError(ERROR_INVALID_PARAMETER);return -1; }
    if(!WriteFile(serial,bytes,(DWORD)length,&written,NULL))return -1;
    if(written!=length) { SetLastError(ERROR_WRITE_FAULT);return -1; }
    /* WriteFile completes under COMMTIMEOUTS. Avoid FlushFileBuffers, which has
     * no timeout and can block indefinitely after unplugging a USB serial port. */
    return 0;
}
int platform_serial_read(platform_serial_t serial,uint8_t *bytes,size_t capacity,int timeout_ms) {
    if(timeout_ms<=0 || capacity>INT_MAX) { SetLastError(ERROR_INVALID_PARAMETER);return -1; }
    COMMTIMEOUTS timeouts;
    if(!GetCommTimeouts(serial,&timeouts))return -1;
    /* Return queued bytes immediately, otherwise wait only for the first byte.
     * This preserves byte-at-a-time responses and the protocol's total deadline. */
    timeouts.ReadIntervalTimeout=MAXDWORD;timeouts.ReadTotalTimeoutMultiplier=MAXDWORD;
    timeouts.ReadTotalTimeoutConstant=(DWORD)timeout_ms;
    if(!SetCommTimeouts(serial,&timeouts))return -1;
    DWORD n=0;
    if(!ReadFile(serial,bytes,(DWORD)capacity,&n,NULL))return -1;
    return (int)n;
}
void platform_serial_flush(platform_serial_t serial) { PurgeComm(serial,PURGE_RXCLEAR); }
bool platform_ipv4_valid(const char *address) { struct in_addr ip;return InetPtonA(AF_INET,address,&ip)==1; }
int platform_network_init(void) {
    WSADATA data;int result=WSAStartup(MAKEWORD(2,2),&data);
    if(result)SetLastError((DWORD)result);
    return result ? -1 : 0;
}
void platform_network_cleanup(void) { WSACleanup(); }
platform_socket_t platform_udp_open(const char *address,int port) {
    struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_port=htons((u_short)port);
    if(InetPtonA(AF_INET,address,&local.sin_addr)!=1) { SetLastError(ERROR_INVALID_PARAMETER);return INVALID_SOCKET; }
    SOCKET socket_handle=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(socket_handle==INVALID_SOCKET) { SetLastError((DWORD)WSAGetLastError());return INVALID_SOCKET; }
    /* Prevent another agent from binding the same address/port on Windows. */
    BOOL exclusive=TRUE;
    if(setsockopt(socket_handle,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char *)&exclusive,sizeof(exclusive))==SOCKET_ERROR ||
       bind(socket_handle,(struct sockaddr *)&local,sizeof(local))==SOCKET_ERROR) {
        DWORD error=(DWORD)WSAGetLastError();closesocket(socket_handle);SetLastError(error);return INVALID_SOCKET;
    }
    return socket_handle;
}
void platform_udp_close(platform_socket_t socket_handle) { closesocket(socket_handle); }
int platform_udp_receive(platform_socket_t socket_handle,uint8_t *bytes,size_t capacity,int timeout_ms,struct sockaddr_in *peer) {
    fd_set ready;FD_ZERO(&ready);FD_SET(socket_handle,&ready);
    struct timeval delay={timeout_ms/1000,(timeout_ms%1000)*1000};
    int result=select(0,&ready,NULL,NULL,&delay);
    if(result<=0)return 0;
    int size=sizeof(*peer);
    int n=recvfrom(socket_handle,(char *)bytes,(int)capacity,0,(struct sockaddr *)peer,&size);
    return n==SOCKET_ERROR ? -1 : n;
}
void platform_udp_send(platform_socket_t socket_handle,const uint8_t *bytes,size_t length,const struct sockaddr_in *peer) {
    sendto(socket_handle,(const char *)bytes,(int)length,0,(const struct sockaddr *)peer,sizeof(*peer));
}
