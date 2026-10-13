#define _GNU_SOURCE
#include "ups.h"
#include "family10.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include <errno.h>
#include <math.h>
#include <time.h>

// UPS Protocol constants (from original ESP32 code)
#define UPS_MIN_PACKET_SIZE 31
#define UPS_RAW_BUF_SIZE 64

// Comandos do protocolo Ragtech
static const uint8_t cmd_req1[] = {0xFF, 0xFE, 0x00, 0x8E, 0x01, 0x8F};
static const uint8_t cmd_get_data[] = {0xAA, 0x04, 0x00, 0x80, 0x1E, 0x9E};

// Global state
volatile ups_metrics_t g_ups_metrics = {0};
volatile ups_status_t  g_ups_status  = {0};
volatile bool          g_ups_connected = false;

uint8_t  g_ups_raw_buf[UPS_RAW_BUF_SIZE] = {0};
size_t   g_ups_raw_len = 0;

// Thread synchronization
static platform_mutex_t g_ups_mutex = PLATFORM_MUTEX_INITIALIZER;
static platform_thread_t g_ups_thread;
static platform_flag_t g_ups_running = 0;
static bool g_thread_started;
static char g_model[64] = "Unknown";
static uint64_t g_battery_since_ms;
static bool g_on_battery;
static platform_serial_t g_serial_fd = PLATFORM_SERIAL_INVALID;
static config_t g_config;
static uint8_t g_osc53, g_osc57;
static bool g_frequency_calibrated;

// Energy persistence
static char g_energy_file[CONFIG_FILE_MAX_PATH + 16];
#define ENERGY_SAVE_INTERVAL_S 60

// Carrega energia do arquivo
static void energy_load(float *kwh_out, float *kwh_in) {
    FILE *fp = fopen(g_energy_file, "rb");
    if (!fp) {
        *kwh_out = 0.0f;
        *kwh_in = 0.0f;
        return;
    }
    
    float data[2];
    if (fread(data, sizeof(float), 2, fp) == 2) {
        *kwh_out = data[0];
        *kwh_in = data[1];
    }
    fclose(fp);
}

// Salva energia no arquivo
static void energy_save(float kwh_out, float kwh_in) {
    // Create directory if needed
    if (platform_make_directory(g_config.state_directory)<0) {
        LOG_WARN("Cannot create state directory %s: %s", g_config.state_directory, platform_error());
        return;
    }
    
    FILE *fp = fopen(g_energy_file, "wb");
    if (!fp) {
        LOG_WARN("Failed to save energy data: %s", strerror(errno));
        return;
    }
    
    float data[2] = {kwh_out, kwh_in};
    fwrite(data, sizeof(float), 2, fp);
    fclose(fp);
}

// Processamento de potência: acumula energia e persiste periodicamente
static void process_power(float wOut, float wIn, float *power_current_kwh_out, float *power_current_kwh_in, time_t *power_last_update) {
    time_t curr = time(NULL);
    float diff = (float)(curr - *power_last_update);
    *power_last_update = curr;

    *power_current_kwh_out += (wOut / 1000.0f) * (diff / 3600.0f);
    *power_current_kwh_in  += (wIn  / 1000.0f) * (diff / 3600.0f);

    // Persistência periódica
    static time_t last_save = 0;
    if (curr - last_save >= ENERGY_SAVE_INTERVAL_S) {
        last_save = curr;
        energy_save(*power_current_kwh_out, *power_current_kwh_in);
    }
}

static int ups_send_cmd(platform_serial_t serial, const uint8_t *bytes, size_t length) {
    if (platform_serial_write(serial, bytes, length)<0) {
        LOG_ERROR("Serial write failed: %s", platform_error());
        return -1;
    }
    return 0;
}
static int ups_read_response(platform_serial_t serial, uint8_t *bytes, size_t capacity, int timeout_ms) {
    int result=platform_serial_read(serial, bytes, capacity, timeout_ms);
    if (result<0) LOG_ERROR("Serial read failed: %s", platform_error());
    return result;
}
/* Polling waits are interruptible so Ctrl+C can join the worker promptly. */
static void ups_wait_ms(unsigned ms) {
    while (ms && platform_flag_get(&g_ups_running)) {
        unsigned part=ms>100 ? 100 : ms;
        platform_sleep_ms(part);ms-=part;
    }
}

static int serial_send_registers(void *context, const uint8_t *data, size_t length) {
    return ups_send_cmd(*(platform_serial_t *)context, data, length);
}
static int serial_receive_registers(void *context, uint8_t *data, size_t capacity, int timeout_ms) {
    return ups_read_response(*(platform_serial_t *)context, data, capacity, timeout_ms);
}
static int ups_read_registers(platform_serial_t fd, uint16_t address, uint8_t count, uint8_t *data) {
    uint8_t raw[31], command[6]; size_t length;
    family10_read_command(address, count, command);
    LOG_TAG_DEBUG("UPS", "Read register %04X count=%u command: %02X %02X %02X %02X %02X %02X",
                  address, count, command[0], command[1], command[2], command[3], command[4], command[5]);
    platform_serial_flush(fd);
    int result=family10_read_registers(&fd, serial_send_registers, serial_receive_registers,
        address, count, raw, &length, g_config.read_timeout_ms);
    char hex[94]; size_t offset=0;
    hex[0]='\0';
    for (size_t i=0;i<length;i++)
        offset+=(size_t)snprintf(hex+offset,sizeof(hex)-offset,"%02X ",raw[i]);
    LOG_TAG_DEBUG("UPS", "Register %04X response (%zu bytes): %s", address, length, hex);
    if (result<0) {
        LOG_TAG_WARN("UPS", "Register %04X read failed (%s)", address,
            result==-2 ? "timeout/incomplete response" : result==-3 ? "unexpected response format" : "transport error");
        return -1;
    }
    memcpy(data,raw+1,count);
    return 0;
}
static void ups_read_calibration(platform_serial_t fd) {
    uint8_t calibration;
    if (ups_read_registers(fd,0x00F3,1,&calibration)==0 && calibration>0) {
        g_config.iout_calib=calibration;
        LOG_INFO("Using device IOUTCALIB: %.1f", g_config.iout_calib);
    } else {
        LOG_WARN("Using fallback IOUTCALIB: %.1f", g_config.iout_calib);
    }
    uint8_t oscillators[2];
    g_frequency_calibrated=false;
    if (ups_read_registers(fd,0x0202,2,oscillators)==0 && oscillators[0]!=oscillators[1]) {
        g_osc53=oscillators[0];g_osc57=oscillators[1];g_frequency_calibrated=true;
        LOG_INFO("Using frequency calibration: OSC53=%u OSC57=%u",g_osc53,g_osc57);
    } else {
        LOG_WARN_S("Frequency calibration unavailable; SNMP frequency will be unavailable");
    }
}

// Decodifica o buffer de resposta do UPS
// op_battery: true if UPS is running on battery (for runtime estimation)
static ups_metrics_t build_payload(const uint8_t *buffer, float iout_calib, bool op_battery) {
    static float   power_current_kwh_out = 0.0f;
    static float   power_current_kwh_in  = 0.0f;
    static time_t  power_last_update     = 0;

    // Initialize energy from file on first run
    static bool energy_loaded = false;
    if (!energy_loaded) {
        energy_load(&power_current_kwh_out, &power_current_kwh_in);
        power_last_update = time(NULL);
        energy_loaded = true;
    }

    // Família 10 — Easy Pro series (auto-detect model)
    // SOF = buf[0] = 0xAA; dados em buf[1..30] = reg 0x80..0x9D
    float cBat = (float)buffer[8] * 0.3930f;    // reg 0x87 V_CBATTERY ×0.393 → %
    float vBat = (float)buffer[11] * 0.0670f;   // reg 0x8A V_VBATTERY ×0.067 → V
    float vIn  = (float)buffer[12] * 1.0600f;   // reg 0x8B V_VINPUT   ×1.06  → V
    float calib = (iout_calib > 0.0f) ? iout_calib : 1.0f;
    
    uint8_t model_id=buffer[27];
    const family10_model_t *model=family10_model(model_id);
    float iout_factor=model ? model->current_factor : 4.3f;
    const char *model_name=model ? model->name : "Unknown";
    snprintf(g_model,sizeof(g_model),"%s",model_name);
    static int last_model=-1;
    if (last_model!=model_id) {
        LOG_TAG_INFO("UPS", "Detected model: %s (ID=%u, factor=%.2f)",model_name,model_id,iout_factor);
        last_model=model_id;
    }
    float iOut=(float)buffer[13]*iout_factor/calib;
    float pPct=(float)buffer[14];
    float temp=(float)buffer[15];
    float freq=family10_frequency(buffer[24],g_osc53,g_osc57,g_frequency_calibrated);
    float vOut=(float)buffer[30]*(model ? model->output_voltage_factor : 0.555f);
    vBat=(float)buffer[11]*(model ? model->battery_voltage_factor : 0.067f);

    // Carga de potência (W)
    float wOut = iOut * vOut;
    float wIn  = iOut * vIn;

    process_power(wOut, wIn, &power_current_kwh_out, &power_current_kwh_in, &power_last_update);

    // Estimated battery runtime (minutes) - only when on battery
    // Fallback: if input voltage is very low (< 50V) but outputting power, assume on battery
    (void)op_battery;
    
    float battery_runtime = 0.0f;
    if (wOut > 0.0f && cBat > 0.0f) {
        // Battery energy available (Wh) = capacity(Ah) * voltage(V) * battery%/100
        float battery_energy_wh = g_config.battery_capacity_ah * g_config.battery_voltage_nominal * (cBat / 100.0f);
        // Runtime in hours = available energy / load power
        float runtime_hours = battery_energy_wh / wOut;
        battery_runtime = runtime_hours * 60.0f;  // Convert to minutes
        
        // Cap at reasonable maximum (e.g., 24 hours = 1440 minutes)
        if (battery_runtime > 1440.0f) battery_runtime = 1440.0f;
    }

    ups_metrics_t metrics = {
        .power_out_percent = pPct,
        .current_out       = iOut,
        .voltage_out       = vOut,
        .voltage_in        = vIn,
        .power_out         = wOut,
        .power_in          = wIn,
        .energy_out        = power_current_kwh_out,
        .energy_in         = power_current_kwh_in,
        .temperature       = temp,
        .battery_state     = cBat,
        .battery_voltage   = vBat,
        .frequency         = freq,
        .battery_runtime   = battery_runtime,
    };
    return metrics;
}

// Decodifica os bytes de status/flags do buffer de resposta
static void parse_status_flags(const uint8_t *buf, ups_status_t *s) {
    // buf[17] = reg 0x0090 — status operacional e entrada
    // buf[18] = reg 0x0091 — falhas de hardware
    // buf[19] = reg 0x0092 — flags complementares
    uint8_t op   = buf[17];  // reg 0x0090
    uint8_t fail = buf[18];  // reg 0x0091
    uint8_t misc = buf[19];  // reg 0x0092

    // reg 0x0090
    s->no_battery            = (op >> 0) & 1;  // F_NOBAT
    s->old_battery           = (op >> 1) & 1;  // F_OLDBAT
    s->op_checkup            = (op >> 2) & 1;  // F_OPCHECKUP
    s->no_v_input            = (op >> 3) & 1;  // F_NOVINPUT
    s->lo_v_input            = (op >> 4) & 1;  // F_LOVINPUT
    s->hi_v_input            = (op >> 5) & 1;  // F_HIVINPUT
    s->op_battery            = (op >> 6) & 1;  // F_OPBATTERY
    s->hi_p_output           = (op >> 7) & 1;  // F_HIPOUTPUT

    // reg 0x0091
    s->lo_battery            = (fail >> 0) & 1;  // F_LOBATTERY
    s->fail_overtemp         = (fail >> 1) & 1;  // F_FOVERTEMP
    s->fail_end_battery      = (fail >> 2) & 1;  // F_FENDBATTERY
    s->fail_overload         = (fail >> 3) & 1;  // F_FOVERLOAD
    s->fail_abnormal_vout    = (fail >> 4) & 1;  // F_FABNORMALVOUT
    s->fail_abnormal_vbat    = (fail >> 5) & 1;  // F_FABNORMALVBAT
    s->fail_inverter         = (fail >> 6) & 1;  // F_FINVERTER
    s->fail_shortcircuit     = (fail >> 7) & 1;  // F_FSHORTCIRCUIT

    // reg 0x0092
    s->sync_input            = (misc >> 0) & 1;  // F_SYNCIN
    s->more_battery          = (misc >> 2) & 1;  // F_MOREBAT
    s->less_battery          = (misc >> 3) & 1;  // F_LESSBAT
    s->remote_control_active = (misc >> 5) & 1;  // F_RCTRLON
}

// Reconnect serial port
static int ups_reconnect_serial(void) {
    if (platform_serial_valid(g_serial_fd)) {
        platform_serial_close(g_serial_fd);
        g_serial_fd = PLATFORM_SERIAL_INVALID;
    }
    
    // Try to reopen
    g_serial_fd = platform_serial_open(&g_config);
    if (!platform_serial_valid(g_serial_fd)) {
        LOG_TAG_WARN("UPS", "Failed to reopen serial port %s: %s", g_config.serial_port, platform_error());
        return -1;
    }
    
    // Send initialization command
    if (ups_send_cmd(g_serial_fd, cmd_req1, sizeof(cmd_req1)) < 0) {
        platform_serial_close(g_serial_fd);
        g_serial_fd = PLATFORM_SERIAL_INVALID;
        return -1;
    }
    
    platform_sleep_ms(100);
    
    platform_serial_flush(g_serial_fd);
    
    ups_read_calibration(g_serial_fd);
    platform_serial_flush(g_serial_fd);
    LOG_TAG_INFO_S("UPS", "Serial port reconnected successfully");
    return 0;
}

// Thread principal de polling do UPS
static void *ups_poll_thread(void *arg) {
    (void)arg;
    
    uint8_t rx_buf[256];
    uint8_t accum_buf[512];
    size_t accum_len = 0;
    int consecutive_errors = 0;
    
    LOG_TAG_INFO_S("UPS", "Poll thread started");
    
    while (platform_flag_get(&g_ups_running)) {
        // Check if serial port is valid
        if (!platform_serial_valid(g_serial_fd)) {
            platform_mutex_lock(&g_ups_mutex);
            g_ups_connected = false;
            g_on_battery = false;
            platform_mutex_unlock(&g_ups_mutex);
            
            if (ups_reconnect_serial() < 0) {
                ups_wait_ms(5000);
                continue;
            }
            consecutive_errors = 0;
            accum_len = 0;
        }
        
        // Envia comando de solicitação de dados
        if (ups_send_cmd(g_serial_fd, cmd_get_data, sizeof(cmd_get_data)) < 0) {
            consecutive_errors++;
            if (consecutive_errors == 1) {
                LOG_TAG_DEBUG_S("UPS", "Send failed, retrying...");
            } else if (consecutive_errors >= 3) {
                platform_mutex_lock(&g_ups_mutex);
                g_ups_connected = false;
                g_on_battery = false;
                platform_mutex_unlock(&g_ups_mutex);
                LOG_TAG_WARN("UPS", "Too many consecutive send errors (%d), attempting reconnect...", consecutive_errors);
                ups_reconnect_serial();
                consecutive_errors = 0;
            }
            ups_wait_ms(1000);
            continue;
        }
        
        // Wait for complete 31-byte packet (may come in multiple fragments)
        accum_len = 0;
        int read_attempts = 0;
        const int max_read_attempts = 10;  // Max 10 read attempts per command
        
        while (platform_flag_get(&g_ups_running) && read_attempts < max_read_attempts) {
            int n = ups_read_response(g_serial_fd, rx_buf, sizeof(rx_buf), g_config.read_timeout_ms);
            if (n <= 0) {
                read_attempts++;
                continue;
            }
            
            // Log raw response at debug level
            char hexbuf[192];
            char *p = hexbuf;
            for (int i = 0; i < n && i < 60; i++) {
                p += snprintf(p, sizeof(hexbuf) - (p - hexbuf), "%02X ", rx_buf[i]);
            }
            LOG_TAG_DEBUG("UPS", "Received fragment %d bytes: %s", n, hexbuf);
            
            // Reset error counter on successful read
            consecutive_errors = 0;
            
            // Acumula dados no buffer
            if (accum_len + n < sizeof(accum_buf)) {
                memcpy(accum_buf + accum_len, rx_buf, n);
                accum_len += n;
            } else {
                LOG_TAG_WARN_S("UPS", "Buffer overflow, resetting");
                accum_len = 0;
                break;
            }
            
            // Sincroniza no SOF (0xAA) — descarta bytes iniciais se desalinhado
            while (accum_len > 0 && accum_buf[0] != 0xAA) {
                memmove(accum_buf, accum_buf + 1, accum_len - 1);
                accum_len--;
            }
            
            // Verifica se recebeu dados suficientes (31 bytes completo)
            if (accum_len >= UPS_MIN_PACKET_SIZE) {
                // Validate packet: must start with 0xAA and be exactly 31 bytes for Family 10
                if (accum_buf[0] != 0xAA || accum_len != 31) {
                    LOG_TAG_DEBUG("UPS", "Invalid packet: SOF=%02X len=%zu (expected 0xAA, 31)", 
                                 accum_buf[0], accum_len);
                    // Try to find next valid SOF
                    size_t i;
                    for (i = 1; i < accum_len; i++) {
                        if (accum_buf[i] == 0xAA) break;
                    }
                    if (i < accum_len) {
                        memmove(accum_buf, accum_buf + i, accum_len - i);
                        accum_len -= i;
                    } else {
                        accum_len = 0;
                    }
                    continue;
                }
                
                // Log full packet for debugging
                char packet_hex[192];
                char *packet_cursor = packet_hex;
                for (size_t i = 0; i < accum_len && i < 60; i++) {
                    packet_cursor += snprintf(packet_cursor, sizeof(packet_hex) - (packet_cursor - packet_hex), "%02X ", accum_buf[i]);
                }
                LOG_TAG_DEBUG("UPS", "Full packet (%zu bytes): %s", accum_len, packet_hex);
                
                // Debug raw register values
                LOG_TAG_DEBUG("UPS", "Raw regs: V_CBAT[8]=%d V_VBAT[11]=%d V_VIN[12]=%d V_IOUT[13]=%d V_POUT[14]=%d V_TEMP[15]=%d V_FOUT[24]=%d V_VOUT[30]=%d V_MODEL[27]=%d",
                             accum_buf[8], accum_buf[11], accum_buf[12], accum_buf[13], 
                             accum_buf[14], accum_buf[15], accum_buf[24], accum_buf[30], accum_buf[27]);
                
                // Salva buffer raw para diagnóstico
                size_t copy_len = accum_len < UPS_RAW_BUF_SIZE ? accum_len : UPS_RAW_BUF_SIZE;
                platform_mutex_lock(&g_ups_mutex);
                memcpy(g_ups_raw_buf, accum_buf, copy_len);
                g_ups_raw_len = copy_len;
                
                // Processa payload - parse status first for op_battery flag
                ups_status_t status;
                parse_status_flags(accum_buf, &status);
                ups_metrics_t metrics = build_payload(accum_buf, g_config.iout_calib, status.op_battery);
                
                bool on_battery_now = status.op_battery ||
                    (metrics.voltage_in < 50.0f && metrics.power_out > 10.0f);
                if (on_battery_now && !g_on_battery)
                    g_battery_since_ms=platform_monotonic_ms();
                g_on_battery = on_battery_now;
                g_ups_metrics = metrics;
                g_ups_status = status;
                g_ups_connected = true;
                platform_mutex_unlock(&g_ups_mutex);
                
                // Fallback: if input voltage is very low (< 50V) but outputting power, assume on battery

                
                LOG_TAG_DEBUG("UPS", "Received packet: V_in=%.1fV V_out=%.1fV I_out=%.2fA Bat=%.1f%% Runtime=%.0fmin OnBatt=%d",
                             metrics.voltage_in, metrics.voltage_out, metrics.current_out, metrics.battery_state, metrics.battery_runtime, 
                             (metrics.battery_runtime > 0.0f) ? 1 : 0);
                
                break;  // Got complete packet, exit read loop
            }
        }
        
        if (accum_len < UPS_MIN_PACKET_SIZE) {
            LOG_TAG_DEBUG("UPS", "Incomplete packet after %d reads (%zu bytes), retrying...", read_attempts, accum_len);
            consecutive_errors++;
            if (consecutive_errors >= 3) {
                platform_mutex_lock(&g_ups_mutex);
                g_ups_connected = false;
                g_on_battery = false;
                platform_mutex_unlock(&g_ups_mutex);
                LOG_TAG_WARN("UPS", "Too many incomplete packets (%d), attempting reconnect...", consecutive_errors);
                ups_reconnect_serial();
                consecutive_errors = 0;
            }
            ups_wait_ms(100);
            continue;
        }
        
        // Aguarda próximo ciclo de polling
        ups_wait_ms((unsigned)g_config.poll_interval_ms);
    }
    
    LOG_TAG_INFO_S("UPS", "Poll thread stopped");
    return NULL;
}

int ups_init(const config_t *cfg) {
    memcpy(&g_config, cfg, sizeof(config_t));
    int path_length=snprintf(g_energy_file,sizeof(g_energy_file),"%s/energy.dat",cfg->state_directory);
    if (path_length<0 || (size_t)path_length>=sizeof(g_energy_file)) return -1;
    
    // Abre porta serial
    g_serial_fd = platform_serial_open(cfg);
    if (!platform_serial_valid(g_serial_fd)) {
        LOG_ERROR("Failed to open serial port %s: %s", cfg->serial_port, platform_error());
        return -1;
    }
    
    LOG_INFO("Serial port configured: %d baud, %d%c%d", cfg->baud_rate, cfg->data_bits, cfg->parity, cfg->stop_bits);

    // Envia comando de inicialização (cmd_req1)
    if (ups_send_cmd(g_serial_fd, cmd_req1, sizeof(cmd_req1)) < 0) {
        platform_serial_close(g_serial_fd);
        g_serial_fd = PLATFORM_SERIAL_INVALID;
        return -1;
    }
    
    platform_sleep_ms(100);
    
    // Read and log response to init command (some UPS devices require this)
    uint8_t init_rsp[64];
    int n = ups_read_response(g_serial_fd, init_rsp, sizeof(init_rsp), cfg->read_timeout_ms);
    if (n > 0) {
        char hexbuf[192];
        char *p = hexbuf;
        for (int i = 0; i < n && i < 60; i++) {
            p += snprintf(p, sizeof(hexbuf) - (p - hexbuf), "%02X ", init_rsp[i]);
        }
        LOG_TAG_DEBUG("UPS", "Init response (%d bytes): %s", n, hexbuf);
    } else if (n == 0) {
        LOG_TAG_DEBUG_S("UPS", "No response to init command (timeout)");
    }
    
    ups_read_calibration(g_serial_fd);

    // Limpa buffers
    platform_serial_flush(g_serial_fd);
    
    LOG_INFO("UPS initialized on %s at %d baud", cfg->serial_port, cfg->baud_rate);
    return 0;
}

int ups_start(void) {
    if (platform_flag_get(&g_ups_running)) return 0;
    
    platform_flag_set(&g_ups_running, true);
    int ret = platform_thread_start(&g_ups_thread, ups_poll_thread, NULL);
    if (ret != 0) {
        LOG_ERROR("Failed to create UPS thread: %s", platform_error());
        platform_flag_set(&g_ups_running, false);
        return -1;
    }
    
    g_thread_started = true;
    // Don't detach - we'll join in cleanup
    return 0;
}

void ups_stop(void) {
    platform_flag_set(&g_ups_running, false);
    // Thread will exit on next iteration
}

void ups_cleanup(void) {
    ups_stop();
    
    // Wait for thread to finish (join)
    if (g_thread_started) {
        platform_thread_join(g_ups_thread);
        g_thread_started = false;
    }
    
    if (platform_serial_valid(g_serial_fd)) {
        platform_serial_close(g_serial_fd);
        g_serial_fd = PLATFORM_SERIAL_INVALID;
    }
    
    platform_mutex_lock(&g_ups_mutex);
    g_ups_connected = false;
    g_on_battery = false;
    platform_mutex_unlock(&g_ups_mutex);
    
    LOG_INFO_S("UPS cleaned up");
}

void ups_parse_status_flags(const uint8_t *buf, ups_status_t *s) {
    parse_status_flags(buf, s);
}

void ups_get_metrics(ups_metrics_t *metrics) {
    platform_mutex_lock(&g_ups_mutex);
    *metrics = g_ups_metrics;
    platform_mutex_unlock(&g_ups_mutex);
}

void ups_get_status(ups_status_t *status) {
    platform_mutex_lock(&g_ups_mutex);
    *status = g_ups_status;
    platform_mutex_unlock(&g_ups_mutex);
}

bool ups_is_connected(void) {
    bool connected;
    platform_mutex_lock(&g_ups_mutex);
    connected = g_ups_connected;
    platform_mutex_unlock(&g_ups_mutex);
    return connected;
}
void ups_get_snapshot(ups_snapshot_t *snapshot) {
    platform_mutex_lock(&g_ups_mutex);
    snapshot->metrics = g_ups_metrics;
    snapshot->status = g_ups_status;
    snapshot->connected = g_ups_connected;
    memcpy(snapshot->model, g_model, sizeof(g_model));
    snapshot->raw_len = g_ups_raw_len;
    memcpy(snapshot->raw, g_ups_raw_buf, sizeof(snapshot->raw));
    snapshot->seconds_on_battery = 0;
    if (g_ups_connected && g_on_battery) {
        uint64_t seconds=(platform_monotonic_ms()-g_battery_since_ms)/1000;
        snapshot->seconds_on_battery=seconds>2147483647 ? 2147483647 : (unsigned)seconds;
    }
    platform_mutex_unlock(&g_ups_mutex);
}
