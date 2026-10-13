#ifndef RAGTECH_PLATFORM_H
#define RAGTECH_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SRWLOCK platform_mutex_t;
#define PLATFORM_MUTEX_INITIALIZER SRWLOCK_INIT
typedef HANDLE platform_thread_t;
typedef volatile LONG platform_flag_t;
typedef HANDLE platform_serial_t;
#define PLATFORM_SERIAL_INVALID INVALID_HANDLE_VALUE
typedef SOCKET platform_socket_t;
#define PLATFORM_SOCKET_INVALID INVALID_SOCKET
#else
#include <pthread.h>
#include <stdatomic.h>
#include <netinet/in.h>
typedef pthread_mutex_t platform_mutex_t;
#define PLATFORM_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
typedef pthread_t platform_thread_t;
typedef atomic_bool platform_flag_t;
typedef int platform_serial_t;
#define PLATFORM_SERIAL_INVALID (-1)
typedef int platform_socket_t;
#define PLATFORM_SOCKET_INVALID (-1)
#endif
/* System-specific operations; protocol and metrics stay in shared C code. */
void platform_mutex_lock(platform_mutex_t *mutex);
void platform_mutex_unlock(platform_mutex_t *mutex);
bool platform_flag_get(platform_flag_t *flag);
void platform_flag_set(platform_flag_t *flag, bool value);
int platform_thread_start(platform_thread_t *thread, void *(*entry)(void *), void *arg);
void platform_thread_join(platform_thread_t thread);
uint64_t platform_monotonic_ms(void);
void platform_sleep_ms(unsigned ms);
void platform_timestamp(char *text, size_t capacity);
const char *platform_error(void);
int platform_make_directory(const char *path);
platform_serial_t platform_serial_open(const config_t *cfg);
bool platform_serial_valid(platform_serial_t serial);
void platform_serial_close(platform_serial_t serial);
int platform_serial_write(platform_serial_t serial, const uint8_t *bytes, size_t length);
int platform_serial_read(platform_serial_t serial, uint8_t *bytes, size_t capacity, int timeout_ms);
void platform_serial_flush(platform_serial_t serial);
bool platform_ipv4_valid(const char *address);
int platform_network_init(void);
void platform_network_cleanup(void);
platform_socket_t platform_udp_open(const char *address, int port);
void platform_udp_close(platform_socket_t socket);
int platform_udp_receive(platform_socket_t socket, uint8_t *bytes, size_t capacity,
                         int timeout_ms, struct sockaddr_in *peer);
void platform_udp_send(platform_socket_t socket, const uint8_t *bytes, size_t length,
                       const struct sockaddr_in *peer);
#endif
