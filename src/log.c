#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform.h"
#ifndef _WIN32
#include <syslog.h>
#endif
#include <stdbool.h>

// Undefine syslog macros that conflict with our log_level_t enum
#undef LOG_DEBUG
#undef LOG_INFO

static log_level_t g_current_level = LOG_INFO;
static bool g_use_syslog = false;
static platform_mutex_t g_log_mutex = PLATFORM_MUTEX_INITIALIZER;

log_level_t g_log_level = LOG_INFO;

void log_init(log_level_t level, bool use_syslog, const char *ident) {
    g_current_level = level;
    g_log_level = level;
    g_use_syslog = use_syslog;
    
#ifndef _WIN32
    if (g_use_syslog && ident) openlog(ident, LOG_PID | LOG_NDELAY, LOG_DAEMON);
#else
    (void)ident;g_use_syslog=false;
#endif
}

void log_set_level(log_level_t level) {
    platform_mutex_lock(&g_log_mutex);
    g_current_level = level;
    g_log_level = level;
    platform_mutex_unlock(&g_log_mutex);
}

static const char *level_to_str(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

#ifndef _WIN32
static int level_to_syslog(log_level_t level) {
    switch(level) {
        case LOG_DEBUG:return 7;
        case LOG_INFO:return 6;
        case LOG_WARN:return 4;
        case LOG_ERROR:return 3;
        default:return 6;
    }
}
#endif

void log_log(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
    if (level < g_current_level) return;
    
    platform_mutex_lock(&g_log_mutex);
    
    // Get timestamp
    char timestamp[40];
    platform_timestamp(timestamp,sizeof(timestamp));
    
    // Extract basename from file path
    const char *basename=file;
    for (const char *cursor=file;*cursor;cursor++)
        if (*cursor=='/' || *cursor=='\\') basename=cursor+1;
    
    // Format message
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    
    // Output to stderr
    fprintf(stderr, "[%s] %-5s [%s:%d] %s(): %s\n",
            timestamp, level_to_str(level), basename, line, func, msg);
    fflush(stderr);
    
    // Output to syslog if enabled
#ifndef _WIN32
    if (g_use_syslog) {
        syslog(level_to_syslog(level), "[%s:%d] %s(): %s", basename, line, func, msg);
    }
    
#endif
    platform_mutex_unlock(&g_log_mutex);
}

void log_log_tag(log_level_t level, const char *tag, const char *fmt, ...) {
    if (level < g_current_level) return;
    
    platform_mutex_lock(&g_log_mutex);
    
    // Get timestamp
    char timestamp[40];
    platform_timestamp(timestamp,sizeof(timestamp));
    
    // Format message
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    
    // Output to stderr
    fprintf(stderr, "[%s] %-5s [%s] %s\n",
            timestamp, level_to_str(level), tag, msg);
    fflush(stderr);
    
    // Output to syslog if enabled
#ifndef _WIN32
    if (g_use_syslog) {
        syslog(level_to_syslog(level), "[%s] %s", tag, msg);
    }
    
#endif
    platform_mutex_unlock(&g_log_mutex);
}

// Cleanup function (call at program exit)
void log_cleanup(void) {
#ifndef _WIN32
    if (g_use_syslog) {
        closelog();
    }
#endif
}