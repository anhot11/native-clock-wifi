#ifndef SYS_CPU_H
#define SYS_CPU_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_CPU_CORES 16
#define CPU_HISTORY_LEN 60

typedef struct {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
} CpuRawStat;

typedef struct {
    int core_id;
    float usage_percent;   // 0.0 to 100.0%
    int cur_freq_mhz;      // e.g. 2301 MHz
    int min_freq_mhz;      // e.g. 900 MHz
    int max_freq_mhz;      // e.g. 2301 MHz
    bool is_online;
} CpuCoreInfo;

typedef struct {
    float total_usage;     // 0.0 to 100.0%
    float load_1m;
    float load_5m;
    float load_15m;
    
    int core_count;
    CpuCoreInfo cores[MAX_CPU_CORES];

    // Thermals
    float temp_cpu_c;      // e.g. 42.5 C
    float temp_battery_c;  // e.g. 33.0 C
    float temp_gpu_c;      // e.g. 41.0 C
    float temp_skin_c;     // e.g. 34.0 C
    bool temp_warning;     // true if temp > 75 C
    bool has_bridge_stat;  // true if fed by Shizuku/ADB bridge

    // Rolling History
    float history[CPU_HISTORY_LEN];
    int history_idx;
} CpuTelemetry;

void sys_cpu_init(CpuTelemetry* cpu);
void sys_cpu_update(CpuTelemetry* cpu);
void sys_cpu_apply_bridge_stat(CpuTelemetry* cpu, const CpuRawStat* total, const CpuRawStat* cores, int core_count);

#endif // SYS_CPU_H
