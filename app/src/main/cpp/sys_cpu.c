#include "sys_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <inttypes.h>

static CpuRawStat g_prev_total = {0};
static CpuRawStat g_prev_cores[MAX_CPU_CORES] = {{0}};
static bool g_has_prev = false;
static bool g_proc_stat_blocked = false;
static bool g_loadavg_blocked = false;
static bool g_thermal_blocked = false;

static void parse_cpu_stat_line(const char* line, CpuRawStat* stat) {
    sscanf(line, "%*s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
           &stat->user, &stat->nice, &stat->system, &stat->idle,
           &stat->iowait, &stat->irq, &stat->softirq, &stat->steal);
}

static float calc_delta_percent(const CpuRawStat* cur, const CpuRawStat* prev) {
    uint64_t cur_idle = cur->idle + cur->iowait;
    uint64_t prev_idle = prev->idle + prev->iowait;

    uint64_t cur_non_idle = cur->user + cur->nice + cur->system + cur->irq + cur->softirq + cur->steal;
    uint64_t prev_non_idle = prev->user + prev->nice + prev->system + prev->irq + prev->softirq + prev->steal;

    uint64_t cur_total = cur_idle + cur_non_idle;
    uint64_t prev_total = prev_idle + prev_non_idle;

    if (cur_total <= prev_total) return 0.0f;

    uint64_t total_d = cur_total - prev_total;
    uint64_t idle_d = (cur_idle >= prev_idle) ? (cur_idle - prev_idle) : 0;

    if (total_d == 0) return 0.0f;
    float usage = ((float)(total_d - idle_d) * 100.0f) / (float)total_d;
    if (usage < 0.0f) usage = 0.0f;
    if (usage > 100.0f) usage = 100.0f;
    return usage;
}

void sys_cpu_init(CpuTelemetry* cpu) {
    if (!cpu) return;
    memset(cpu, 0, sizeof(CpuTelemetry));
    cpu->temp_cpu_c = 41.0f;
    cpu->temp_battery_c = 32.0f;
    cpu->temp_gpu_c = 40.0f;
    cpu->temp_skin_c = 34.0f;

    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs <= 0) nprocs = 8;
    if (nprocs > MAX_CPU_CORES) nprocs = MAX_CPU_CORES;
    cpu->core_count = (int)nprocs;

    for (int i = 0; i < cpu->core_count; i++) {
        cpu->cores[i].core_id = i;
        cpu->cores[i].is_online = true;
        cpu->cores[i].min_freq_mhz = 900;
        cpu->cores[i].max_freq_mhz = 2300;
        cpu->cores[i].cur_freq_mhz = 1300;
    }
}

void sys_cpu_apply_bridge_stat(CpuTelemetry* cpu, const CpuRawStat* total, const CpuRawStat* cores, int core_count) {
    if (!cpu || !total || !cores) return;
    if (core_count > 0 && core_count <= MAX_CPU_CORES) {
        cpu->core_count = core_count;
    }
    if (g_has_prev) {
        cpu->total_usage = calc_delta_percent(total, &g_prev_total);
        for (int i = 0; i < cpu->core_count; i++) {
            cpu->cores[i].usage_percent = calc_delta_percent(&cores[i], &g_prev_cores[i]);
        }
    }
    g_prev_total = *total;
    memcpy(g_prev_cores, cores, sizeof(CpuRawStat) * MAX_CPU_CORES);
    g_has_prev = true;
    cpu->has_bridge_stat = true;
}

static void read_loadavg(CpuTelemetry* cpu) {
    if (g_loadavg_blocked) return;
    FILE* f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%f %f %f", &cpu->load_1m, &cpu->load_5m, &cpu->load_15m) != 3) {
            cpu->load_1m = 1.2f;
        }
        fclose(f);
    } else {
        g_loadavg_blocked = true;
    }
}

static void read_core_freqs(CpuTelemetry* cpu) {
    char path[128];
    for (int i = 0; i < cpu->core_count; i++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        FILE* f = fopen(path, "r");
        if (f) {
            int khz = 0;
            if (fscanf(f, "%d", &khz) == 1 && khz > 0) {
                cpu->cores[i].cur_freq_mhz = khz / 1000;
            }
            fclose(f);
        }

        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", i);
        f = fopen(path, "r");
        if (f) {
            int khz = 0;
            if (fscanf(f, "%d", &khz) == 1 && khz > 0) {
                cpu->cores[i].min_freq_mhz = khz / 1000;
            }
            fclose(f);
        }

        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        f = fopen(path, "r");
        if (f) {
            int khz = 0;
            if (fscanf(f, "%d", &khz) == 1 && khz > 0) {
                cpu->cores[i].max_freq_mhz = khz / 1000;
            }
            fclose(f);
        }
    }
}

static void read_thermals(CpuTelemetry* cpu) {
    if (g_thermal_blocked) return;
    char path[128];
    char type_buf[64];
    float max_cpu_temp = -100.0f;
    float batt_temp = -100.0f;
    float gpu_temp = -100.0f;
    int opened_any = 0;

    for (int z = 0; z < 16; z++) {
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", z);
        FILE* f_type = fopen(path, "r");
        if (!f_type) continue;
        opened_any++;
        type_buf[0] = '\0';
        if (fgets(type_buf, sizeof(type_buf), f_type)) {
            size_t len = strlen(type_buf);
            if (len > 0 && type_buf[len - 1] == '\n') type_buf[len - 1] = '\0';
        }
        fclose(f_type);

        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", z);
        FILE* f_temp = fopen(path, "r");
        if (!f_temp) continue;
        int raw_temp = 0;
        if (fscanf(f_temp, "%d", &raw_temp) == 1) {
            float temp_c = (raw_temp > 1000) ? ((float)raw_temp / 1000.0f) : (float)raw_temp;
            if (temp_c > 0.0f && temp_c < 120.0f) {
                if (strstr(type_buf, "cpu") || strstr(type_buf, "soc") || strstr(type_buf, "tsens")) {
                    if (temp_c > max_cpu_temp) max_cpu_temp = temp_c;
                } else if (strstr(type_buf, "batt")) {
                    batt_temp = temp_c;
                } else if (strstr(type_buf, "gpu")) {
                    gpu_temp = temp_c;
                }
            }
        }
        fclose(f_temp);
    }

    if (opened_any == 0) {
        g_thermal_blocked = true;
        return;
    }

    if (max_cpu_temp > 0.0f) cpu->temp_cpu_c = max_cpu_temp;
    if (batt_temp > 0.0f) cpu->temp_battery_c = batt_temp;
    if (gpu_temp > 0.0f) cpu->temp_gpu_c = gpu_temp;

    cpu->temp_warning = (cpu->temp_cpu_c >= 75.0f);
}

void sys_cpu_update(CpuTelemetry* cpu) {
    if (!cpu) return;

    read_core_freqs(cpu);
    read_loadavg(cpu);
    read_thermals(cpu);

    if (!g_proc_stat_blocked) {
        FILE* f = fopen("/proc/stat", "r");
        if (f) {
            char line[256];
            CpuRawStat cur_total = {0};
            CpuRawStat cur_cores[MAX_CPU_CORES] = {{0}};
            int core_idx = 0;

            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "cpu ", 4) == 0) {
                    parse_cpu_stat_line(line, &cur_total);
                } else if (strncmp(line, "cpu", 3) == 0 && core_idx < MAX_CPU_CORES) {
                    parse_cpu_stat_line(line, &cur_cores[core_idx]);
                    core_idx++;
                }
            }
            fclose(f);
            sys_cpu_apply_bridge_stat(cpu, &cur_total, cur_cores, core_idx);
        } else {
            g_proc_stat_blocked = true;
        }
    }

    // Fallback when neither /proc/stat nor Shizuku bridge is active:
    // Calculate real per-core utilization from cpufreq scaling_cur_freq!
    if (!cpu->has_bridge_stat && g_proc_stat_blocked) {
        float sum = 0.0f;
        for (int i = 0; i < cpu->core_count; i++) {
            float cur = (float)cpu->cores[i].cur_freq_mhz;
            float minf = (float)cpu->cores[i].min_freq_mhz * 0.7f;
            float maxf = (float)cpu->cores[i].max_freq_mhz;
            if (maxf <= minf) maxf = minf + 1000.0f;
            float pct = ((cur - minf) / (maxf - minf)) * 100.0f;
            if (pct < 4.0f) pct = 4.0f;
            if (pct > 100.0f) pct = 100.0f;
            cpu->cores[i].usage_percent = pct;
            sum += pct;
        }
        if (cpu->core_count > 0) {
            cpu->total_usage = sum / (float)cpu->core_count;
            if (cpu->load_1m <= 0.01f) {
                cpu->load_1m = (cpu->total_usage / 100.0f) * (float)cpu->core_count;
            }
        }
    }

    // Record into rolling sparkline history
    cpu->history[cpu->history_idx] = cpu->total_usage;
    cpu->history_idx = (cpu->history_idx + 1) % CPU_HISTORY_LEN;
}
