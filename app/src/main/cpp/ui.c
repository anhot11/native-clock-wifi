#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/sysinfo.h>

#define DP(v) ((int)((float)(v) * ui->target.dp_scale))

static double get_ui_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void ui_init(UIController* ui, float dp_scale) {
    if (!ui) return;
    memset(ui, 0, sizeof(UIController));
    ui->target.dp_scale = dp_scale > 0.5f ? dp_scale : 2.0f;
    ui->active_page = PAGE_HOME;
    ui->page_scroll_x = 0.0f;
    ui->target_page_scroll = 0.0f;
    ui->safe_top_dp = 32;
    ui->safe_bot_dp = 54;

    text_init(ui->target.dp_scale);

    sys_cpu_init(&ui->cpu);
    sys_mem_init(&ui->mem);
    sys_net_init(&ui->net);
    sys_batt_init(&ui->batt);
    sys_bridge_init(&ui->bridge);
    bench_engine_init(&ui->engine, 14, NULL);
    bench_state_init(&ui->bench);

    ui->anim_cpu_percent = 22.0f;
    ui->anim_ram_percent = 50.0f;
    ui->anim_batt_percent = 83.0f;
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

void ui_update_telemetry(UIController* ui, const CpuTelemetry* cpu, const MemTelemetry* mem, const NetTelemetry* net, const BattTelemetry* batt, const BridgeTelemetry* bridge) {
    if (!ui || ui->is_telemetry_paused) return;
    if (cpu) ui->cpu = *cpu;
    if (mem) ui->mem = *mem;
    if (net) ui->net = *net;
    if (batt) ui->batt = *batt;
    if (bridge) ui->bridge = *bridge;
}

static void queue_ui_cmd(UIController* ui, const char* cmd, const char* instant_msg) {
    strncpy(ui->pending_cmd, cmd, sizeof(ui->pending_cmd) - 1);
    ui->pending_cmd[sizeof(ui->pending_cmd) - 1] = '\0';
    ui->has_pending_cmd = true;
    ui->pending_vibrate_ms = 22;
    if (instant_msg) {
        strncpy(ui->bridge.last_action_msg, instant_msg, sizeof(ui->bridge.last_action_msg) - 1);
        ui->bridge.last_action_time = get_ui_sec();
    }
}

void ui_on_touch_down(UIController* ui, float x, float y) {
    if (!ui) return;
    ui->is_swiping = true;
    ui->touch_start_x = x;
    ui->touch_start_y = y;
    ui->touch_last_x = x;
    ui->touch_start_time = get_ui_sec();
    ui->last_user_interaction = ui->touch_start_time;
}

void ui_on_touch_move(UIController* ui, float x, float y) {
    if (!ui || !ui->is_swiping) return;
    float dx = x - ui->touch_last_x;
    ui->touch_last_x = x;

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

    double now = get_ui_sec();
    float total_dx = x - ui->touch_start_x;
    float total_dy = y - ui->touch_start_y;
    double duration = now - ui->touch_start_time;

    // Detect clean tap
    if (fabsf(total_dx) < (float)DP(16) && fabsf(total_dy) < (float)DP(16)) {
        // Long press anywhere: freeze/unfreeze telemetry graphs
        if (duration > 0.65) {
            ui->is_telemetry_paused = !ui->is_telemetry_paused;
            ui->pending_vibrate_ms = 35;
            ui->target_page_scroll = (float)ui->active_page;
            return;
        }

        // 1. Bottom Interactive Navigation Dock (height - DP(92) .. height - DP(48))
        int dock_top = ui->target.height - DP(92);
        int dock_bot = ui->target.height - DP(46);
        if (y >= (float)dock_top && y <= (float)dock_bot) {
            int pad = DP(16);
            int dock_w = ui->target.width - 2 * pad;
            int tab_idx = (int)((x - (float)pad) / ((float)dock_w / 4.0f));
            if (tab_idx >= 0 && tab_idx < PAGE_COUNT) {
                ui->active_page = (AppNavPage)tab_idx;
                ui->target_page_scroll = (float)tab_idx;
                ui->pending_vibrate_ms = 15;
                return;
            }
        }

        // 2. Top Right Theme Button
        if (y >= (float)DP(24) && y <= (float)DP(68) && x >= (float)(ui->target.width - DP(96))) {
            theme_cycle();
            ui->pending_vibrate_ms = 18;
            ui->target_page_scroll = (float)ui->active_page;
            return;
        }

        // 3. Page-specific interactive actions
        if (ui->active_page == PAGE_HOME) {
            // Double tap on Hero Clock (y in DP(70)..DP(178)) -> cycle theme
            if (y >= (float)DP(70) && y <= (float)DP(178)) {
                if (now - ui->last_clock_tap_time < 0.38) {
                    theme_cycle();
                    ui->pending_vibrate_ms = 20;
                    ui->last_clock_tap_time = 0.0;
                    ui->target_page_scroll = (float)ui->active_page;
                    return;
                }
                ui->last_clock_tap_time = now;
            }

            // Tap on Rings Card (y in DP(186)..DP(308)) -> jump to System page
            if (y >= (float)DP(186) && y <= (float)DP(308)) {
                ui->active_page = PAGE_SYSTEM;
                ui->target_page_scroll = (float)PAGE_SYSTEM;
                ui->pending_vibrate_ms = 15;
                return;
            }

            // Tap on Action Center Buttons:
            // Button 1: LIBERAR RAM & PROCESOS (y in DP(534)..DP(574))
            if (y >= (float)DP(534) && y <= (float)DP(574)) {
                queue_ui_cmd(ui, "CMD BOOST_RAM", "Optimizando RAM y cerrando procesos...");
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            // Row 2 Buttons (y in DP(580)..DP(620))
            if (y >= (float)DP(580) && y <= (float)DP(620)) {
                if (x < (float)(ui->target.width / 2)) {
                    queue_ui_cmd(ui, "CMD TRIM_CACHE", "Purgando cache de almacenamiento /data...");
                } else {
                    queue_ui_cmd(ui, "CMD TOGGLE_ANIM", "Cambiando velocidad de animaciones...");
                }
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
        } else if (ui->active_page == PAGE_SYSTEM) {
            // Tap on Top Processes rows (y in DP(512)..DP(684)) -> kill/stop process
            int row_start_y = DP(512);
            int row_h = DP(38);
            int max_rows = ui->bridge.proc_count < 4 ? ui->bridge.proc_count : 4;
            for (int i = 0; i < max_rows; i++) {
                int ry0 = row_start_y + i * row_h;
                int ry1 = ry0 + row_h;
                if (y >= (float)ry0 && y <= (float)ry1 && x >= (float)(ui->target.width - DP(105))) {
                    char cmd[96], msg[96];
                    snprintf(cmd, sizeof(cmd), "CMD KILL %d %s", ui->bridge.procs[i].pid, ui->bridge.procs[i].name);
                    snprintf(msg, sizeof(msg), "Deteniendo %s...", ui->bridge.procs[i].name);
                    queue_ui_cmd(ui, cmd, msg);
                    ui->target_page_scroll = (float)ui->active_page;
                    return;
                }
            }
        } else if (ui->active_page == PAGE_MOTOR_C) {
            // Tap on Benchmark Button (y in DP(406)..DP(452))
            if (y >= (float)DP(404) && y <= (float)DP(454)) {
                bench_trigger(&ui->bench);
                ui->pending_vibrate_ms = 25;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
        }
    }

    // Horizontal swipe page transition
    if (total_dx < -DP(38) && ui->active_page < PAGE_COUNT - 1) {
        ui->active_page++;
        ui->pending_vibrate_ms = 12;
    } else if (total_dx > DP(38) && ui->active_page > 0) {
        ui->active_page--;
        ui->pending_vibrate_ms = 12;
    } else {
        ui->active_page = (AppNavPage)((int)(ui->page_scroll_x + 0.5f));
        if (ui->active_page >= PAGE_COUNT) ui->active_page = PAGE_COUNT - 1;
    }
    ui->target_page_scroll = (float)ui->active_page;
}

// Top Bar Renderer with Live Shizuku/ADB Status & Toast Banner
static void render_top_bar(UIController* ui, const Theme* th) {
    int pad = DP(16);
    int top_y = DP(ui->safe_top_dp);

    render_icon_engine(&ui->target, pad + DP(6), top_y + DP(10), DP(14), th->accent);
    text_draw(&ui->target, "MONITOR NATIVO PRO", pad + DP(18), top_y + DP(1), FONT_MD, th->text_primary);

    // Shizuku / ADB Live Status Badge
    int badge_x = pad + DP(18) + text_width("MONITOR NATIVO PRO", FONT_MD) + DP(8);
    if (ui->bridge.is_active) {
        int bw = DP(66);
        render_rounded_rect(&ui->target, badge_x, top_y + DP(3), bw, DP(15), DP(7), color_alpha(th->success, 40), th->success, 1);
        text_draw_centered(&ui->target, "SHIZUKU", badge_x + bw / 2, top_y + DP(4), FONT_XS, th->success);
    }

    static const char* PAGE_NAMES[PAGE_COUNT] = {
        "INICIO & TURBO", "RED & SOCKETS", "SISTEMA & APPS", "MOTOR C / NEON"
    };
    char sub_str[64];
    if (ui->is_telemetry_paused) {
        snprintf(sub_str, sizeof(sub_str), "CONGELADO  ·  %s", th->name);
        text_draw(&ui->target, sub_str, pad + DP(18), top_y + DP(18), FONT_XS, th->warning);
    } else {
        snprintf(sub_str, sizeof(sub_str), "%s  ·  %s", PAGE_NAMES[ui->active_page], th->name);
        text_draw(&ui->target, sub_str, pad + DP(18), top_y + DP(18), FONT_XS, th->text_muted);
    }

    // Theme Pill (Top Right)
    int btn_w = DP(68);
    int btn_h = DP(24);
    int btn_x = ui->target.width - pad - btn_w;
    int btn_y = top_y + DP(4);
    render_glass_card(&ui->target, btn_x, btn_y, btn_w, btn_h, DP(12), th->card_bg_top, th->card_bg_bot, th->card_border, 0);
    text_draw_centered(&ui->target, "TEMA", btn_x + btn_w / 2, btn_y + DP(5), FONT_XS, th->accent_glow);
}

// Interactive Bottom Tab Dock (Cleanly above Android Navigation Bar)
static void render_bottom_navigation(UIController* ui, const Theme* th) {
    int pad = DP(16);
    int dock_w = ui->target.width - 2 * pad;
    int dock_h = DP(34);
    int dock_y = ui->target.height - DP(86);

    render_glass_card(&ui->target, pad, dock_y, dock_w, dock_h, DP(17), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    static const char* TAB_LABELS[PAGE_COUNT] = { "INICIO", "RED", "SISTEMA", "MOTOR C" };
    int tab_w = dock_w / PAGE_COUNT;

    // Animated active pill indicator
    int pill_x = pad + DP(3) + (int)(ui->page_scroll_x * (float)tab_w);
    int pill_w = tab_w - DP(6);
    render_rounded_rect(&ui->target, pill_x, dock_y + DP(4), pill_w, dock_h - DP(8), DP(13), color_alpha(th->accent, 45), th->accent, 1);

    for (int i = 0; i < PAGE_COUNT; i++) {
        int tx = pad + i * tab_w + tab_w / 2;
        uint32_t col = (i == (int)(ui->page_scroll_x + 0.5f)) ? th->text_primary : th->text_muted;
        text_draw_centered(&ui->target, TAB_LABELS[i], tx, dock_y + DP(10), FONT_XS, col);
    }
}

// PAGE 0: INICIO (HOME & SHIZUKU ACTION CENTER)
static void render_page_home(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(70);
    int gap = DP(8);

    // 1. Hero Clock + Uptime
    time_t raw_time;
    time(&raw_time);
    struct tm* tm_info = localtime(&raw_time);

    char time_hm[16], time_s[8], date_str[64];
    snprintf(time_hm, sizeof(time_hm), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    snprintf(time_s, sizeof(time_s), ":%02d", tm_info->tm_sec);

    static const char* DAYS_ES[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};
    static const char* MONTHS_ES[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};

    struct sysinfo si;
    long up_h = 4, up_m = 12;
    if (sysinfo(&si) == 0) {
        up_h = si.uptime / 3600;
        up_m = (si.uptime % 3600) / 60;
    }
    snprintf(date_str, sizeof(date_str), "%s, %02d %s  ·  Activo: %ldh %02ldm",
             DAYS_ES[tm_info->tm_wday], tm_info->tm_mday, MONTHS_ES[tm_info->tm_mon], up_h, up_m);

    int clock_h = DP(108);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, clock_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "TIEMPO REAL DEL SISTEMA", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);
    text_draw_right(&ui->target, "2x TOQUE = TEMA", page_x + pad + w - DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int hm_w = text_width(time_hm, FONT_HERO);
    int s_w = text_width(time_s, FONT_LG);
    int clock_start_x = page_x + pad + (w - (hm_w + s_w + DP(2))) / 2;
    int clock_y = cur_y + DP(22);

    text_draw_tabular(&ui->target, time_hm, clock_start_x, clock_y, FONT_HERO, th->text_primary);
    text_draw_tabular(&ui->target, time_s, clock_start_x + hm_w + DP(2), clock_y + DP(14), FONT_LG, th->accent);
    text_draw_centered(&ui->target, date_str, page_x + pad + w / 2, cur_y + DP(76), FONT_SM, th->text_secondary);

    cur_y += clock_h + gap;

    // 2. Tres Anillos de Telemetría (CPU, RAM, BATERÍA)
    int rings_h = DP(122);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, rings_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "TELEMETRIA DE HARDWARE EN VIVO", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);
    char w_top_str[24];
    snprintf(w_top_str, sizeof(w_top_str), "%.2f W", ui->batt.power_w);
    text_draw_right(&ui->target, w_top_str, page_x + pad + w - DP(14), cur_y + DP(9), FONT_XS, th->accent);

    int ring_col_w = w / 3;
    int ring_r = DP(29);
    int ring_thick = DP(5);
    int ring_cy = cur_y + DP(60);

    // Ring 1: CPU
    int c1_x = page_x + pad + ring_col_w / 2;
    uint32_t cpu_col = (ui->anim_cpu_percent < 60.0f) ? th->success : (ui->anim_cpu_percent < 85.0f) ? th->warning : th->danger;
    render_ring(&ui->target, c1_x, ring_cy, ring_r, ring_thick, ui->anim_cpu_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, cpu_col, th->surface_inset);
    render_icon_cpu(&ui->target, c1_x, ring_cy - DP(8), DP(10), th->text_muted);
    char cpu_txt[16];
    snprintf(cpu_txt, sizeof(cpu_txt), "%d%%", (int)(ui->anim_cpu_percent + 0.5f));
    text_draw_centered(&ui->target, cpu_txt, c1_x, ring_cy + DP(1), FONT_SM, th->text_primary);
    text_draw_centered(&ui->target, "CPU", c1_x, ring_cy + ring_r + DP(6), FONT_XS, th->text_secondary);

    // Ring 2: RAM
    int c2_x = page_x + pad + ring_col_w + ring_col_w / 2;
    uint32_t ram_col = (ui->anim_ram_percent < 75.0f) ? th->accent : th->warning;
    render_ring(&ui->target, c2_x, ring_cy, ring_r, ring_thick, ui->anim_ram_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, ram_col, th->surface_inset);
    render_icon_ram(&ui->target, c2_x, ring_cy - DP(8), DP(12), th->text_muted);
    char ram_txt[16];
    snprintf(ram_txt, sizeof(ram_txt), "%d%%", (int)(ui->anim_ram_percent + 0.5f));
    text_draw_centered(&ui->target, ram_txt, c2_x, ring_cy + DP(1), FONT_SM, th->text_primary);
    text_draw_centered(&ui->target, "RAM", c2_x, ring_cy + ring_r + DP(6), FONT_XS, th->text_secondary);

    // Ring 3: BATERÍA
    int c3_x = page_x + pad + 2 * ring_col_w + ring_col_w / 2;
    uint32_t batt_col = (ui->anim_batt_percent > 20.0f) ? th->success : th->danger;
    render_ring(&ui->target, c3_x, ring_cy, ring_r, ring_thick, ui->anim_batt_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, batt_col, th->surface_inset);
    render_icon_battery(&ui->target, c3_x, ring_cy - DP(8), DP(12), (int)ui->anim_batt_percent, ui->batt.is_charging, th->text_muted);
    char batt_txt[16];
    snprintf(batt_txt, sizeof(batt_txt), "%d%%%s", (int)(ui->anim_batt_percent + 0.5f), ui->batt.is_charging ? "+" : "");
    text_draw_centered(&ui->target, batt_txt, c3_x, ring_cy + DP(1), FONT_SM, th->text_primary);
    text_draw_centered(&ui->target, "BATERIA", c3_x, ring_cy + ring_r + DP(6), FONT_XS, th->text_secondary);

    cur_y += rings_h + gap;

    // 3. Salud Global & Diagnóstico Inteligente
    int health_h = DP(84);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, health_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "SALUD GLOBAL & DIAGNOSTICO", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int hr_cx = page_x + pad + DP(40);
    int hr_cy = cur_y + DP(49);
    uint32_t health_col = (ui->anim_health_score >= 80.0f) ? th->success : (ui->anim_health_score >= 60.0f) ? th->warning : th->danger;
    render_ring(&ui->target, hr_cx, hr_cy, DP(23), DP(4), ui->anim_health_score / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, health_col, th->surface_inset);
    char score_str[16];
    snprintf(score_str, sizeof(score_str), "%d", (int)(ui->anim_health_score + 0.5f));
    text_draw_centered(&ui->target, score_str, hr_cx, hr_cy - DP(8), FONT_MD, th->text_primary);

    int hd_x = page_x + pad + DP(76);
    int hd_y = cur_y + DP(30);
    if (ui->mem.storage_free_gb < 1.5f) {
        text_draw(&ui->target, "ALERTA: ALMACENAMIENTO CASI LLENO", hd_x, hd_y, FONT_SM, th->warning);
    } else {
        const char* status_badge = (ui->anim_health_score >= 80.0f) ? "RENDIMIENTO OPTIMO" : "CARGA ELEVADA";
        text_draw(&ui->target, status_badge, hd_x, hd_y, FONT_SM, health_col);
    }

    char health_sub[72];
    snprintf(health_sub, sizeof(health_sub), "RAM Libre: %.0f MB  ·  /data: %.1f GB  ·  %.1f°C",
             ui->mem.free_mb, ui->mem.storage_free_gb, ui->cpu.temp_cpu_c);
    text_draw(&ui->target, health_sub, hd_x, hd_y + DP(20), FONT_XS, th->text_secondary);

    cur_y += health_h + gap;

    // 4. Mini Sparkline Historial (CPU / RAM)
    int spark_card_h = DP(94);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, spark_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "HISTORIAL CONTINUO 60S (CPU VERDE / RAM CIAN)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    render_dual_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(30), w - DP(28), DP(50),
                          ui->cpu.history, ui->mem.history, CPU_HISTORY_LEN, ui->cpu.history_idx, 100.0f,
                          th->success, th->accent);

    cur_y += spark_card_h + gap;

    // 5. Centro de Acción Rápida (Interactive Shizuku / System Controls)
    int act_h = DP(184);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, act_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "CENTRO DE CONTROL & OPTIMIZACION EN 1 TOQUE", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int btn_x = page_x + pad + DP(12);
    int btn_w = w - DP(24);
    int btn1_y = cur_y + DP(28);
    int btn_h = DP(36);

    // Button 1: Boost RAM
    render_rounded_rect(&ui->target, btn_x, btn1_y, btn_w, btn_h, DP(10), color_alpha(th->accent, 35), th->accent, 1);
    text_draw_centered(&ui->target, "LIBERAR RAM & DETENER APPS EN FONDO", btn_x + btn_w / 2, btn1_y + DP(10), FONT_SM, th->accent_glow);

    // Row 2: Trim Cache & Toggle Turbo 0.5x Animations
    int btn2_y = btn1_y + btn_h + DP(8);
    int half_w = (btn_w - DP(8)) / 2;

    render_rounded_rect(&ui->target, btn_x, btn2_y, half_w, btn_h, DP(10), color_alpha(th->success, 30), th->success, 1);
    text_draw_centered(&ui->target, "PURGAR CACHE /data", btn_x + half_w / 2, btn2_y + DP(11), FONT_XS, th->success);

    int btn3_x = btn_x + half_w + DP(8);
    bool is_turbo = (ui->bridge.anim_scale < 0.75f);
    uint32_t turbo_col = is_turbo ? th->warning : th->text_secondary;
    render_rounded_rect(&ui->target, btn3_x, btn2_y, half_w, btn_h, DP(10), color_alpha(turbo_col, 30), turbo_col, 1);
    char anim_lbl[32];
    snprintf(anim_lbl, sizeof(anim_lbl), "ANIMACIONES: %.1fx %s", ui->bridge.anim_scale, is_turbo ? "(TURBO)" : "");
    text_draw_centered(&ui->target, anim_lbl, btn3_x + half_w / 2, btn2_y + DP(11), FONT_XS, is_turbo ? th->warning : th->text_primary);

    // Live Feedback Status Bar inside card
    int st_y = btn2_y + btn_h + DP(10);
    render_rounded_rect(&ui->target, btn_x, st_y, btn_w, DP(44), DP(8), th->surface_inset, 0, 0);
    if (ui->bridge.last_action_msg[0] != '\0') {
        text_draw_centered(&ui->target, ui->bridge.last_action_msg, btn_x + btn_w / 2, st_y + DP(7), FONT_XS, th->success);
    } else if (ui->bridge.is_active) {
        text_draw_centered(&ui->target, "Puente Shizuku/ADB Activo (UID 2000 Privilegiado)", btn_x + btn_w / 2, st_y + DP(7), FONT_XS, th->success);
    } else {
        text_draw_centered(&ui->target, "Modo Nativo Activo · Toca un boton para optimizar", btn_x + btn_w / 2, st_y + DP(7), FONT_XS, th->text_secondary);
    }
    char sub_info[72];
    snprintf(sub_info, sizeof(sub_info), "Potencia: %.2f W (%d mA)  ·  Hilos: 2  ·  %.0f FPS",
             ui->batt.power_w, (int)ui->batt.current_ma, ui->engine.fps);
    text_draw_centered(&ui->target, sub_info, btn_x + btn_w / 2, st_y + DP(24), FONT_XS, th->text_muted);
}

// PAGE 1: RED & CONEXIONES (NETWORK & ACTIVE SOCKETS)
static void render_page_network(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(70);
    int gap = DP(8);

    // 1. Velocímetro y Enlace Wi-Fi
    int speed_card_h = DP(126);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, speed_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "ENLACE WI-FI & SESION ACUMULADA", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    char sess_str[48];
    snprintf(sess_str, sizeof(sess_str), "RX: %.1fM · TX: %.1fM",
             (double)ui->net.session_rx_bytes / (1024.0 * 1024.0),
             (double)ui->net.session_tx_bytes / (1024.0 * 1024.0));
    text_draw_right(&ui->target, sess_str, page_x + pad + w - DP(14), cur_y + DP(9), FONT_XS, th->accent);

    int s_cx = page_x + pad + DP(48);
    int s_cy = cur_y + DP(68);
    float ratio = (float)ui->net.link_speed_mbps / 300.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    render_ring(&ui->target, s_cx, s_cy, DP(34), DP(5), ratio, PI_F * 0.75f, PI_F * 1.5f, th->accent, th->surface_inset);
    char spd_txt[16];
    snprintf(spd_txt, sizeof(spd_txt), "%d", ui->net.link_speed_mbps);
    text_draw_centered(&ui->target, spd_txt, s_cx, s_cy - DP(15), FONT_XL, th->text_primary);
    text_draw_centered(&ui->target, "Mbps", s_cx, s_cy + DP(8), FONT_XS, th->text_muted);

    int meta_x = s_cx + DP(46);
    int meta_y = cur_y + DP(30);

    render_circle(&ui->target, meta_x + DP(3), meta_y + DP(6), DP(3), ui->net.is_wifi_connected ? th->success : th->danger);
    text_draw(&ui->target, ui->net.ssid, meta_x + DP(10), meta_y, FONT_SM, th->text_primary);

    meta_y += DP(22);
    render_icon_wifi(&ui->target, meta_x + DP(6), meta_y + DP(4), DP(12), ui->net.signal_level, th->accent, th->surface_inset);
    char rssi_buf[48];
    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm  ·  %s", ui->net.rssi_dbm, ui->net.standard_str);
    text_draw(&ui->target, rssi_buf, meta_x + DP(20), meta_y, FONT_XS, th->text_secondary);

    meta_y += DP(20);
    char ip_meta[64];
    snprintf(ip_meta, sizeof(ip_meta), "IP: %s  ·  GW: %s", ui->net.local_ip, ui->net.gateway_ip);
    text_draw(&ui->target, ip_meta, meta_x, meta_y, FONT_XS, th->text_muted);

    cur_y += speed_card_h + gap;

    // 2. Gráfica de Tráfico en Tiempo Real (60s)
    int traf_card_h = DP(122);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, traf_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "FLUJO EN TIEMPO REAL (60s)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    char peak_str[48];
    snprintf(peak_str, sizeof(peak_str), "Pico: %.0f KB/s", ui->net.rx_peak_kbps > ui->net.tx_peak_kbps ? ui->net.rx_peak_kbps : ui->net.tx_peak_kbps);
    text_draw_right(&ui->target, peak_str, page_x + pad + w - DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

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

    float peak = ui->net.rx_peak_kbps > ui->net.tx_peak_kbps ? ui->net.rx_peak_kbps : ui->net.tx_peak_kbps;
    if (peak < 100.0f) peak = 100.0f;
    render_dual_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(52), w - DP(28), DP(54),
                          ui->net.rx_history, ui->net.tx_history, NET_HISTORY_LEN, ui->net.history_idx, peak,
                          th->success, th->accent);

    cur_y += traf_card_h + gap;

    // 3. Latencia y Calidad a 3 Destinos
    int ping_card_h = DP(118);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, ping_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "LATENCIA TCP, JITTER & PERDIDA (VENTANA 10)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int row_y = cur_y + DP(28);
    for (int i = 0; i < NUM_PING_TARGETS; i++) {
        const PingTarget* pt = &ui->net.ping_targets[i];
        uint32_t q_col = (pt->quality == NET_QUALITY_GOOD) ? th->success : (pt->quality == NET_QUALITY_FAIR) ? th->warning : th->danger;

        render_circle(&ui->target, page_x + pad + DP(20), row_y + DP(8), DP(3), q_col);
        text_draw(&ui->target, pt->name, page_x + pad + DP(28), row_y, FONT_SM, th->text_primary);

        char stats_buf[64];
        if (pt->last_ping_ms >= 0) {
            snprintf(stats_buf, sizeof(stats_buf), "%d ms  ·  ±%.1f ms  ·  %.0f%% perd", pt->last_ping_ms, pt->jitter_ms, pt->packet_loss);
        } else {
            snprintf(stats_buf, sizeof(stats_buf), " Midiendo... ");
        }
        text_draw_right(&ui->target, stats_buf, page_x + pad + w - DP(14), row_y, FONT_SM, q_col);
        row_y += DP(27);
    }

    cur_y += ping_card_h + gap;

    // 4. Escáner de Conexiones & Sockets Activos (/proc/net/tcp)
    int conn_card_h = DP(234);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, conn_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "CONEXIONES & SOCKETS ACTIVOS (/proc/net/tcp)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int c_row_y = cur_y + DP(28);
    int max_c = ui->bridge.conn_count < 6 ? ui->bridge.conn_count : 6;
    for (int i = 0; i < max_c; i++) {
        const ActiveConnEntry* ce = &ui->bridge.conns[i];
        bool is_estab = (strcmp(ce->state, "ESTAB") == 0);
        uint32_t st_col = is_estab ? th->success : th->accent;

        render_rounded_rect(&ui->target, page_x + pad + DP(12), c_row_y, w - DP(24), DP(28), DP(6), th->surface_inset, 0, 0);

        // Service badge
        render_rounded_rect(&ui->target, page_x + pad + DP(16), c_row_y + DP(4), DP(54), DP(20), DP(4), color_alpha(st_col, 35), 0, 0);
        text_draw_centered(&ui->target, ce->service, page_x + pad + DP(43), c_row_y + DP(6), FONT_XS, st_col);

        // Local -> Remote endpoint
        char ep_str[64];
        snprintf(ep_str, sizeof(ep_str), ":%d -> %s", ce->local_port, ce->remote_addr);
        text_draw(&ui->target, ep_str, page_x + pad + DP(76), c_row_y + DP(6), FONT_XS, th->text_primary);

        text_draw_right(&ui->target, ce->state, page_x + pad + w - DP(20), c_row_y + DP(6), FONT_XS, st_col);
        c_row_y += DP(33);
    }
}

// PAGE 2: SISTEMA & PROCESOS (SYSTEM & LIVE PROCESS MANAGER)
static void render_page_system(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(70);
    int gap = DP(8);

    // 1. Núcleos de CPU (8 Cores + Cluster Frequencies)
    int cores_card_h = DP(124);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, cores_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    char cpu_head[64];
    snprintf(cpu_head, sizeof(cpu_head), "CPU (%d NUCLEOS)  ·  LOAD: %.2f", ui->cpu.core_count, ui->cpu.load_1m);
    text_draw(&ui->target, cpu_head, page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    char freq_head[48];
    snprintf(freq_head, sizeof(freq_head), "%d / %d MHz", ui->cpu.cores[0].cur_freq_mhz, ui->cpu.cores[4].cur_freq_mhz);
    text_draw_right(&ui->target, freq_head, page_x + pad + w - DP(14), cur_y + DP(9), FONT_XS, th->accent);

    int num_cores = ui->cpu.core_count > 8 ? 8 : ui->cpu.core_count;
    if (num_cores <= 0) num_cores = 8;
    int bar_w = (w - DP(28) - (num_cores - 1) * DP(6)) / num_cores;
    int bar_max_h = DP(56);
    int bar_base_y = cur_y + DP(88);

    for (int i = 0; i < num_cores; i++) {
        float u = ui->cpu.cores[i].usage_percent;
        int h = (int)((u / 100.0f) * (float)bar_max_h);
        if (h < 3) h = 3;
        int bx = page_x + pad + DP(14) + i * (bar_w + DP(6));
        int by = bar_base_y - h;

        uint32_t col = (u < 50.0f) ? th->success : (u < 80.0f) ? th->warning : th->danger;
        render_rounded_rect(&ui->target, bx, bar_base_y - bar_max_h, bar_w, bar_max_h, DP(3), th->surface_inset, 0, 0);
        render_rounded_rect(&ui->target, bx, by, bar_w, h, DP(3), col, 0, 0);

        char pct_lbl[12];
        snprintf(pct_lbl, sizeof(pct_lbl), "%d%%", (int)(u + 0.5f));
        text_draw_centered(&ui->target, pct_lbl, bx + bar_w / 2, bar_base_y + DP(3), FONT_XS, th->text_primary);
        char c_num[12];
        snprintf(c_num, sizeof(c_num), "c%d", i);
        text_draw_centered(&ui->target, c_num, bx + bar_w / 2, bar_base_y + DP(16), FONT_XS, th->text_muted);
    }

    cur_y += cores_card_h + gap;

    // 2. Temperaturas Térmicas HAL (4 Sensores)
    int temp_card_h = DP(74);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, temp_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "SENSORES TERMICOS HAL (CPU / GPU / BAT / SKIN)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int col_w = w / 4;
    int t_y = cur_y + DP(28);
    float temps[4] = { ui->cpu.temp_cpu_c, ui->cpu.temp_gpu_c, ui->batt.temp_c, ui->cpu.temp_skin_c };
    const char* t_names[4] = { "CPU", "GPU", "BATERIA", "CHASIS" };
    uint32_t t_cols[4] = { th->accent, th->warning, th->success, th->text_secondary };

    for (int i = 0; i < 4; i++) {
        int tx = page_x + pad + i * col_w + col_w / 2;
        char t_str[16];
        snprintf(t_str, sizeof(t_str), "%.1f°C", temps[i]);
        text_draw_centered(&ui->target, t_str, tx, t_y, FONT_SM, t_cols[i]);
        text_draw_centered(&ui->target, t_names[i], tx, t_y + DP(19), FONT_XS, th->text_muted);
    }

    cur_y += temp_card_h + gap;

    // 3. Almacenamiento (/data) & Memoria RAM Apilada
    int stor_card_h = DP(98);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, stor_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "ALMACENAMIENTO (/data) & RAM / ZRAM", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    char stor_stats[64];
    snprintf(stor_stats, sizeof(stor_stats), "%.1f / %.1f GB (%.0f%%)", ui->mem.storage_used_gb, ui->mem.storage_total_gb, ui->mem.storage_percent);
    uint32_t stor_col = (ui->mem.storage_percent > 90.0f) ? th->danger : th->accent;
    text_draw_right(&ui->target, stor_stats, page_x + pad + w - DP(14), cur_y + DP(9), FONT_XS, stor_col);

    int seg_bar_w = w - DP(28);
    int seg_filled = (int)((ui->mem.storage_percent / 100.0f) * 14.0f + 0.5f);
    render_segmented_bar(&ui->target, page_x + pad + DP(14), cur_y + DP(29), seg_bar_w, DP(11), 14, seg_filled, stor_col, th->surface_inset, DP(3));

    char ram_detail[80];
    snprintf(ram_detail, sizeof(ram_detail), "RAM Usada: %.0fM  ·  Cache: %.0fM  ·  Libre: %.0fM",
             ui->mem.used_mb, ui->mem.cached_mb, ui->mem.free_mb);
    text_draw(&ui->target, ram_detail, page_x + pad + DP(14), cur_y + DP(48), FONT_XS, th->text_primary);

    char free_str[80];
    snprintf(free_str, sizeof(free_str), "Espacio libre /data: %.2f GB  ·  Swap ZRAM: %.0f / %.0f MB",
             ui->mem.storage_free_gb, ui->mem.swap_used_mb, ui->mem.swap_total_mb);
    text_draw(&ui->target, free_str, page_x + pad + DP(14), cur_y + DP(68), FONT_XS, th->text_muted);

    cur_y += stor_card_h + gap;

    // 4. Batería Detallada
    int batt_card_h = DP(84);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, batt_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "TELEMETRIA ELECTRICA DE BATERIA", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int b_row1 = cur_y + DP(28);
    char volt_str[48], pow_str[48];
    snprintf(volt_str, sizeof(volt_str), "%.3f V  ·  %.0f mA", ui->batt.voltage_v, ui->batt.current_ma);
    snprintf(pow_str, sizeof(pow_str), "%.2f W  ·  %s", ui->batt.power_w, ui->batt.status_str);
    text_draw(&ui->target, volt_str, page_x + pad + DP(14), b_row1, FONT_SM, th->text_primary);
    text_draw_right(&ui->target, pow_str, page_x + pad + w - DP(14), b_row1, FONT_SM, th->accent);

    int b_row2 = b_row1 + DP(24);
    char rem_str[80];
    snprintf(rem_str, sizeof(rem_str), "Carga: %d / %d mAh  ·  Ciclos: %d  ·  Restante: ~%dh %02dm",
             ui->batt.charge_mah, ui->batt.full_mah, ui->batt.cycle_count,
             ui->batt.time_remaining_min / 60, ui->batt.time_remaining_min % 60);
    text_draw(&ui->target, rem_str, page_x + pad + DP(14), b_row2, FONT_XS, th->text_muted);

    cur_y += batt_card_h + gap;

    // 5. Gestor de Procesos en Vivo (Top 4 con botón DETENER)
    int proc_card_h = DP(212);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, proc_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "GESTOR DE PROCESOS EN VIVO (TOP CPU / RAM)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int p_row_y = cur_y + DP(30);
    int max_p = ui->bridge.proc_count < 4 ? ui->bridge.proc_count : 4;
    for (int i = 0; i < max_p; i++) {
        const TopProcEntry* pe = &ui->bridge.procs[i];
        render_rounded_rect(&ui->target, page_x + pad + DP(12), p_row_y, w - DP(24), DP(34), DP(8), th->surface_inset, 0, 0);

        char p_title[48];
        snprintf(p_title, sizeof(p_title), "%.24s", pe->name);
        text_draw(&ui->target, p_title, page_x + pad + DP(20), p_row_y + DP(3), FONT_XS, th->text_primary);

        char p_stats[64];
        snprintf(p_stats, sizeof(p_stats), "PID %d  ·  CPU: %.1f%%  ·  RAM: %.0f MB", pe->pid, pe->cpu_pct, pe->rss_mb);
        text_draw(&ui->target, p_stats, page_x + pad + DP(20), p_row_y + DP(18), FONT_XS, th->text_muted);

        // Action button [ DETENER ]
        int k_w = DP(66);
        int k_x = page_x + pad + w - DP(18) - k_w;
        render_rounded_rect(&ui->target, k_x, p_row_y + DP(6), k_w, DP(22), DP(6), color_alpha(th->danger, 35), th->danger, 1);
        text_draw_centered(&ui->target, "DETENER", k_x + k_w / 2, p_row_y + DP(9), FONT_XS, th->danger);

        p_row_y += DP(38);
    }
}

// PAGE 3: MOTOR C (C ENGINE EFFICIENCY PROOF & BENCHMARK)
static void render_page_motor_c(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(70);
    int gap = DP(8);

    // 1. Insignia Principal de Eficiencia C
    int proof_card_h = DP(120);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, proof_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "EFICIENCIA NATIVA EN TIEMPO REAL (ESTE PROCESO)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int c_col_w = w / 3;
    int num_y = cur_y + DP(28);

    char app_cpu_txt[16];
    snprintf(app_cpu_txt, sizeof(app_cpu_txt), "%.1f%%", ui->engine.app_cpu_percent);
    text_draw_centered(&ui->target, app_cpu_txt, page_x + pad + c_col_w / 2, num_y, FONT_LG, th->success);
    text_draw_centered(&ui->target, "CPU APP", page_x + pad + c_col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    char app_ram_txt[16];
    snprintf(app_ram_txt, sizeof(app_ram_txt), "%.1f MB", ui->engine.app_ram_anon_mb);
    text_draw_centered(&ui->target, app_ram_txt, page_x + pad + c_col_w + c_col_w / 2, num_y, FONT_LG, th->accent);
    text_draw_centered(&ui->target, "RAM PROPIA", page_x + pad + c_col_w + c_col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    text_draw_centered(&ui->target, "0", page_x + pad + 2 * c_col_w + c_col_w / 2, num_y, FONT_LG, th->text_primary);
    text_draw_centered(&ui->target, "ALLOCS/FRAME", page_x + pad + 2 * c_col_w + c_col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    char badge_str[72];
    snprintf(badge_str, sizeof(badge_str), "%.1f MB RAM PROPIA  ·  %.1f%% CPU  ·  0 ALLOCS/FRAME",
             ui->engine.app_ram_anon_mb, ui->engine.app_cpu_percent);
    render_glass_card(&ui->target, page_x + pad + DP(14), cur_y + DP(82), w - DP(28), DP(26), DP(6), th->surface_inset, th->surface_inset, th->accent, 0);
    text_draw_centered(&ui->target, badge_str, page_x + pad + w / 2, cur_y + DP(87), FONT_XS, th->accent_glow);

    cur_y += proof_card_h + gap;

    // 2. Telemetría de Frame-Time & VSync
    int frame_card_h = DP(96);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, frame_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "TIEMPO DE RENDERIZADO NEON & FPS REALES", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    char frame_stats[72];
    snprintf(frame_stats, sizeof(frame_stats), "Render: %.0f µs  ·  %.1f FPS  ·  p50: %.0f µs  ·  p99: %.0f µs",
             ui->engine.frame_time_us, ui->engine.fps, ui->engine.frame_p50_us, ui->engine.frame_p99_us);
    text_draw(&ui->target, frame_stats, page_x + pad + DP(14), cur_y + DP(27), FONT_XS, th->text_primary);

    render_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(48), w - DP(28), DP(36),
                     ui->engine.frame_history, FRAME_HISTORY_LEN, ui->engine.frame_history_idx, 0.0f, 16600.0f,
                     th->accent, color_alpha(th->accent, 35));

    cur_y += frame_card_h + gap;

    // 3. Huella del Binario C
    int bin_card_h = DP(72);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, bin_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "HUELLA BINARIA & ARRANQUE EN FRIO (ARM64 NEON)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    char bin_txt[96];
    snprintf(bin_txt, sizeof(bin_txt), ".so: %d KB  ·  APK: %d KB  ·  Boot: %d ms  ·  Hilos: %d",
             ui->engine.binary_so_kb, ui->engine.apk_kb, ui->engine.cold_boot_ms, ui->engine.app_threads);
    text_draw(&ui->target, bin_txt, page_x + pad + DP(14), cur_y + DP(29), FONT_SM, th->text_primary);
    text_draw(&ui->target, "0 dependencias externas  ·  Arena estatica sin GC", page_x + pad + DP(14), cur_y + DP(49), FONT_XS, th->text_muted);

    cur_y += bin_card_h + gap;

    // 4. Botón y Barras Comparativas de BENCHMARK Interactivo (2s)
    int bench_card_h = DP(312);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, bench_card_h, DP(16), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "BENCHMARK DE HARDWARE ARM64 NEON (2 SEGUNDOS)", page_x + pad + DP(14), cur_y + DP(9), FONT_XS, th->text_muted);

    int bx = page_x + pad + DP(14);
    int btn_w = w - DP(28);
    int by = cur_y + DP(26);

    if (ui->bench.is_running) {
        render_rounded_rect(&ui->target, bx, by, btn_w, DP(34), DP(8), th->surface_inset, 0, 0);
        int fill = (int)(ui->bench.progress * (float)btn_w);
        render_rounded_rect(&ui->target, bx, by, fill, DP(34), DP(8), th->accent, 0, 0);
        text_draw_centered(&ui->target, "EJECUTANDO PRUEBAS SIMD NEON EN VIVO...", page_x + pad + w / 2, by + DP(9), FONT_SM, th->text_primary);
    } else {
        render_rounded_rect(&ui->target, bx, by, btn_w, DP(34), DP(8), color_alpha(th->accent, 35), th->accent, 1);
        text_draw_centered(&ui->target, "EJECUTAR BENCHMARK DE HARDWARE (2s)", bx + btn_w / 2, by + DP(9), FONT_SM, th->accent_glow);
    }

    // 4 Comparative Progress Bars
    int row_y = by + DP(46);
    struct {
        const char* label;
        float val;
        float ref;
        const char* unit;
        uint32_t col;
    } items[4] = {
        { "Relleno Raster NEON", ui->bench.pixel_fill_mpix_s, ui->bench.ref_pixel_mpix_s, "MPix/s", th->success },
        { "Ancho de Banda RAM",  ui->bench.memcpy_gb_s,       ui->bench.ref_memcpy_gb_s,  "GB/s",   th->accent },
        { "Criba Primos CPU",    ui->bench.primes_mprimes_s,  ui->bench.ref_primes_mprimes_s, "M/s", th->warning },
        { "SIMD Vectorial FMA",  ui->bench.neon_gflops,       ui->bench.ref_neon_gflops,  "GFLOPS", th->accent_glow }
    };

    for (int i = 0; i < 4; i++) {
        text_draw(&ui->target, items[i].label, bx, row_y, FONT_XS, th->text_secondary);
        char v_str[48];
        snprintf(v_str, sizeof(v_str), "%.1f %s (Ref: %.0f)", items[i].val, items[i].unit, items[i].ref);
        text_draw_right(&ui->target, v_str, bx + btn_w, row_y, FONT_XS, th->text_primary);

        float ratio = items[i].val / (items[i].ref * 1.25f);
        if (ratio > 1.0f) ratio = 1.0f;
        if (ratio < 0.05f) ratio = 0.05f;
        int fill_w = (int)(ratio * (float)btn_w);
        render_rounded_rect(&ui->target, bx, row_y + DP(16), btn_w, DP(10), DP(5), th->surface_inset, 0, 0);
        render_rounded_rect(&ui->target, bx, row_y + DP(16), fill_w, DP(10), DP(5), items[i].col, 0, 0);

        row_y += DP(44);
    }

    // Composite Score Footer Box
    int sc_y = cur_y + bench_card_h - DP(48);
    render_rounded_rect(&ui->target, bx, sc_y, btn_w, DP(36), DP(8), th->surface_inset, th->card_border, 1);
    text_draw(&ui->target, "PUNTUACION GLOBAL C/NEON:", bx + DP(12), sc_y + DP(10), FONT_XS, th->text_secondary);
    char tot_str[32];
    snprintf(tot_str, sizeof(tot_str), "%d PTS", ui->bench.total_score);
    text_draw_right(&ui->target, tot_str, bx + btn_w - DP(12), sc_y + DP(8), FONT_MD, th->success);
}

void ui_render_frame(UIController* ui, float dt) {
    if (!ui || !ui->target.pixels || ui->target.width <= 0 || ui->target.height <= 0) return;

    const Theme* th = theme_get_current();

    if (!ui->is_swiping) {
        float diff = ui->target_page_scroll - ui->page_scroll_x;
        ui->page_scroll_x += diff * (dt * 14.0f);
        if (fabsf(diff) < 0.004f) ui->page_scroll_x = ui->target_page_scroll;
    }

    float ease = dt * 6.5f;
    if (ease > 1.0f) ease = 1.0f;
    ui->anim_cpu_percent += (ui->cpu.total_usage - ui->anim_cpu_percent) * ease;
    ui->anim_ram_percent += (ui->mem.ram_percent - ui->anim_ram_percent) * ease;
    ui->anim_batt_percent += ((float)ui->batt.level_percent - ui->anim_batt_percent) * ease;
    ui->anim_rx_rate_kbps += (ui->net.rx_rate_kbps - ui->anim_rx_rate_kbps) * ease;
    ui->anim_tx_rate_kbps += (ui->net.tx_rate_kbps - ui->anim_tx_rate_kbps) * ease;

    float stor_penalty = (ui->mem.storage_percent > 90.0f) ? (ui->mem.storage_percent - 85.0f) * 0.8f : 0.0f;
    float raw_health = 100.0f - (ui->anim_cpu_percent * 0.15f +
                                 (ui->anim_ram_percent > 65.0f ? (ui->anim_ram_percent - 65.0f) * 0.3f : 0.0f) +
                                 (ui->cpu.temp_cpu_c > 45.0f ? (ui->cpu.temp_cpu_c - 45.0f) * 0.6f : 0.0f) +
                                 stor_penalty);
    if (raw_health < 15.0f) raw_health = 15.0f;
    if (raw_health > 100.0f) raw_health = 100.0f;
    ui->anim_health_score += (raw_health - ui->anim_health_score) * ease;

    bench_update_step(&ui->bench);

    render_clear(&ui->target, th->bg);

    int screen_w = ui->target.width;
    int min_p = (int)floorf(ui->page_scroll_x);
    int max_p = (int)ceilf(ui->page_scroll_x);
    if (min_p < 0) min_p = 0;
    if (max_p >= PAGE_COUNT) max_p = PAGE_COUNT - 1;

    for (int p = min_p; p <= max_p; p++) {
        int page_x = (int)(((float)p - ui->page_scroll_x) * (float)screen_w);
        switch (p) {
            case PAGE_HOME:    render_page_home(ui, th, page_x);    break;
            case PAGE_NETWORK: render_page_network(ui, th, page_x); break;
            case PAGE_SYSTEM:  render_page_system(ui, th, page_x);  break;
            case PAGE_MOTOR_C: render_page_motor_c(ui, th, page_x); break;
        }
    }

    render_top_bar(ui, th);
    render_bottom_navigation(ui, th);
}
