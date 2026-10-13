#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define DP(v) ((int)((float)(v) * ui->target.dp_scale))

void ui_init(UIController* ui, float dp_scale) {
    if (!ui) return;
    memset(ui, 0, sizeof(UIController));
    ui->target.dp_scale = dp_scale > 0.5f ? dp_scale : 2.0f;
    ui->active_page = PAGE_HOME;
    ui->page_scroll_x = 0.0f;
    ui->target_page_scroll = 0.0f;
    ui->safe_top_dp = 36;
    ui->safe_bot_dp = 24;

    sys_cpu_init(&ui->cpu);
    sys_mem_init(&ui->mem);
    sys_net_init(&ui->net);
    sys_batt_init(&ui->batt);
    bench_engine_init(&ui->engine, 38, NULL);
    bench_state_init(&ui->bench);

    ui->anim_cpu_percent = 25.0f;
    ui->anim_ram_percent = 45.0f;
    ui->anim_batt_percent = 85.0f;
    ui->anim_health_score = 92.0f;
}

void ui_set_framebuffer(UIController* ui, uint32_t* pixels, int width, int height, int stride) {
    if (!ui) return;
    ui->target.pixels = pixels;
    ui->target.width = width;
    ui->target.height = height;
    ui->target.stride = stride;

    float calc_scale = (float)width / 360.0f;
    if (calc_scale < 1.0f) calc_scale = 1.0f;
    if (fabsf(ui->target.dp_scale - calc_scale) > 0.1f) {
        ui->target.dp_scale = calc_scale;
        text_cleanup();
        text_init(ui->target.dp_scale);
    }
}

void ui_update_telemetry(UIController* ui, const CpuTelemetry* cpu, const MemTelemetry* mem, const NetTelemetry* net, const BattTelemetry* batt) {
    if (!ui || ui->is_telemetry_paused) return;
    if (cpu) ui->cpu = *cpu;
    if (mem) ui->mem = *mem;
    if (net) ui->net = *net;
    if (batt) ui->batt = *batt;
}

// Touch Handling
void ui_on_touch_down(UIController* ui, float x, float y) {
    if (!ui) return;
    ui->is_swiping = true;
    ui->touch_start_x = x;
    ui->touch_start_y = y;
    ui->touch_last_x = x;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ui->touch_start_time = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    ui->last_user_interaction = ui->touch_start_time;
}

void ui_on_touch_move(UIController* ui, float x, float y) {
    if (!ui || !ui->is_swiping) return;
    float dx = x - ui->touch_last_x;
    ui->touch_last_x = x;

    // Direct swipe drag
    float page_w = (float)ui->target.width;
    if (page_w > 0.0f) {
        ui->page_scroll_x -= dx / page_w;
        if (ui->page_scroll_x < 0.0f) ui->page_scroll_x = 0.0f;
        if (ui->page_scroll_x > (float)(PAGE_COUNT - 1)) ui->page_scroll_x = (float)(PAGE_COUNT - 1);
    }
}

void ui_on_touch_up(UIController* ui, float x, float y) {
    if (!ui) return;
    ui->is_swiping = false;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    float total_dx = x - ui->touch_start_x;
    float total_dy = y - ui->touch_start_y;
    double duration = now - ui->touch_start_time;

    // Check tap (minimal movement & fast)
    if (fabsf(total_dx) < (float)DP(15) && fabsf(total_dy) < (float)DP(15)) {
        if (duration > 0.6) {
            // Long press: toggle pause
            ui->is_telemetry_paused = !ui->is_telemetry_paused;
            return;
        }

        // Tap on Hero Clock area (y in DP(40)..DP(170)): detect double tap to cycle theme
        if (ui->active_page == PAGE_HOME && y >= (float)DP(40) && y <= (float)DP(180)) {
            if (now - ui->last_clock_tap_time < 0.35) {
                theme_cycle();
                ui->last_clock_tap_time = 0.0;
                return;
            }
            ui->last_clock_tap_time = now;
        }

        // Tap on Benchmark button (in Motor C page)
        if (ui->active_page == PAGE_MOTOR_C && y >= (float)DP(370) && y <= (float)DP(420)) {
            bench_trigger(&ui->bench);
            return;
        }

        // Tap on Theme Button in Top Bar
        if (y <= (float)DP(44) && x >= (float)(ui->target.width - DP(90))) {
            theme_cycle();
            return;
        }
    }

    // Swipe page snap
    if (total_dx < -DP(40) && ui->active_page < PAGE_COUNT - 1) {
        ui->active_page++;
    } else if (total_dx > DP(40) && ui->active_page > 0) {
        ui->active_page--;
    } else {
        // Snap to nearest page
        ui->active_page = (AppNavPage)((int)(ui->page_scroll_x + 0.5f));
        if (ui->active_page >= PAGE_COUNT) ui->active_page = PAGE_COUNT - 1;
    }
    ui->target_page_scroll = (float)ui->active_page;
}

// Top Bar Renderer
static void render_top_bar(UIController* ui, const Theme* th) {
    int pad = DP(16);
    int top_y = DP(ui->safe_top_dp);

    // App Title
    render_icon_engine(&ui->target, pad + DP(6), top_y + DP(10), DP(14), th->accent);
    text_draw(&ui->target, "MONITOR NATIVO", pad + DP(20), top_y + DP(2), FONT_MD, th->text_primary);

    // Subtitle / Page indicator
    static const char* PAGE_NAMES[PAGE_COUNT] = {
        "INICIO", "RED & ENLACE", "SISTEMA", "MOTOR C"
    };
    char sub_str[48];
    if (ui->is_telemetry_paused) {
        snprintf(sub_str, sizeof(sub_str), "PAUSADO  |  %s", th->name);
        text_draw(&ui->target, sub_str, pad + DP(20), top_y + DP(18), FONT_XS, th->warning);
    } else {
        snprintf(sub_str, sizeof(sub_str), "%s  |  %s", PAGE_NAMES[ui->active_page], th->name);
        text_draw(&ui->target, sub_str, pad + DP(20), top_y + DP(18), FONT_XS, th->text_muted);
    }

    // Theme Pill (Top Right)
    int btn_w = DP(76);
    int btn_h = DP(24);
    int btn_x = ui->target.width - pad - btn_w;
    int btn_y = top_y + DP(4);
    render_glass_card(&ui->target, btn_x, btn_y, btn_w, btn_h, DP(12), th->card_bg_top, th->card_bg_bot, th->card_border, 0);
    text_draw_centered(&ui->target, "TEMA", btn_x + btn_w / 2, btn_y + DP(5), FONT_XS, th->text_secondary);
}

// Bottom Page Indicator (4 Dots)
static void render_bottom_navigation(UIController* ui, const Theme* th) {
    int cy = ui->target.height - DP(16);
    int cx = ui->target.width / 2;
    int spacing = DP(14);
    int total_w = (PAGE_COUNT - 1) * spacing + DP(18);
    int start_x = cx - total_w / 2;

    for (int i = 0; i < PAGE_COUNT; i++) {
        float dist = fabsf(ui->page_scroll_x - (float)i);
        if (dist > 1.0f) dist = 1.0f;
        float active_weight = 1.0f - dist;

        int dot_w = DP(6) + (int)(active_weight * (float)DP(12));
        int dot_h = DP(6);
        int dot_x = start_x + i * spacing;
        uint32_t col = color_blend(th->card_border, th->accent, active_weight);

        render_rounded_rect(&ui->target, dot_x, cy - dot_h / 2, dot_w, dot_h, dot_h / 2, col, 0, 0);
    }
}

// PAGE 0: INICIO (HOME)
static void render_page_home(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(ui->safe_top_dp + 36);
    int gap = DP(10);

    // 1. Reloj Principal Minimalista (Hero Clock)
    time_t raw_time;
    time(&raw_time);
    struct tm* tm_info = localtime(&raw_time);

    char time_hm[16], time_s[8], date_str[64];
    snprintf(time_hm, sizeof(time_hm), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    snprintf(time_s, sizeof(time_s), ":%02d", tm_info->tm_sec);

    static const char* DAYS_ES[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};
    static const char* MONTHS_ES[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
    snprintf(date_str, sizeof(date_str), "%s, %02d de %s",
             DAYS_ES[tm_info->tm_wday], tm_info->tm_mday, MONTHS_ES[tm_info->tm_mon]);

    int clock_h = DP(116);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, clock_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "RELOJ PRINCIPAL", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int hm_w = text_width(time_hm, FONT_HERO);
    int s_w = text_width(time_s, FONT_LG);
    int clock_start_x = page_x + pad + (w - (hm_w + s_w + DP(2))) / 2;
    int clock_y = cur_y + DP(26);

    text_draw_tabular(&ui->target, time_hm, clock_start_x, clock_y, FONT_HERO, th->text_primary);
    text_draw_tabular(&ui->target, time_s, clock_start_x + hm_w + DP(2), clock_y + DP(14), FONT_LG, th->text_secondary);
    text_draw_centered(&ui->target, date_str, page_x + pad + w / 2, cur_y + DP(78), FONT_MD, th->text_primary);

    cur_y += clock_h + gap;

    // 2. Tres Anillos de Telemetría (CPU, RAM, BATERÍA)
    int rings_h = DP(128);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, rings_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "ESTADO GENERAL EN VIVO", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int ring_col_w = w / 3;
    int ring_r = DP(30);
    int ring_thick = DP(5);
    int ring_cy = cur_y + DP(62);

    // Ring 1: CPU
    int c1_x = page_x + pad + ring_col_w / 2;
    uint32_t cpu_col = (ui->anim_cpu_percent < 60.0f) ? th->success : (ui->anim_cpu_percent < 85.0f) ? th->warning : th->danger;
    render_ring(&ui->target, c1_x, ring_cy, ring_r, ring_thick, ui->anim_cpu_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, cpu_col, th->surface_inset);
    render_icon_cpu(&ui->target, c1_x, ring_cy - DP(8), DP(10), th->text_muted);
    char cpu_txt[16];
    snprintf(cpu_txt, sizeof(cpu_txt), "%d%%", (int)ui->anim_cpu_percent);
    text_draw_centered(&ui->target, cpu_txt, c1_x, ring_cy + DP(2), FONT_SM, th->text_primary);
    text_draw_centered(&ui->target, "CPU", c1_x, ring_cy + ring_r + DP(8), FONT_XS, th->text_secondary);

    // Ring 2: RAM
    int c2_x = page_x + pad + ring_col_w + ring_col_w / 2;
    uint32_t ram_col = (ui->anim_ram_percent < 75.0f) ? th->accent : th->warning;
    render_ring(&ui->target, c2_x, ring_cy, ring_r, ring_thick, ui->anim_ram_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, ram_col, th->surface_inset);
    render_icon_ram(&ui->target, c2_x, ring_cy - DP(8), DP(12), th->text_muted);
    char ram_txt[16];
    snprintf(ram_txt, sizeof(ram_txt), "%d%%", (int)ui->anim_ram_percent);
    text_draw_centered(&ui->target, ram_txt, c2_x, ring_cy + DP(2), FONT_SM, th->text_primary);
    text_draw_centered(&ui->target, "RAM", c2_x, ring_cy + ring_r + DP(8), FONT_XS, th->text_secondary);

    // Ring 3: BATERÍA
    int c3_x = page_x + pad + 2 * ring_col_w + ring_col_w / 2;
    uint32_t batt_col = (ui->anim_batt_percent > 20.0f) ? th->success : th->danger;
    render_ring(&ui->target, c3_x, ring_cy, ring_r, ring_thick, ui->anim_batt_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, batt_col, th->surface_inset);
    render_icon_battery(&ui->target, c3_x, ring_cy - DP(8), DP(12), (int)ui->anim_batt_percent, ui->batt.is_charging, th->text_muted);
    char batt_txt[16];
    snprintf(batt_txt, sizeof(batt_txt), "%d%%%s", (int)ui->anim_batt_percent, ui->batt.is_charging ? "+" : "");
    text_draw_centered(&ui->target, batt_txt, c3_x, ring_cy + DP(2), FONT_SM, th->text_primary);
    text_draw_centered(&ui->target, "BATERIA", c3_x, ring_cy + ring_r + DP(8), FONT_XS, th->text_secondary);

    cur_y += rings_h + gap;

    // 3. Puntuación de Salud del Dispositivo (Health Score)
    int health_h = DP(90);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, health_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "SALUD GLOBAL DEL DISPOSITIVO", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    // Health Ring on Left
    int hr_cx = page_x + pad + DP(42);
    int hr_cy = cur_y + DP(50);
    uint32_t health_col = (ui->anim_health_score >= 80.0f) ? th->success : (ui->anim_health_score >= 60.0f) ? th->warning : th->danger;
    render_ring(&ui->target, hr_cx, hr_cy, DP(24), DP(4), ui->anim_health_score / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, health_col, th->surface_inset);
    char score_str[16];
    snprintf(score_str, sizeof(score_str), "%d", (int)ui->anim_health_score);
    text_draw_centered(&ui->target, score_str, hr_cx, hr_cy - DP(8), FONT_MD, th->text_primary);

    // Health Details on Right
    int hd_x = page_x + pad + DP(80);
    int hd_y = cur_y + DP(34);
    const char* status_badge = (ui->anim_health_score >= 80.0f) ? "RENDIMIENTO OPTIMO" : (ui->anim_health_score >= 60.0f) ? "ESTADO REGULAR" : "ATENCION REQUERIDA";
    text_draw(&ui->target, status_badge, hd_x, hd_y, FONT_SM, health_col);

    char health_sub[64];
    snprintf(health_sub, sizeof(health_sub), "CPU: %.0f%%  |  RAM: %.1fG  |  Temp: %.0f°C",
             ui->anim_cpu_percent, ui->mem.used_mb / 1024.0f, ui->cpu.temp_cpu_c);
    text_draw(&ui->target, health_sub, hd_x, hd_y + DP(18), FONT_XS, th->text_secondary);

    cur_y += health_h + gap;

    // 4. Mini Sparkline Historial (CPU / RAM)
    int spark_card_h = DP(90);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, spark_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "HISTORIAL CONTINUO (CPU & RAM)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    render_dual_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(30), w - DP(28), DP(46),
                          ui->cpu.history, ui->mem.history, CPU_HISTORY_LEN, ui->cpu.history_idx, 100.0f,
                          th->success, th->accent);
}

// PAGE 1: RED (NETWORK)
static void render_page_network(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(ui->safe_top_dp + 36);
    int gap = DP(10);

    // 1. Velocímetro y Enlace Wi-Fi
    int speed_card_h = DP(130);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, speed_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "ENLACE WI-FI & SENAL", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    // Speedometer Arc on Left
    int s_cx = page_x + pad + DP(50);
    int s_cy = cur_y + DP(68);
    float max_speed = 1000.0f;
    float cur_speed = (float)ui->net.link_speed_mbps;
    float ratio = cur_speed / max_speed;
    if (ratio > 1.0f) ratio = 1.0f;

    render_ring(&ui->target, s_cx, s_cy, DP(36), DP(5), ratio, PI_F * 0.75f, PI_F * 1.5f, th->accent, th->surface_inset);
    char spd_txt[16];
    snprintf(spd_txt, sizeof(spd_txt), "%d", ui->net.link_speed_mbps);
    text_draw_centered(&ui->target, spd_txt, s_cx, s_cy - DP(16), FONT_XL, th->text_primary);
    text_draw_centered(&ui->target, "Mbps", s_cx, s_cy + DP(8), FONT_XS, th->text_muted);

    // Wi-Fi Metadata on Right
    int meta_x = s_cx + DP(48);
    int meta_y = cur_y + DP(30);

    render_circle(&ui->target, meta_x + DP(3), meta_y + DP(6), DP(3), ui->net.is_wifi_connected ? th->success : th->danger);
    text_draw(&ui->target, ui->net.ssid, meta_x + DP(10), meta_y, FONT_SM, th->text_primary);

    meta_y += DP(22);
    render_icon_wifi(&ui->target, meta_x + DP(6), meta_y + DP(4), DP(12), ui->net.signal_level, th->accent, th->surface_inset);
    char rssi_buf[32];
    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm (%s)", ui->net.rssi_dbm, ui->net.standard_str);
    text_draw(&ui->target, rssi_buf, meta_x + DP(20), meta_y, FONT_XS, th->text_secondary);

    meta_y += DP(20);
    char ip_meta[48];
    snprintf(ip_meta, sizeof(ip_meta), "IP: %s  |  GW: %s", ui->net.local_ip, ui->net.gateway_ip);
    text_draw(&ui->target, ip_meta, meta_x, meta_y, FONT_XS, th->text_muted);

    cur_y += speed_card_h + gap;

    // 2. Gráfica de Tráfico en Tiempo Real (60s)
    int traf_card_h = DP(120);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, traf_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "FLUJO EN TIEMPO REAL (60s)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    // Live Numbers
    int rx_x = page_x + pad + DP(14);
    int tx_x = page_x + pad + w / 2;
    char rx_str[32], tx_str[32];
    if (ui->anim_rx_rate_kbps > 1024.0f) snprintf(rx_str, sizeof(rx_str), "%.2f MB/s", ui->anim_rx_rate_kbps / 1024.0f);
    else snprintf(rx_str, sizeof(rx_str), "%.1f KB/s", ui->anim_rx_rate_kbps);

    if (ui->anim_tx_rate_kbps > 1024.0f) snprintf(tx_str, sizeof(tx_str), "%.2f MB/s", ui->anim_tx_rate_kbps / 1024.0f);
    else snprintf(tx_str, sizeof(tx_str), "%.1f KB/s", ui->anim_tx_rate_kbps);

    render_icon_arrow_down(&ui->target, rx_x + DP(4), cur_y + DP(32), DP(10), th->success);
    text_draw(&ui->target, rx_str, rx_x + DP(14), cur_y + DP(24), FONT_MD, th->success);

    render_icon_arrow_up(&ui->target, tx_x + DP(4), cur_y + DP(32), DP(10), th->accent);
    text_draw(&ui->target, tx_str, tx_x + DP(14), cur_y + DP(24), FONT_MD, th->accent);

    // Sparkline Graph
    float peak = ui->net.rx_peak_kbps > ui->net.tx_peak_kbps ? ui->net.rx_peak_kbps : ui->net.tx_peak_kbps;
    if (peak < 100.0f) peak = 100.0f;
    render_dual_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(52), w - DP(28), DP(42),
                          ui->net.rx_history, ui->net.tx_history, NET_HISTORY_LEN, ui->net.history_idx, peak,
                          th->success, th->accent);

    cur_y += traf_card_h + gap;

    // 3. Latencia y Calidad a 3 Destinos
    int ping_card_h = DP(126);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, ping_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "LATENCIA, JITTER & PERDIDA", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int row_y = cur_y + DP(28);
    for (int i = 0; i < NUM_PING_TARGETS; i++) {
        const PingTarget* pt = &ui->net.ping_targets[i];
        uint32_t q_col = (pt->quality == NET_QUALITY_GOOD) ? th->success : (pt->quality == NET_QUALITY_FAIR) ? th->warning : th->danger;

        render_circle(&ui->target, page_x + pad + DP(20), row_y + DP(8), DP(3), q_col);
        text_draw(&ui->target, pt->name, page_x + pad + DP(28), row_y, FONT_SM, th->text_primary);

        char stats_buf[64];
        if (pt->last_ping_ms >= 0) {
            snprintf(stats_buf, sizeof(stats_buf), "%d ms  ·  ±%.1f ms  ·  %.0f%%", pt->last_ping_ms, pt->jitter_ms, pt->packet_loss);
        } else {
            snprintf(stats_buf, sizeof(stats_buf), "-- ms  ·  ±-- ms  ·  --");
        }
        text_draw_right(&ui->target, stats_buf, page_x + pad + w - DP(14), row_y, FONT_SM, q_col);

        row_y += DP(28);
    }
}

// PAGE 2: SISTEMA (SYSTEM)
static void render_page_system(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(ui->safe_top_dp + 36);
    int gap = DP(10);

    // 1. Núcleos de CPU (Multi-Core Bars)
    int cores_card_h = DP(126);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, cores_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    
    char cpu_head[48];
    snprintf(cpu_head, sizeof(cpu_head), "CPU (%d NUCLEOS)  |  LOAD: %.2f", ui->cpu.core_count, ui->cpu.load_1m);
    text_draw(&ui->target, cpu_head, page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int num_cores = ui->cpu.core_count > 8 ? 8 : ui->cpu.core_count;
    if (num_cores <= 0) num_cores = 4;
    int bar_w = (w - DP(28) - (num_cores - 1) * DP(6)) / num_cores;
    int bar_max_h = DP(60);
    int bar_base_y = cur_y + DP(96);

    for (int i = 0; i < num_cores; i++) {
        float u = ui->cpu.cores[i].usage_percent;
        int h = (int)((u / 100.0f) * (float)bar_max_h);
        if (h < 2) h = 2;
        int bx = page_x + pad + DP(14) + i * (bar_w + DP(6));
        int by = bar_base_y - h;

        uint32_t col = (u < 50.0f) ? th->success : (u < 80.0f) ? th->warning : th->danger;
        render_rounded_rect(&ui->target, bx, bar_base_y - bar_max_h, bar_w, bar_max_h, DP(2), th->surface_inset, 0, 0);
        render_rounded_rect(&ui->target, bx, by, bar_w, h, DP(2), col, 0, 0);

        char c_num[8];
        snprintf(c_num, sizeof(c_num), "c%d", i);
        text_draw_centered(&ui->target, c_num, bx + bar_w / 2, bar_base_y + DP(3), FONT_XS, th->text_muted);
    }

    cur_y += cores_card_h + gap;

    // 2. Temperaturas del Sistema
    int temp_card_h = DP(74);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, temp_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "TEMPERATURAS TERMICAS (/sys/class/thermal)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int col_w = w / 3;
    int t_y = cur_y + DP(30);

    // CPU Temp
    render_icon_temp(&ui->target, page_x + pad + DP(20), t_y + DP(12), DP(14), th->accent);
    char cpu_t_str[16];
    snprintf(cpu_t_str, sizeof(cpu_t_str), "%.1f°C", ui->cpu.temp_cpu_c);
    text_draw(&ui->target, cpu_t_str, page_x + pad + DP(32), t_y + DP(4), FONT_MD, ui->cpu.temp_warning ? th->danger : th->text_primary);
    text_draw(&ui->target, "CPU", page_x + pad + DP(32), t_y + DP(22), FONT_XS, th->text_muted);

    // Battery Temp
    int b_x = page_x + pad + col_w;
    render_icon_temp(&ui->target, b_x + DP(10), t_y + DP(12), DP(14), th->success);
    char bat_t_str[16];
    snprintf(bat_t_str, sizeof(bat_t_str), "%.1f°C", ui->batt.temp_c);
    text_draw(&ui->target, bat_t_str, b_x + DP(22), t_y + DP(4), FONT_MD, th->text_primary);
    text_draw(&ui->target, "BATERIA", b_x + DP(22), t_y + DP(22), FONT_XS, th->text_muted);

    // Storage/GPU Temp
    int g_x = page_x + pad + 2 * col_w;
    render_icon_temp(&ui->target, g_x + DP(10), t_y + DP(12), DP(14), th->warning);
    char gpu_t_str[16];
    snprintf(gpu_t_str, sizeof(gpu_t_str), "%.1f°C", ui->cpu.temp_gpu_c);
    text_draw(&ui->target, gpu_t_str, g_x + DP(22), t_y + DP(4), FONT_MD, th->text_primary);
    text_draw(&ui->target, "GPU", g_x + DP(22), t_y + DP(22), FONT_XS, th->text_muted);

    cur_y += temp_card_h + gap;

    // 3. Almacenamiento & Memoria
    int stor_card_h = DP(84);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, stor_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "ALMACENAMIENTO (/data statvfs)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    char stor_stats[64];
    snprintf(stor_stats, sizeof(stor_stats), "%.1f GB / %.1f GB (%.0f%%)", ui->mem.storage_used_gb, ui->mem.storage_total_gb, ui->mem.storage_percent);
    text_draw_right(&ui->target, stor_stats, page_x + pad + w - DP(14), cur_y + DP(10), FONT_XS, th->text_secondary);

    // Segmented bar
    int seg_bar_w = w - DP(28);
    int seg_filled = (int)((ui->mem.storage_percent / 100.0f) * 12.0f);
    render_segmented_bar(&ui->target, page_x + pad + DP(14), cur_y + DP(32), seg_bar_w, DP(12), 12, seg_filled, th->accent, th->surface_inset, DP(3));

    char free_str[48];
    snprintf(free_str, sizeof(free_str), "Espacio libre disponible: %.1f GB", ui->mem.storage_free_gb);
    text_draw(&ui->target, free_str, page_x + pad + DP(14), cur_y + DP(56), FONT_XS, th->text_muted);

    cur_y += stor_card_h + gap;

    // 4. Batería Detallada
    int batt_card_h = DP(90);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, batt_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "BATERIA DETALLADA (/sys/class/power_supply)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int b_row1 = cur_y + DP(30);
    char volt_str[32], pow_str[32];
    snprintf(volt_str, sizeof(volt_str), "%.2f V  |  %.0f mA", ui->batt.voltage_v, ui->batt.current_ma);
    snprintf(pow_str, sizeof(pow_str), "%.2f W  ·  %s", ui->batt.power_w, ui->batt.status_str);
    text_draw(&ui->target, volt_str, page_x + pad + DP(14), b_row1, FONT_SM, th->text_primary);
    text_draw_right(&ui->target, pow_str, page_x + pad + w - DP(14), b_row1, FONT_SM, th->accent);

    int b_row2 = b_row1 + DP(24);
    char rem_str[64];
    snprintf(rem_str, sizeof(rem_str), "Tiempo estimado restante: ~%dh %02dm", ui->batt.time_remaining_min / 60, ui->batt.time_remaining_min % 60);
    text_draw(&ui->target, rem_str, page_x + pad + DP(14), b_row2, FONT_XS, th->text_muted);
}

// PAGE 3: MOTOR C (C ENGINE EFFICIENCY PROOF)
static void render_page_motor_c(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(ui->safe_top_dp + 36);
    int gap = DP(10);

    // 1. Insignia Principal de Eficiencia C
    int proof_card_h = DP(120);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, proof_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "EFICIENCIA EN TIEMPO REAL (ESTE PROCESO)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int c_col_w = w / 3;
    int num_y = cur_y + DP(30);

    // App CPU
    char app_cpu_txt[16];
    snprintf(app_cpu_txt, sizeof(app_cpu_txt), "%.1f%%", ui->engine.app_cpu_percent);
    text_draw_centered(&ui->target, app_cpu_txt, page_x + pad + c_col_w / 2, num_y, FONT_LG, th->success);
    text_draw_centered(&ui->target, "CPU APP", page_x + pad + c_col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    // App RAM
    char app_ram_txt[16];
    snprintf(app_ram_txt, sizeof(app_ram_txt), "%.1fM", ui->engine.app_ram_rss_mb);
    text_draw_centered(&ui->target, app_ram_txt, page_x + pad + c_col_w + c_col_w / 2, num_y, FONT_LG, th->accent);
    text_draw_centered(&ui->target, "RAM RSS", page_x + pad + c_col_w + c_col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    // Allocs / Frame
    text_draw_centered(&ui->target, "0", page_x + pad + 2 * c_col_w + c_col_w / 2, num_y, FONT_LG, th->text_primary);
    text_draw_centered(&ui->target, "ALLOCS/FRAME", page_x + pad + 2 * c_col_w + c_col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    // Golden Proof Badge
    char badge_str[64];
    snprintf(badge_str, sizeof(badge_str), "%.1f MB RAM  ·  %.1f%% CPU  ·  0 ALLOCS/FRAME", ui->engine.app_ram_rss_mb, ui->engine.app_cpu_percent);
    render_glass_card(&ui->target, page_x + pad + DP(14), cur_y + DP(80), w - DP(28), DP(26), DP(6), th->surface_inset, th->surface_inset, th->accent, 0);
    text_draw_centered(&ui->target, badge_str, page_x + pad + w / 2, cur_y + DP(85), FONT_XS, th->accent_glow);

    cur_y += proof_card_h + gap;

    // 2. Telemetría de Frame-Time & VSync
    int frame_card_h = DP(90);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, frame_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "TIEMPO DE FRAME & FPS REALES", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    char frame_stats[64];
    snprintf(frame_stats, sizeof(frame_stats), "%.0f µs  ·  %.1f FPS  ·  p99: %.0f µs", ui->engine.frame_time_us, ui->engine.fps, ui->engine.frame_p99_us);
    text_draw(&ui->target, frame_stats, page_x + pad + DP(14), cur_y + DP(28), FONT_SM, th->text_primary);

    // Mini sparkline for frame times
    render_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(48), w - DP(28), DP(30),
                     ui->engine.frame_history, FRAME_HISTORY_LEN, ui->engine.frame_history_idx, 0.0f, 2000.0f,
                     th->accent, color_alpha(th->accent, 30));

    cur_y += frame_card_h + gap;

    // 3. Huella del Binario C
    int bin_card_h = DP(68);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, bin_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "HUELLA BINARIA & ARRANQUE EN FRIO", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    char bin_txt[96];
    snprintf(bin_txt, sizeof(bin_txt), ".so: %d KB  ·  APK: %d KB  ·  Boot: %d ms  ·  Hilos: %d",
             ui->engine.binary_so_kb, ui->engine.apk_kb, ui->engine.cold_boot_ms, ui->engine.app_threads);
    text_draw(&ui->target, bin_txt, page_x + pad + DP(14), cur_y + DP(30), FONT_SM, th->text_secondary);

    cur_y += bin_card_h + gap;

    // 4. Botón y Resultados de BENCHMARK Interactivo (2s)
    int bench_card_h = DP(128);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, bench_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "BENCHMARK INTERACTIVO DE HARDWARE (2s)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    if (ui->bench.is_running) {
        // Progress bar
        int pb_w = w - DP(28);
        render_rounded_rect(&ui->target, page_x + pad + DP(14), cur_y + DP(32), pb_w, DP(18), DP(6), th->surface_inset, 0, 0);
        int fill = (int)(ui->bench.progress * (float)pb_w);
        render_rounded_rect(&ui->target, page_x + pad + DP(14), cur_y + DP(32), fill, DP(18), DP(6), th->accent, 0, 0);
        text_draw_centered(&ui->target, "EJECUTANDO PRUEBAS EN VIVO...", page_x + pad + w / 2, cur_y + DP(35), FONT_XS, th->text_primary);
    } else {
        // Interactive Launch Button
        int btn_w = w - DP(28);
        int btn_h = DP(28);
        int bx = page_x + pad + DP(14);
        int by = cur_y + DP(28);
        render_glass_card(&ui->target, bx, by, btn_w, btn_h, DP(8), th->card_bg_top, th->card_bg_bot, th->accent, 0);
        text_draw_centered(&ui->target, "EJECUTAR BENCHMARK (2s)", bx + btn_w / 2, by + DP(6), FONT_SM, th->accent_glow);
    }

    // Benchmark Metric Bars
    int m_y = cur_y + DP(64);
    char b1[64], b2[64];
    snprintf(b1, sizeof(b1), "Pixel Fill: %.0f MPix/s  |  Mem: %.1f GB/s", ui->bench.pixel_fill_mpix_s, ui->bench.memcpy_gb_s);
    snprintf(b2, sizeof(b2), "Primes: %.1f M/s  |  NEON SIMD: %.1f GFLOPS", ui->bench.primes_mprimes_s, ui->bench.neon_gflops);

    text_draw(&ui->target, b1, page_x + pad + DP(14), m_y, FONT_XS, th->text_primary);
    text_draw(&ui->target, b2, page_x + pad + DP(14), m_y + DP(18), FONT_XS, th->text_primary);
}

void ui_render_frame(UIController* ui, float dt) {
    if (!ui || !ui->target.pixels || ui->target.width <= 0 || ui->target.height <= 0) return;

    const Theme* th = theme_get_current();

    // 1. Smooth Page Transition Interpolation (Spring Easing)
    if (!ui->is_swiping) {
        float diff = ui->target_page_scroll - ui->page_scroll_x;
        ui->page_scroll_x += diff * (dt * 12.0f);
        if (fabsf(diff) < 0.005f) ui->page_scroll_x = ui->target_page_scroll;
    }

    // 2. Smooth Rolling Easing for Metrics
    float ease = dt * 6.0f;
    ui->anim_cpu_percent += (ui->cpu.total_usage - ui->anim_cpu_percent) * ease;
    ui->anim_ram_percent += (ui->mem.ram_percent - ui->anim_ram_percent) * ease;
    ui->anim_batt_percent += ((float)ui->batt.level_percent - ui->anim_batt_percent) * ease;
    ui->anim_rx_rate_kbps += (ui->net.rx_rate_kbps - ui->anim_rx_rate_kbps) * ease;
    ui->anim_tx_rate_kbps += (ui->net.tx_rate_kbps - ui->anim_tx_rate_kbps) * ease;

    // Calculate Device Health Score
    float raw_health = 100.0f - (ui->anim_cpu_percent * 0.2f + (ui->anim_ram_percent > 60.0f ? (ui->anim_ram_percent - 60.0f) * 0.3f : 0.0f) + (ui->cpu.temp_cpu_c > 45.0f ? (ui->cpu.temp_cpu_c - 45.0f) * 0.8f : 0.0f));
    if (raw_health < 10.0f) raw_health = 10.0f;
    if (raw_health > 100.0f) raw_health = 100.0f;
    ui->anim_health_score += (raw_health - ui->anim_health_score) * ease;

    ui->anim_phase += dt * 2.0f;
    if (ui->anim_phase > 2.0f * PI_F) ui->anim_phase -= 2.0f * PI_F;

    // Step live benchmark if running
    bench_update_step(&ui->bench);

    // 3. Clear Background Canvas
    render_clear(&ui->target, th->bg);

    // 4. Render Pages with Horizontal Offset
    int screen_w = ui->target.width;

    // Determine which pages are visible during transition
    int min_p = (int)floorf(ui->page_scroll_x);
    int max_p = (int)ceilf(ui->page_scroll_x);
    if (min_p < 0) min_p = 0;
    if (max_p >= PAGE_COUNT) max_p = PAGE_COUNT - 1;

    for (int p = min_p; p <= max_p; p++) {
        int page_x = (int)(((float)p - ui->page_scroll_x) * (float)screen_w);

        switch (p) {
            case PAGE_HOME:
                render_page_home(ui, th, page_x);
                break;
            case PAGE_NETWORK:
                render_page_network(ui, th, page_x);
                break;
            case PAGE_SYSTEM:
                render_page_system(ui, th, page_x);
                break;
            case PAGE_MOTOR_C:
                render_page_motor_c(ui, th, page_x);
                break;
        }
    }

    // 5. Render Fixed Top Bar & Bottom Navigation
    render_top_bar(ui, th);
    render_bottom_navigation(ui, th);
}
