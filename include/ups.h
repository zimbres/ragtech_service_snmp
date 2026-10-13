#ifndef UPS_H
#define UPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "config.h"

// Struct para armazenar métricas do UPS
typedef struct {
    float power_out_percent;
    float current_out;
    float voltage_out;
    float voltage_in;
    float power_out;
    float power_in;
    float energy_out;
    float energy_in;
    float temperature;
    float battery_state;
    float battery_voltage;
    float frequency;
    float battery_runtime;      // Estimated runtime in minutes (when on battery)
} ups_metrics_t;

// Status/flags — Família 10 (Easy Pro 1200 TI)
// reg 0x0090, 0x0091, 0x0092 conforme devices.xml família 10
typedef struct {
    // reg 0x0090 — Status operacional e entrada
    bool no_battery;             // F_NOBAT      bit 0
    bool old_battery;            // F_OLDBAT     bit 1
    bool op_checkup;             // F_OPCHECKUP  bit 2
    bool no_v_input;             // F_NOVINPUT   bit 3
    bool lo_v_input;             // F_LOVINPUT   bit 4
    bool hi_v_input;             // F_HIVINPUT   bit 5
    bool op_battery;             // F_OPBATTERY  bit 6
    bool hi_p_output;            // F_HIPOUTPUT  bit 7
    // reg 0x0091 — Falhas de hardware
    bool lo_battery;             // F_LOBATTERY      bit 0
    bool fail_overtemp;          // F_FOVERTEMP      bit 1
    bool fail_end_battery;       // F_FENDBATTERY    bit 2
    bool fail_overload;          // F_FOVERLOAD      bit 3
    bool fail_abnormal_vout;     // F_FABNORMALVOUT  bit 4
    bool fail_abnormal_vbat;     // F_FABNORMALVBAT  bit 5
    bool fail_inverter;          // F_FINVERTER      bit 6
    bool fail_shortcircuit;      // F_FSHORTCIRCUIT  bit 7
    // reg 0x0092 — Flags complementares
    bool sync_input;             // F_SYNCIN   bit 0
    bool more_battery;           // F_MOREBAT  bit 2
    bool less_battery;           // F_LESSBAT  bit 3
    bool remote_control_active;  // F_RCTRLON  bit 5
} ups_status_t;

// Global state (protected by the UPS mutex)
extern volatile ups_metrics_t g_ups_metrics;
extern volatile ups_status_t  g_ups_status;
extern volatile bool          g_ups_connected;

// Último buffer raw recebido do UPS (para diagnóstico)
#define UPS_RAW_BUF_SIZE 64
extern uint8_t  g_ups_raw_buf[UPS_RAW_BUF_SIZE];
extern size_t   g_ups_raw_len;

/**
 * Initialize UPS communication.
 * @param cfg Configuration
 * @return 0 on success, negative on error
 */
int ups_init(const config_t *cfg);

/**
 * Start UPS polling thread.
 * @return 0 on success, negative on error
 */
int ups_start(void);

/**
 * Stop UPS polling thread.
 */
void ups_stop(void);

/**
 * Cleanup UPS resources.
 */
void ups_cleanup(void);

/**
 * Parse status flags from buffer.
 * @param buf Raw buffer from UPS
 * @param s Output status structure
 */
void ups_parse_status_flags(const uint8_t *buf, ups_status_t *s);

/**
 * Get current metrics (thread-safe copy).
 * @param metrics Output metrics structure
 */
void ups_get_metrics(ups_metrics_t *metrics);

/**
 * Get current status (thread-safe copy).
 * @param status Output status structure
 */
void ups_get_status(ups_status_t *status);

/**
 * Check if UPS is connected.
 * @return true if connected
 */
bool ups_is_connected(void);

typedef struct {
    ups_metrics_t metrics;
    ups_status_t status;
    bool connected;
    char model[64];
    unsigned seconds_on_battery;
    uint8_t raw[UPS_RAW_BUF_SIZE];
    size_t raw_len;
} ups_snapshot_t;
void ups_get_snapshot(ups_snapshot_t *snapshot);

#endif // UPS_H