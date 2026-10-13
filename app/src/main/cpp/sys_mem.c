#include "sys_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <inttypes.h>

void sys_mem_init(MemTelemetry* mem) {
    if (!mem) return;
    memset(mem, 0, sizeof(MemTelemetry));
    mem->total_mb = 4096.0f;
    mem->used_mb = 2048.0f;
    mem->cached_mb = 1024.0f;
    mem->free_mb = 1024.0f;
    mem->ram_percent = 50.0f;
    mem->storage_total_gb = 64.0f;
    mem->storage_used_gb = 32.0f;
    mem->storage_free_gb = 32.0f;
    mem->storage_percent = 50.0f;
}

static void read_meminfo(MemTelemetry* mem) {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return;

    char line[128];
    uint64_t mem_total_kb = 0;
    uint64_t mem_free_kb = 0;
    uint64_t mem_avail_kb = 0;
    uint64_t buffers_kb = 0;
    uint64_t cached_kb = 0;
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%" SCNu64, &mem_total_kb);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line + 8, "%" SCNu64, &mem_free_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%" SCNu64, &mem_avail_kb);
        } else if (strncmp(line, "Buffers:", 8) == 0) {
            sscanf(line + 8, "%" SCNu64, &buffers_kb);
        } else if (strncmp(line, "Cached:", 7) == 0) {
            sscanf(line + 7, "%" SCNu64, &cached_kb);
        } else if (strncmp(line, "SwapTotal:", 10) == 0) {
            sscanf(line + 10, "%" SCNu64, &swap_total_kb);
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            sscanf(line + 9, "%" SCNu64, &swap_free_kb);
        }
    }
    fclose(f);

    if (mem_total_kb > 0) {
        mem->total_mb = (float)mem_total_kb / 1024.0f;
        mem->cached_mb = (float)(cached_kb + buffers_kb) / 1024.0f;
        
        if (mem_avail_kb > 0) {
            mem->free_mb = (float)mem_avail_kb / 1024.0f;
            mem->used_mb = mem->total_mb - mem->free_mb;
        } else {
            mem->free_mb = (float)mem_free_kb / 1024.0f;
            mem->used_mb = mem->total_mb - mem->free_mb - mem->cached_mb;
        }

        if (mem->used_mb < 0.0f) mem->used_mb = 0.0f;
        mem->ram_percent = (mem->used_mb / mem->total_mb) * 100.0f;
        if (mem->ram_percent > 100.0f) mem->ram_percent = 100.0f;
    }

    if (swap_total_kb > 0) {
        mem->swap_total_mb = (float)swap_total_kb / 1024.0f;
        mem->swap_used_mb = (float)(swap_total_kb - swap_free_kb) / 1024.0f;
        mem->swap_percent = (mem->swap_used_mb / mem->swap_total_mb) * 100.0f;
    } else {
        mem->swap_total_mb = 0.0f;
        mem->swap_used_mb = 0.0f;
        mem->swap_percent = 0.0f;
    }
}

static void read_storage(MemTelemetry* mem) {
    struct statvfs st;
    if (statvfs("/data", &st) == 0 && st.f_blocks > 0) {
        uint64_t block_size = st.f_frsize;
        uint64_t total_bytes = st.f_blocks * block_size;
        uint64_t free_bytes = st.f_bavail * block_size;
        uint64_t used_bytes = total_bytes > free_bytes ? (total_bytes - free_bytes) : 0;

        double gb = 1024.0 * 1024.0 * 1024.0;
        mem->storage_total_gb = (float)((double)total_bytes / gb);
        mem->storage_used_gb = (float)((double)used_bytes / gb);
        mem->storage_free_gb = (float)((double)free_bytes / gb);

        if (mem->storage_total_gb > 0.0f) {
            mem->storage_percent = (mem->storage_used_gb / mem->storage_total_gb) * 100.0f;
            if (mem->storage_percent > 100.0f) mem->storage_percent = 100.0f;
        }
    }
}

void sys_mem_update(MemTelemetry* mem) {
    if (!mem) return;

    read_meminfo(mem);
    read_storage(mem);

    // Record into rolling sparkline history
    mem->history[mem->history_idx] = mem->ram_percent;
    mem->history_idx = (mem->history_idx + 1) % MEM_HISTORY_LEN;
}
