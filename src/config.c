#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "platform.h"

// Simple INI parser
static char *strtrim(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

static bool parse_line(char *line, char **key, char **value) {
    line = strtrim(line);
    if (*line == '\0' || *line == '#' || *line == ';') return false;
    
    char *eq = strchr(line, '=');
    if (!eq) return false;
    
    *eq = '\0';
    *key = strtrim(line);
    char *val = strtrim(eq + 1);
    
    // Remove quotes if present
    if (strlen(val) >= 2 && val[0] == '"' && val[strlen(val) - 1] == '"') {
        val[strlen(val) - 1] = '\0';
        val++;
    }
    *value = val;
    
    return true;
}

void config_get_defaults(config_t *cfg) {
    memset(cfg, 0, sizeof(config_t));
    
    // Serial port defaults
#ifdef _WIN32
    strcpy(cfg->serial_port, "COM3");
    const char *program_data=getenv("PROGRAMDATA");
    snprintf(cfg->state_directory,sizeof(cfg->state_directory),"%s/Ragtech",program_data ? program_data : "C:/ProgramData");
#else
    strcpy(cfg->serial_port, "/dev/ttyUSB0");
    strcpy(cfg->state_directory, "/var/lib/ragtech");
#endif
    cfg->baud_rate = 2560;  // Ragtech uses 2560 baud
    cfg->data_bits = 8;
    cfg->parity = 'N';
    cfg->stop_bits = 1;
    cfg->read_timeout_ms = 1000;
    
    // UPS protocol defaults
    cfg->poll_interval_ms = 5000;
    cfg->vendor_id = 0x04D8;  // Microchip (common for Ragtech)
    cfg->product_id = 0x000A;
    cfg->iout_calib = 16.0f;
    cfg->va_rating = 1200;
    cfg->power_factor = 70;
    cfg->battery_capacity_ah = 9.0f;       // Default 9Ah for 1200VA UPS
    cfg->battery_voltage_nominal = 12.0f;  // Default 12V nominal (per cell)
    
    strcpy(cfg->snmp_bind_address, "0.0.0.0");
    cfg->snmp_port = 161;
    strcpy(cfg->snmp_community, "public");

    // General
    cfg->log_level = LOG_INFO;
    cfg->daemonize = false;
    cfg->pid_file[0] = '\0';
}

bool config_load(const char *path, config_t *cfg) {
    config_get_defaults(cfg);
    
    FILE *fp = fopen(path, "r");
    if (!fp) {
        LOG_ERROR("Failed to open config file %s: %s", path, strerror(errno));
        return false;
    }
    
    char line[512];
    int line_num = 0;
    bool in_section = false;
    enum { SEC_NONE, SEC_SERIAL, SEC_UPS, SEC_SNMP, SEC_GENERAL } current_section = SEC_NONE;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char *trimmed = strtrim(line);
        
        // Skip comments and empty lines
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') continue;
        
        // Handle sections
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                const char *sec_name = trimmed + 1;
                if (strcmp(sec_name, "serial") == 0) {
                    current_section = SEC_SERIAL;
                    in_section = true;
                } else if (strcmp(sec_name, "ups") == 0) {
                    current_section = SEC_UPS;
                    in_section = true;
                } else if (strcmp(sec_name, "snmp") == 0) {
                    current_section = SEC_SNMP;
                    in_section = true;
                } else if (strcmp(sec_name, "general") == 0) {
                    current_section = SEC_GENERAL;
                    in_section = true;
                } else {
                    current_section = SEC_NONE;
                    in_section = false;
                }
            }
            continue;
        }
        
        if (!in_section) continue;
        
        char *key, *value;
        if (!parse_line(trimmed, &key, &value)) continue;
        
        // Serial section
        if (current_section == SEC_SERIAL) {
            if (strcmp(key, "port") == 0) {
                strncpy(cfg->serial_port, value, CONFIG_MAX_STRING - 1);
            } else if (strcmp(key, "baud_rate") == 0) {
                cfg->baud_rate = atoi(value);
            } else if (strcmp(key, "data_bits") == 0) {
                cfg->data_bits = atoi(value);
            } else if (strcmp(key, "parity") == 0) {
                cfg->parity = toupper(value[0]);
            } else if (strcmp(key, "stop_bits") == 0) {
                cfg->stop_bits = atoi(value);
            } else if (strcmp(key, "read_timeout_ms") == 0) {
                cfg->read_timeout_ms = atoi(value);
            } else {
                LOG_WARN("Unknown config key in [serial]: %s (line %d)", key, line_num);
            }
        }
        // UPS section
        else if (current_section == SEC_UPS) {
            if (strcmp(key, "poll_interval_ms") == 0) {
                cfg->poll_interval_ms = atoi(value);
            } else if (strcmp(key, "vendor_id") == 0) {
                cfg->vendor_id = strtol(value, NULL, 0);
            } else if (strcmp(key, "product_id") == 0) {
                cfg->product_id = strtol(value, NULL, 0);
            } else if (strcmp(key, "iout_calib") == 0) {
                cfg->iout_calib = atof(value);
            } else if (strcmp(key, "va_rating") == 0) {
                cfg->va_rating = atoi(value);
            } else if (strcmp(key, "power_factor") == 0) {
                cfg->power_factor = atoi(value);
            } else if (strcmp(key, "battery_capacity_ah") == 0) {
                cfg->battery_capacity_ah = atof(value);
            } else if (strcmp(key, "battery_voltage_nominal") == 0) {
                cfg->battery_voltage_nominal = atof(value);
            } else {
                LOG_WARN("Unknown config key in [ups]: %s (line %d)", key, line_num);
            }
        }
        else if (current_section == SEC_SNMP) {
            if (strcmp(key, "bind_address") == 0)
                strncpy(cfg->snmp_bind_address, value, CONFIG_MAX_STRING - 1);
            else if (strcmp(key, "port") == 0) {
                char *end;
                long port = strtol(value, &end, 10);
                if (!*value || *end || port < 1 || port > 65535) {
                    fclose(fp);
                    return false;
                }
                cfg->snmp_port = (int)port;
            } else if (strcmp(key, "community") == 0)
                strncpy(cfg->snmp_community, value, CONFIG_MAX_STRING - 1);
            else LOG_WARN("Unknown config key in [snmp]: %s", key);
        }
        // General section
        else if (current_section == SEC_GENERAL) {
            if (strcmp(key, "log_level") == 0) {
                if (strcmp(value, "debug") == 0) cfg->log_level = LOG_DEBUG;
                else if (strcmp(value, "info") == 0) cfg->log_level = LOG_INFO;
                else if (strcmp(value, "warn") == 0) cfg->log_level = LOG_WARN;
                else if (strcmp(value, "error") == 0) cfg->log_level = LOG_ERROR;
                else cfg->log_level = atoi(value);
            } else if (strcmp(key, "daemonize") == 0) {
                cfg->daemonize = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "state_directory") == 0) {
                if (strlen(value)>=sizeof(cfg->state_directory)) { fclose(fp);return false; }
                strcpy(cfg->state_directory,value);
            } else if (strcmp(key, "pid_file") == 0) {
                strncpy(cfg->pid_file, value, CONFIG_MAX_STRING - 1);
            } else {
                LOG_WARN("Unknown config key in [general]: %s (line %d)", key, line_num);
            }
        }
    }
    
    fclose(fp);
    if (!isfinite(cfg->iout_calib) || cfg->iout_calib <= 0 || cfg->iout_calib > 255) {
        LOG_ERROR_S("iout_calib must be positive and at most 255 (one-byte register)");
        return false;
    }
    if (!platform_ipv4_valid(cfg->snmp_bind_address)) return false;
    if (!cfg->state_directory[0] || !cfg->serial_port[0] || cfg->baud_rate<=0 ||
        cfg->data_bits<5 || cfg->data_bits>8 || (cfg->stop_bits!=1 && cfg->stop_bits!=2) ||
        (cfg->parity!='N' && cfg->parity!='E' && cfg->parity!='O')) return false;
    if (!cfg->snmp_community[0] || cfg->poll_interval_ms <= 0 || cfg->read_timeout_ms <= 0)
        return false;
    LOG_INFO("Configuration loaded from %s", path);
    return true;
}

void config_print(const config_t *cfg) {
    printf("=== Configuration ===\n");
    printf("[serial]\n");
    printf("  port = %s\n", cfg->serial_port);
    printf("  baud_rate = %d\n", cfg->baud_rate);
    printf("  data_bits = %d\n", cfg->data_bits);
    printf("  parity = %c\n", cfg->parity);
    printf("  stop_bits = %d\n", cfg->stop_bits);
    printf("  read_timeout_ms = %d\n", cfg->read_timeout_ms);
    printf("[ups]\n");
    printf("  poll_interval_ms = %d\n", cfg->poll_interval_ms);
    printf("  vendor_id = 0x%04X\n", cfg->vendor_id);
    printf("  product_id = 0x%04X\n", cfg->product_id);
    printf("  iout_calib = %.1f\n", cfg->iout_calib);
    printf("  va_rating = %d\n", cfg->va_rating);
    printf("  power_factor = %d\n", cfg->power_factor);
    printf("  battery_capacity_ah = %.1f\n", cfg->battery_capacity_ah);
    printf("  battery_voltage_nominal = %.1f\n", cfg->battery_voltage_nominal);
    printf("[snmp]\n  bind_address = %s\n  port = %d\n  community = ***\n",
           cfg->snmp_bind_address, cfg->snmp_port);
    printf("[general]\n");
    printf("  log_level = %d\n", cfg->log_level);
    printf("  daemonize = %s\n", cfg->daemonize ? "true" : "false");
    printf("  state_directory = %s\n", cfg->state_directory);
    printf("  pid_file = %s\n", cfg->pid_file[0] ? cfg->pid_file : "(none)");
    printf("=====================\n");
}