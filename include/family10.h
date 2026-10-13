#ifndef FAMILY10_H
#define FAMILY10_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint8_t id;
    const char *name;
    float current_factor;
    float output_voltage_factor;
    float battery_voltage_factor;
} family10_model_t;
const family10_model_t *family10_model(uint8_t id);
float family10_frequency(uint8_t raw, uint8_t osc53, uint8_t osc57, bool valid);
void family10_read_command(uint16_t address, uint8_t count, uint8_t command[6]);
typedef int (*family10_send_fn)(void *context, const uint8_t *data, size_t length);
typedef int (*family10_receive_fn)(void *context, uint8_t *data, size_t capacity, int timeout_ms);
/* Read a SOF + count-byte response using the same framing as normal polling.
 * -1 transport error, -2 timeout, -3 invalid framing; zero on success.
 * The returned raw bytes are available for diagnostics even on failure. */
int family10_read_registers(void *context, family10_send_fn send,
    family10_receive_fn receive, uint16_t address, uint8_t count,
    uint8_t *raw, size_t *raw_length, int timeout_ms);
#endif
