#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} log_level_t;

extern log_level_t g_log_level;

/**
 * Initialize logging system.
 * @param level Minimum log level to output
 * @param use_syslog If true, also log to syslog
 * @param ident Syslog identifier (used if use_syslog is true)
 */
void log_init(log_level_t level, bool use_syslog, const char *ident);

/**
 * Set log level at runtime.
 * @param level New log level
 */
void log_set_level(log_level_t level);

/**
 * Log a formatted message.
 * @param level Log level
 * @param file Source file (use __FILE__)
 * @param line Source line (use __LINE__)
 * @param func Function name (use __func__)
 * @param fmt Format string
 * @param ... Format arguments
 */
void log_log(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);

/**
 * Log a message with explicit tag.
 * @param level Log level
 * @param tag Tag string
 * @param fmt Format string
 * @param ... Format arguments
 */
void log_log_tag(log_level_t level, const char *tag, const char *fmt, ...);

// Standard C variadic macros, supported by MSVC and GCC/Clang.
#define LOG_DEBUG(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)  log_log(LOG_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)  log_log(LOG_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...) log_log(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_TAG_DEBUG(tag, ...) log_log_tag(LOG_DEBUG, tag, __VA_ARGS__)
#define LOG_TAG_INFO(tag, ...)  log_log_tag(LOG_INFO, tag, __VA_ARGS__)
#define LOG_TAG_WARN(tag, ...)  log_log_tag(LOG_WARN, tag, __VA_ARGS__)
#define LOG_TAG_ERROR(tag, ...) log_log_tag(LOG_ERROR, tag, __VA_ARGS__)

// For messages with no format arguments, use these:
#define LOG_DEBUG_S(msg)     log_log(LOG_DEBUG, __FILE__, __LINE__, __func__, "%s", msg)
#define LOG_INFO_S(msg)      log_log(LOG_INFO,  __FILE__, __LINE__, __func__, "%s", msg)
#define LOG_WARN_S(msg)      log_log(LOG_WARN,  __FILE__, __LINE__, __func__, "%s", msg)
#define LOG_ERROR_S(msg)     log_log(LOG_ERROR, __FILE__, __LINE__, __func__, "%s", msg)

#define LOG_TAG_DEBUG_S(tag, msg) log_log_tag(LOG_DEBUG, tag, "%s", msg)
#define LOG_TAG_INFO_S(tag, msg)  log_log_tag(LOG_INFO,  tag, "%s", msg)
#define LOG_TAG_WARN_S(tag, msg)  log_log_tag(LOG_WARN,  tag, "%s", msg)
#define LOG_TAG_ERROR_S(tag, msg) log_log_tag(LOG_ERROR, tag, "%s", msg)

/**
 * Cleanup logging system (close syslog).
 */
void log_cleanup(void);

#endif // LOG_H