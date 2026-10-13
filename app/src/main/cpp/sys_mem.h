#ifndef SYS_MEM_H
#define SYS_MEM_H

#include <stdint.h>
#include <stdbool.h>

#define MEM_HISTORY_LEN 60

typedef struct {
    // RAM Metrics (in MB & GB)
    float total_mb;
    float used_mb;
    float cached_mb;
    float free_mb;
    float ram_percent;      // 0.0 to 100.0%
    
    // Swap Metrics
    float swap_total_mb;
    float swap_used_mb;
    float swap_percent;

    // Storage Metrics (/data filesystem via statvfs)
    float storage_total_gb;
    float storage_used_gb;
    float storage_free_gb;
    float storage_percent;

    // Rolling History
    float history[MEM_HISTORY_LEN];
    int history_idx;
} MemTelemetry;

void sys_mem_init(MemTelemetry* mem);
void sys_mem_update(MemTelemetry* mem);

#endif // SYS_MEM_H
