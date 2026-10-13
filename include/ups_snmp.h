#ifndef UPS_SNMP_H
#define UPS_SNMP_H
#include "config.h"
#include <stdint.h>
#include <stddef.h>
int snmp_init(const config_t *cfg);
void snmp_poll(int timeout_ms);
void snmp_cleanup(void);
/* Returns zero for invalid/unauthorized requests. Also used by protocol tests. */
size_t snmp_response(const config_t *cfg, const uint8_t *request, size_t length, uint8_t *response, size_t capacity);
#endif
