#ifndef SYS_BATT_H
#define SYS_BATT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int level_percent;     // 0 to 100%
    bool is_charging;
    bool is_full;
    char status_str[24];   // "Cargando", "Descargando", "Completo"

    float voltage_v;       // e.g. 3.95 V
    float current_ma;      // e.g. -518 mA or +1450 mA
    float power_w;         // e.g. 2.04 W
    float temp_c;          // e.g. 33.9 C

    int cycle_count;       // e.g. 1
    int charge_mah;        // e.g. 1554 mAh
    int full_mah;          // e.g. 1873 mAh
    bool has_bridge_batt;
    
    int time_remaining_min; // Estimated minutes remaining (-1 if unknown)
    bool is_low_warning;   // true if level < 20%
} BattTelemetry;

void sys_batt_init(BattTelemetry* batt);
void sys_batt_update(BattTelemetry* batt, void* jni_env_ptr, void* jni_context_ptr);

#endif // SYS_BATT_H
