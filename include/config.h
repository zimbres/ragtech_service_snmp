#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_FILE_MAX_PATH 256
#define CONFIG_MAX_STRING 128

typedef struct {
    // Serial port configuration
    char serial_port[CONFIG_MAX_STRING];
    int baud_rate;
    int data_bits;
    char parity;        // 'N', 'E', 'O'
    int stop_bits;
    int read_timeout_ms;

    // UPS protocol configuration
    int poll_interval_ms;
    int vendor_id;
    int product_id;
    float iout_calib;
    int va_rating;
    int power_factor;
    float battery_capacity_ah;   // Battery capacity in Ah (for runtime estimation)
    float battery_voltage_nominal; // Nominal battery voltage (for runtime estimation)

    // Read-only SNMP v2c agent
    char snmp_bind_address[CONFIG_MAX_STRING];
    int snmp_port;
    char snmp_community[CONFIG_MAX_STRING];

    // General
    int log_level;          // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
    bool daemonize;
    char pid_file[CONFIG_MAX_STRING];
    char state_directory[CONFIG_FILE_MAX_PATH];
} config_t;

/**
 * Load configuration from file.
 * @param path Path to config file (INI format)
 * @param cfg Output configuration structure
 * @return true on success, false on failure
 */
bool config_load(const char *path, config_t *cfg);

/**
 * Get default configuration values.
 * @param cfg Output configuration structure (will be filled with defaults)
 */
void config_get_defaults(config_t *cfg);

/**
 * Print configuration to stdout (for debugging).
 * @param cfg Configuration to print
 */
void config_print(const config_t *cfg);

#endif // CONFIG_H