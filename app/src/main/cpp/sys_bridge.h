#ifndef SYS_BRIDGE_H
#define SYS_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "sys_cpu.h"
#include "sys_batt.h"

#define MAX_TOP_PROCS 6
#define MAX_ACTIVE_CONNS 6

typedef struct {
    int pid;
    float cpu_pct;
    float rss_mb;
    char name[48];
} TopProcEntry;

typedef struct {
    int local_port;
    char remote_addr[32];
    char state[12];
    char service[16];
} ActiveConnEntry;

typedef struct {
    bool is_active;
    bool is_root;
    bool use_24h;
    bool time_fmt_checked;
    float anim_scale;

    char device_model[48];
    char android_ver[16];
    char soc_name[32];
    char cpu_governor[24];

    // Root Diagnostics for Settings Zone
    char root_bin_path[48];
    char root_diag_msg[96];
    int root_check_count;

    TopProcEntry procs[MAX_TOP_PROCS];
    int proc_count;

    ActiveConnEntry conns[MAX_ACTIVE_CONNS];
    int conn_count;

    char last_action_msg[96];
    double last_action_time;
} BridgeTelemetry;

void sys_bridge_init(BridgeTelemetry* br);
void sys_bridge_update(BridgeTelemetry* br, CpuTelemetry* cpu, BattTelemetry* batt, void* jni_env_ptr, void* jni_context_ptr);
bool sys_bridge_send_cmd(BridgeTelemetry* br, const char* cmd, void* jni_env_ptr, void* jni_context_ptr);
bool sys_bridge_recheck_root(BridgeTelemetry* br);
void sys_vibrate(void* jni_env_ptr, void* jni_context_ptr, int duration_ms);

#endif // SYS_BRIDGE_H
