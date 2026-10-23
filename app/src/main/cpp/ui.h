#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>
#include "text.h"
#include "render.h"
#include "theme.h"
#include "sys_cpu.h"
#include "sys_mem.h"
#include "sys_net.h"
#include "sys_batt.h"
#include "sys_bridge.h"
#include "bench.h"

typedef enum {
    PAGE_HOME     = 0,
    PAGE_NETWORK  = 1,
    PAGE_SYSTEM   = 2,
    PAGE_MOTOR_C  = 3,
    PAGE_SETTINGS = 4,
    PAGE_COUNT    = 5
} AppNavPage;

typedef struct {
    RenderTarget target;
    
    // Navigation & Page Swipe
    AppNavPage active_page;
    float page_scroll_x;     // Animated fractional page offset (0.0 to 4.0)
    float target_page_scroll;
    bool is_swiping;
    float touch_start_x;
    float touch_start_y;
    float touch_last_x;
    double touch_start_time;

    // Gesture, Clock & Immersion States
    bool is_telemetry_paused;
    bool use_24h_clock;
    int ascii_clock_mode;    // 0 = '#' ASCII Matrix, 1 = '█' Block, 2 = '1/0' Binary
    int art_offset;          // 0..4 user photos, 5 = minimalist no-photo mode
    bool card_expanded[8];
    double last_clock_tap_time;

    // Animated Smooth Rolling Metrics (Easing)
    float anim_cpu_percent;
    float anim_ram_percent;
    float anim_batt_percent;
    float anim_health_score;
    float anim_rx_rate_kbps;
    float anim_tx_rate_kbps;
    float anim_phase;

    // Pending async command for Root/Shizuku worker thread
    char pending_cmd[96];
    bool has_pending_cmd;
    int pending_vibrate_ms;

    // Telemetry Data Snapshots
    CpuTelemetry cpu;
    MemTelemetry mem;
    NetTelemetry net;
    BattTelemetry batt;
    BridgeTelemetry bridge;
    EngineMetrics engine;
    BenchmarkState bench;

    // Display & Power Stability
    int safe_top_dp;
    int safe_bot_dp;
    bool power_save_mode;
    double last_user_interaction;
} UIController;

void ui_init(UIController* ui, float dp_scale);
void ui_set_framebuffer(UIController* ui, uint32_t* pixels, int width, int height, int stride);
void ui_update_telemetry(UIController* ui, const CpuTelemetry* cpu, const MemTelemetry* mem, const NetTelemetry* net, const BattTelemetry* batt, const BridgeTelemetry* bridge);
void ui_render_frame(UIController* ui, float dt);

// Touch inputs
void ui_on_touch_down(UIController* ui, float x, float y);
void ui_on_touch_move(UIController* ui, float x, float y);
void ui_on_touch_up(UIController* ui, float x, float y);

#endif // UI_H
