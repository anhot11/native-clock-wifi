#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>
#include <stdbool.h>

#define FRAME_HISTORY_LEN 60

typedef struct {
    // Process Self Telemetry
    float app_cpu_percent;
    float app_ram_rss_mb;
    float app_ram_anon_mb;      // Private C heap/arena memory (excluding Zygote shared libs)
    int app_threads;
    uint32_t allocs_per_frame;  // Verified 0
    
    // Frame Timing
    float frame_time_us;
    float fps;
    float frame_p50_us;
    float frame_p99_us;
    float frame_history[FRAME_HISTORY_LEN];
    int frame_history_idx;

    // Binary footprint
    int binary_so_kb;
    int apk_kb;
    int cold_boot_ms;
} EngineMetrics;

typedef struct {
    bool is_running;
    bool has_completed;
    float progress;         // 0.0 to 1.0f
    double start_time;

    // Measured Results
    float pixel_fill_mpix_s; // Megapixels / sec
    float memcpy_gb_s;       // Memory bandwidth GB/s
    float primes_mprimes_s;  // Millions primes / sec
    float neon_gflops;       // Vector math GFLOPS / MOps
    int total_score;         // Composite hardware benchmark score

    // Reference Baselines (Mid-tier ARM SoC)
    float ref_pixel_mpix_s;
    float ref_memcpy_gb_s;
    float ref_primes_mprimes_s;
    float ref_neon_gflops;
} BenchmarkState;

void bench_engine_init(EngineMetrics* engine, int cold_boot_ms, const char* apk_path);
void bench_engine_update_frame(EngineMetrics* engine, float frame_time_us);
void bench_engine_sample_proc(EngineMetrics* engine);

void bench_state_init(BenchmarkState* bench);
void bench_trigger(BenchmarkState* bench);
void bench_update_step(BenchmarkState* bench);

#endif // BENCH_H
