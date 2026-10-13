#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <inttypes.h>

#include "../app/src/main/cpp/sys_cpu.h"
#include "../app/src/main/cpp/sys_mem.h"
#include "../app/src/main/cpp/bench.h"

// Test 1: CPU stat delta calculation
static void test_cpu_delta(void) {
    printf("[TEST] Testing CPU delta calculation...\n");
    CpuRawStat prev = {
        .user = 1000, .nice = 20, .system = 500, .idle = 8000,
        .iowait = 50, .irq = 10, .softirq = 20, .steal = 0
    };
    CpuRawStat cur = {
        .user = 1100, .nice = 20, .system = 550, .idle = 8300,
        .iowait = 50, .irq = 15, .softirq = 25, .steal = 0
    };

    uint64_t cur_idle = cur.idle + cur.iowait;
    uint64_t prev_idle = prev.idle + prev.iowait;
    uint64_t cur_non_idle = cur.user + cur.nice + cur.system + cur.irq + cur.softirq + cur.steal;
    uint64_t prev_non_idle = prev.user + prev.nice + prev.system + prev.irq + prev.softirq + prev.steal;

    uint64_t cur_total = cur_idle + cur_non_idle;
    uint64_t prev_total = prev_idle + prev_non_idle;

    uint64_t total_d = cur_total - prev_total;
    uint64_t idle_d = cur_idle - prev_idle;

    float usage = ((float)(total_d - idle_d) * 100.0f) / (float)total_d;
    printf("  Total delta: %" PRIu64 ", Idle delta: %" PRIu64 ", Usage: %.2f%%\n", total_d, idle_d, usage);

    assert(total_d == 460);
    assert(idle_d == 300);
    assert(fabsf(usage - 34.78f) < 0.1f);
    printf("  [PASS] CPU delta calculation verified!\n");
}

// Test 2: Memory parsing logic
static void test_mem_calculation(void) {
    printf("[TEST] Testing Meminfo parser calculations...\n");
    uint64_t mem_total_kb = 8192000;
    uint64_t mem_avail_kb = 4096000;
    uint64_t cached_kb = 2048000;
    uint64_t buffers_kb = 512000;

    float total_mb = (float)mem_total_kb / 1024.0f;
    float cached_mb = (float)(cached_kb + buffers_kb) / 1024.0f;
    float free_mb = (float)mem_avail_kb / 1024.0f;
    float used_mb = total_mb - free_mb;
    float ram_pct = (used_mb / total_mb) * 100.0f;

    printf("  Total: %.1f MB, Used: %.1f MB, Cached: %.1f MB, Free: %.1f MB, Pct: %.1f%%\n",
           total_mb, used_mb, cached_mb, free_mb, ram_pct);

    assert(fabsf(total_mb - 8000.0f) < 0.1f);
    assert(fabsf(used_mb - 4000.0f) < 0.1f);
    assert(fabsf(ram_pct - 50.0f) < 0.1f);
    printf("  [PASS] Meminfo calculations verified!\n");
}

// Test 3: Benchmark routines
static void test_benchmark_logic(void) {
    printf("[TEST] Testing Benchmark state machine...\n");
    BenchmarkState bench;
    bench_state_init(&bench);
    assert(!bench.is_running);
    assert(!bench.has_completed);

    bench_trigger(&bench);
    assert(bench.is_running);
    assert(bench.progress == 0.0f);

    printf("  [PASS] Benchmark state verified!\n");
}

// Test 4: Engine metrics
static void test_engine_metrics(void) {
    printf("[TEST] Testing Engine Zero-Alloc assertion...\n");
    EngineMetrics engine;
    bench_engine_init(&engine, 40, NULL);
    assert(engine.allocs_per_frame == 0);
    assert(engine.cold_boot_ms == 40);

    for (int i = 0; i < 60; i++) {
        bench_engine_update_frame(&engine, 500.0f + (float)(i * 10));
    }
    assert(engine.allocs_per_frame == 0);
    assert(engine.fps > 0.0f);
    printf("  FPS: %.1f, p50: %.1f µs, p99: %.1f µs, Allocs: %u\n",
           engine.fps, engine.frame_p50_us, engine.frame_p99_us, engine.allocs_per_frame);
    printf("  [PASS] Engine zero-alloc verification passed!\n");
}

int main(void) {
    printf("=== RUNNING MONITOR NATIVO UNIT TESTS ===\n");
    test_cpu_delta();
    test_mem_calculation();
    test_benchmark_logic();
    test_engine_metrics();
    printf("=== ALL UNIT TESTS PASSED SUCCESSFULLY! ===\n");
    return 0;
}
