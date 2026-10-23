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
    ui->use_24h_clock = false; // 12-hour AM/PM clock synced with user device by default
    ui->ascii_clock_mode = 0;  // Mode 0: '#' ASCII Terminal Matrix (0 KB RAM)
    ui->art_offset = 0;
    ui->power_save_mode = false;

    text_init(ui->target.dp_scale);
    render_art_init_jni(NULL);

    sys_cpu_init(&ui->cpu);
    sys_mem_init(&ui->mem);
    sys_net_init(&ui->net);
    sys_batt_init(&ui->batt);
    sys_bridge_init(&ui->bridge);
    bench_engine_init(&ui->engine, 12, NULL);
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
        strncpy(ui->bridge.root_diag_msg, instant_msg, sizeof(ui->bridge.root_diag_msg) - 1);
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

        // 1. Bottom Interactive 5-Tab Navigation Dock (height - DP(92) .. height - DP(46))
        int dock_top = ui->target.height - DP(92);
        int dock_bot = ui->target.height - DP(46);
        if (y >= (float)dock_top && y <= (float)dock_bot) {
            int pad = DP(16);
            int dock_w = ui->target.width - 2 * pad;
            int tab_idx = (int)((x - (float)pad) / ((float)dock_w / (float)PAGE_COUNT));
            if (tab_idx >= 0 && tab_idx < PAGE_COUNT) {
                ui->active_page = (AppNavPage)tab_idx;
                ui->target_page_scroll = (float)tab_idx;
                ui->pending_vibrate_ms = 15;
                return;
            }
        }

        // 2. Top Right Quick Action Pill (jumps to AJUSTES, or cycles theme if already in AJUSTES)
        if (y >= (float)DP(24) && y <= (float)DP(66) && x >= (float)(ui->target.width - DP(96))) {
            if (ui->active_page != PAGE_SETTINGS) {
                ui->active_page = PAGE_SETTINGS;
                ui->target_page_scroll = (float)PAGE_SETTINGS;
            } else {
                theme_cycle();
            }
            ui->pending_vibrate_ms = 18;
            return;
        }

        // 3. Page-specific interactive actions
        if (ui->active_page == PAGE_HOME) {
            // Hero Card (y in DP(66)..DP(182))
            if (y >= (float)DP(66) && y <= (float)DP(182)) {
                if (x <= (float)DP(120)) {
                    ui->art_offset = (ui->art_offset + 1) % 5;
                    ui->pending_vibrate_ms = 16;
                    ui->target_page_scroll = (float)ui->active_page;
                    return;
                }
                // Double tap on ASCII Clock -> cycle ASCII style; Single tap -> toggle 12h/24h
                if (now - ui->last_clock_tap_time < 0.38) {
                    ui->ascii_clock_mode = (ui->ascii_clock_mode + 1) % 3;
                    ui->pending_vibrate_ms = 22;
                    ui->last_clock_tap_time = 0.0;
                } else {
                    ui->use_24h_clock = !ui->use_24h_clock;
                    ui->pending_vibrate_ms = 14;
                    ui->last_clock_tap_time = now;
                }
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }

            // Tap on 3 Rings Card (y in DP(190)..DP(300)) -> jump to System page
            if (y >= (float)DP(190) && y <= (float)DP(300)) {
                ui->active_page = PAGE_SYSTEM;
                ui->target_page_scroll = (float)PAGE_SYSTEM;
                ui->pending_vibrate_ms = 15;
                return;
            }

            // Tap on Health & Art Card left photo (y in DP(308)..DP(414), x <= DP(110)) -> cycle gallery
            if (y >= (float)DP(308) && y <= (float)DP(414) && x <= (float)DP(110)) {
                ui->art_offset = (ui->art_offset + 1) % 5;
                ui->pending_vibrate_ms = 16;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }

            // Root & System Control Buttons (Card 4)
            if (y >= (float)DP(432) && y <= (float)DP(480)) {
                if (x < (float)(ui->target.width / 2)) {
                    queue_ui_cmd(ui, "CMD BOOST_RAM", "Drop Caches + Liberando RAM...");
                } else {
                    queue_ui_cmd(ui, "CMD GOV_CYCLE", "Cambiando CPU Governor (Root)...");
                }
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            if (y >= (float)DP(486) && y <= (float)DP(534)) {
                if (x < (float)(ui->target.width / 2)) {
                    queue_ui_cmd(ui, "CMD TRIM_CACHE", "Ejecutando FSTRIM /data + Cache...");
                } else {
                    queue_ui_cmd(ui, "CMD TOGGLE_ANIM", "Ajustando escala UI (Turbo 0.5x)...");
                }
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
        } else if (ui->active_page == PAGE_NETWORK) {
            if (y >= (float)DP(66) && y <= (float)DP(190) && x <= (float)DP(124)) {
                ui->art_offset = (ui->art_offset + 1) % 5;
                ui->pending_vibrate_ms = 16;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
        } else if (ui->active_page == PAGE_SYSTEM) {
            if (y >= (float)DP(66) && y <= (float)DP(110) && x >= (float)(ui->target.width - DP(140))) {
                queue_ui_cmd(ui, "CMD GOV_CYCLE", "Cambiando CPU Governor (Root)...");
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            if (y >= (float)DP(196) && y <= (float)DP(302) && x <= (float)DP(114)) {
                ui->art_offset = (ui->art_offset + 1) % 5;
                ui->pending_vibrate_ms = 16;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            int row_start_y = DP(418);
            int row_h = DP(40);
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
            if (y >= (float)DP(66) && y <= (float)DP(186) && x <= (float)DP(122)) {
                ui->art_offset = (ui->art_offset + 1) % 5;
                ui->pending_vibrate_ms = 16;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            if (y >= (float)DP(300) && y <= (float)DP(356)) {
                bench_trigger(&ui->bench);
                ui->pending_vibrate_ms = 25;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
        } else if (ui->active_page == PAGE_SETTINGS) {
            // Card 1: Root Re-Check Button (y in DP(148)..DP(196))
            if (y >= (float)DP(146) && y <= (float)DP(198)) {
                queue_ui_cmd(ui, "CMD RECHECK_ROOT", "Ejecutando Re-Check de Root (su)...");
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            // Card 1 Quick Root Actions Row (y in DP(204)..DP(248))
            if (y >= (float)DP(202) && y <= (float)DP(250)) {
                if (x < (float)(ui->target.width / 2)) {
                    queue_ui_cmd(ui, "CMD GOV_CYCLE", "Cambiando CPU Governor (Root)...");
                } else {
                    queue_ui_cmd(ui, "CMD BOOST_RAM", "Drop Caches del Kernel + RAM...");
                }
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            // Card 2: ASCII Clock & Time Format Settings
            // Button: ASCII Clock Style (y in DP(300)..DP(346))
            if (y >= (float)DP(298) && y <= (float)DP(348)) {
                ui->ascii_clock_mode = (ui->ascii_clock_mode + 1) % 3;
                ui->pending_vibrate_ms = 18;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            // Button: 12h / 24h Format (y in DP(354)..DP(400))
            if (y >= (float)DP(352) && y <= (float)DP(402)) {
                ui->use_24h_clock = !ui->use_24h_clock;
                ui->pending_vibrate_ms = 18;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            // Card 3: Stability, Photo Gallery & Theme Settings
            // Row 1 (y in DP(454)..DP(500)): Left = FPS Power Save, Right = Photo Gallery
            if (y >= (float)DP(452) && y <= (float)DP(502)) {
                if (x < (float)(ui->target.width / 2)) {
                    ui->power_save_mode = !ui->power_save_mode;
                } else {
                    ui->art_offset = (ui->art_offset + 1) % 5;
                }
                ui->pending_vibrate_ms = 18;
                ui->target_page_scroll = (float)ui->active_page;
                return;
            }
            // Row 2 (y in DP(508)..DP(556)): Cycle Visual Theme
            if (y >= (float)DP(506) && y <= (float)DP(558)) {
                theme_cycle();
                ui->pending_vibrate_ms = 20;
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

// Minimalist Top Bar: Real Device Model + Root/Shizuku Badge + Quick Settings/Theme Pill
static void render_top_bar(UIController* ui, const Theme* th) {
    int pad = DP(16);
    int top_y = DP(ui->safe_top_dp);

    render_icon_engine(&ui->target, pad + DP(6), top_y + DP(11), DP(14), th->accent);

    char dev_title[64];
    snprintf(dev_title, sizeof(dev_title), "%s", ui->bridge.device_model);
    text_draw(&ui->target, dev_title, pad + DP(18), top_y + DP(2), FONT_MD, th->text_primary);

    int badge_x = pad + DP(18) + text_width(dev_title, FONT_MD) + DP(8);
    char badge_txt[32];
    uint32_t badge_col = th->accent;
    if (ui->bridge.is_root) {
        snprintf(badge_txt, sizeof(badge_txt), "ROOT · A%s", ui->bridge.android_ver);
        badge_col = th->success;
    } else if (ui->bridge.is_active) {
        snprintf(badge_txt, sizeof(badge_txt), "SHIZUKU · A%s", ui->bridge.android_ver);
        badge_col = th->success;
    } else {
        snprintf(badge_txt, sizeof(badge_txt), "ANDROID %s", ui->bridge.android_ver);
        badge_col = th->accent;
    }
    int bw = text_width(badge_txt, FONT_XS) + DP(14);
    render_rounded_rect(&ui->target, badge_x, top_y + DP(3), bw, DP(16), DP(8), color_alpha(badge_col, 38), badge_col, 1);
    text_draw_centered(&ui->target, badge_txt, badge_x + bw / 2, top_y + DP(4), FONT_XS, badge_col);

    // Top-Right Pill: jumps to AJUSTES (or shows active theme name when on AJUSTES)
    int btn_w = DP(68);
    int btn_h = DP(22);
    int btn_x = ui->target.width - pad - btn_w;
    int btn_y = top_y + DP(2);
    render_glass_card(&ui->target, btn_x, btn_y, btn_w, btn_h, DP(11), th->card_bg_top, th->card_bg_bot, th->card_border, 0);
    const char* top_btn_lbl = (ui->active_page == PAGE_SETTINGS) ? th->name : "AJUSTES";
    text_draw_centered(&ui->target, top_btn_lbl, btn_x + btn_w / 2, btn_y + DP(4), FONT_XS, th->accent_glow);
}

// Interactive 5-Tab Bottom Navigation Dock (INICIO | RED | SISTEMA | MOTOR C | AJUSTES)
static void render_bottom_navigation(UIController* ui, const Theme* th) {
    int pad = DP(16);
    int dock_w = ui->target.width - 2 * pad;
    int dock_h = DP(34);
    int dock_y = ui->target.height - DP(86);

    render_glass_card(&ui->target, pad, dock_y, dock_w, dock_h, DP(17), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    static const char* TAB_LABELS[PAGE_COUNT] = { "INICIO", "RED", "SISTEMA", "MOTOR C", "AJUSTES" };
    int tab_w = dock_w / PAGE_COUNT;

    int pill_x = pad + DP(2) + (int)(ui->page_scroll_x * (float)tab_w);
    int pill_w = tab_w - DP(4);
    render_rounded_rect(&ui->target, pill_x, dock_y + DP(4), pill_w, dock_h - DP(8), DP(13), color_alpha(th->accent, 45), th->accent, 1);

    for (int i = 0; i < PAGE_COUNT; i++) {
        int tx = pad + i * tab_w + tab_w / 2;
        uint32_t col = (i == (int)(ui->page_scroll_x + 0.5f)) ? th->text_primary : th->text_muted;
        text_draw_centered(&ui->target, TAB_LABELS[i], tx, dock_y + DP(10), FONT_XS, col);
    }
}

// PAGE 0: INICIO (ZERO-RAM ASCII CLOCK + USER ART + RINGS + SPARKLINE + ROOT DECK)
static void render_page_home(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(66);
    int gap = DP(8);

    // 1. Immersive Hero Card: User Artwork + Zero-RAM ASCII Clock + Governor Badge
    int hero_h = DP(114);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, hero_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int art_sz = DP(92);
    int art_x = page_x + pad + DP(11);
    int art_y = cur_y + DP(11);
    render_art_card(&ui->target, 0 + ui->art_offset, art_x, art_y, art_sz, art_sz, DP(14), th->accent);

    time_t raw_time;
    time(&raw_time);
    struct tm* tm_info = localtime(&raw_time);

    char time_hm[16], time_sec[8], ampm_str[8] = "";
    if (ui->use_24h_clock) {
        snprintf(time_hm, sizeof(time_hm), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
        snprintf(ampm_str, sizeof(ampm_str), "24H");
    } else {
        int h12 = tm_info->tm_hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(time_hm, sizeof(time_hm), "%d:%02d", h12, tm_info->tm_min);
        snprintf(ampm_str, sizeof(ampm_str), "%s", (tm_info->tm_hour >= 12) ? "PM" : "AM");
    }
    snprintf(time_sec, sizeof(time_sec), ":%02d", tm_info->tm_sec);

    int right_x = art_x + art_sz + DP(14);
    int clock_y = cur_y + DP(15);
    int cell_px = DP(4);
    if (cell_px < 4) cell_px = 4;

    // Render Zero-RAM Procedural ASCII Clock!
    int ascii_w = render_ascii_clock(&ui->target, time_hm, right_x, clock_y, cell_px,
                                     ui->ascii_clock_mode, th->text_primary, color_alpha(th->text_muted, 50));

    text_draw_tabular(&ui->target, ampm_str, right_x + ascii_w + DP(4), clock_y - DP(1), FONT_SM, th->accent_glow);
    text_draw_tabular(&ui->target, time_sec, right_x + ascii_w + DP(4), clock_y + DP(15), FONT_SM, th->text_muted);

    static const char* DAYS_ES[] = {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"};
    static const char* MONTHS_ES[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
    struct sysinfo si;
    long up_h = 4, up_m = 12;
    if (sysinfo(&si) == 0) {
        up_h = si.uptime / 3600;
        up_m = (si.uptime % 3600) / 60;
    }
    char date_str[64];
    snprintf(date_str, sizeof(date_str), "%s %02d %s  ·  %ldh %02ldm  ·  ASCII 0KB",
             DAYS_ES[tm_info->tm_wday], tm_info->tm_mday, MONTHS_ES[tm_info->tm_mon], up_h, up_m);
    text_draw(&ui->target, date_str, right_x, cur_y + DP(56), FONT_XS, th->text_secondary);

    char gov_pill[64];
    snprintf(gov_pill, sizeof(gov_pill), "%s  ·  %s  ·  %.1f°C",
             ui->bridge.soc_name, ui->bridge.cpu_governor, ui->cpu.temp_cpu_c);
    int gp_w = text_width(gov_pill, FONT_XS) + DP(14);
    render_rounded_rect(&ui->target, right_x, cur_y + DP(78), gp_w, DP(20), DP(6), th->surface_inset, th->card_border, 1);
    text_draw_centered(&ui->target, gov_pill, right_x + gp_w / 2, cur_y + DP(81), FONT_XS, th->accent);

    cur_y += hero_h + gap;

    // 2. Three Pure Visual Hardware Rings (CPU, RAM, BATTERY)
    int rings_h = DP(108);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, rings_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int ring_col_w = w / 3;
    int ring_r = DP(30);
    int ring_thick = DP(5);
    int ring_cy = cur_y + DP(44);

    int c1_x = page_x + pad + ring_col_w / 2;
    uint32_t cpu_col = (ui->anim_cpu_percent < 60.0f) ? th->success : (ui->anim_cpu_percent < 85.0f) ? th->warning : th->danger;
    render_ring(&ui->target, c1_x, ring_cy, ring_r, ring_thick, ui->anim_cpu_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, cpu_col, th->surface_inset);
    char cpu_txt[16], cpu_sub[24];
    snprintf(cpu_txt, sizeof(cpu_txt), "%d%%", (int)(ui->anim_cpu_percent + 0.5f));
    snprintf(cpu_sub, sizeof(cpu_sub), "CPU · %dMHz", ui->cpu.cores[0].cur_freq_mhz);
    text_draw_centered(&ui->target, cpu_txt, c1_x, ring_cy - DP(8), FONT_MD, th->text_primary);
    text_draw_centered(&ui->target, cpu_sub, c1_x, ring_cy + ring_r + DP(7), FONT_XS, th->text_secondary);

    int c2_x = page_x + pad + ring_col_w + ring_col_w / 2;
    uint32_t ram_col = (ui->anim_ram_percent < 75.0f) ? th->accent : th->warning;
    render_ring(&ui->target, c2_x, ring_cy, ring_r, ring_thick, ui->anim_ram_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, ram_col, th->surface_inset);
    char ram_txt[16], ram_sub[24];
    snprintf(ram_txt, sizeof(ram_txt), "%d%%", (int)(ui->anim_ram_percent + 0.5f));
    snprintf(ram_sub, sizeof(ram_sub), "RAM · %.1fG", ui->mem.used_mb / 1024.0f);
    text_draw_centered(&ui->target, ram_txt, c2_x, ring_cy - DP(8), FONT_MD, th->text_primary);
    text_draw_centered(&ui->target, ram_sub, c2_x, ring_cy + ring_r + DP(7), FONT_XS, th->text_secondary);

    int c3_x = page_x + pad + 2 * ring_col_w + ring_col_w / 2;
    uint32_t batt_col = (ui->anim_batt_percent > 20.0f) ? th->success : th->danger;
    render_ring(&ui->target, c3_x, ring_cy, ring_r, ring_thick, ui->anim_batt_percent / 100.0f, -PI_F * 0.5f, 2.0f * PI_F, batt_col, th->surface_inset);
    char batt_txt[16], batt_sub[24];
    snprintf(batt_txt, sizeof(batt_txt), "%d%%%s", (int)(ui->anim_batt_percent + 0.5f), ui->batt.is_charging ? "+" : "");
    snprintf(batt_sub, sizeof(batt_sub), "%.2f W", ui->batt.power_w);
    text_draw_centered(&ui->target, batt_txt, c3_x, ring_cy - DP(8), FONT_MD, th->text_primary);
    text_draw_centered(&ui->target, batt_sub, c3_x, ring_cy + ring_r + DP(7), FONT_XS, th->accent);

    cur_y += rings_h + gap;

    // 3. Visual Immersion + Live 60s Sparkline
    int vis_h = DP(104);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, vis_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int art2_sz = DP(82);
    render_art_card(&ui->target, 3 + ui->art_offset, page_x + pad + DP(11), cur_y + DP(11), art2_sz, art2_sz, DP(13), th->card_border);

    int sp_x = page_x + pad + DP(104);
    int sp_w = w - DP(116);
    char sp_badge[64];
    snprintf(sp_badge, sizeof(sp_badge), "SALUD %d%%  ·  LIBRE %.0f MB  ·  /data %.1f GB",
             (int)(ui->anim_health_score + 0.5f), ui->mem.free_mb, ui->mem.storage_free_gb);
    text_draw(&ui->target, sp_badge, sp_x, cur_y + DP(12), FONT_XS, th->text_secondary);

    render_dual_sparkline(&ui->target, sp_x, cur_y + DP(32), sp_w, DP(58),
                          ui->cpu.history, ui->mem.history, CPU_HISTORY_LEN, ui->cpu.history_idx, 100.0f,
                          th->success, th->accent);

    cur_y += vis_h + gap;

    // 4. Tactile Root / Shizuku 2x2 Control Deck
    int act_h = DP(178);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, act_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int bx = page_x + pad + DP(12);
    int bw_full = w - DP(24);
    int half_w = (bw_full - DP(8)) / 2;
    int bh = DP(42);
    int r1_y = cur_y + DP(12);
    int r2_y = r1_y + bh + DP(8);

    render_rounded_rect(&ui->target, bx, r1_y, half_w, bh, DP(11), color_alpha(th->accent, 36), th->accent, 1);
    text_draw_centered(&ui->target, "DROP CACHES & RAM", bx + half_w / 2, r1_y + DP(13), FONT_XS, th->accent_glow);

    int b2_x = bx + half_w + DP(8);
    uint32_t gov_col = (strcmp(ui->bridge.cpu_governor, "performance") == 0) ? th->warning : th->success;
    render_rounded_rect(&ui->target, b2_x, r1_y, half_w, bh, DP(11), color_alpha(gov_col, 32), gov_col, 1);
    char gov_btn[36];
    snprintf(gov_btn, sizeof(gov_btn), "GOV: %s", ui->bridge.cpu_governor);
    text_draw_centered(&ui->target, gov_btn, b2_x + half_w / 2, r1_y + DP(13), FONT_XS, gov_col);

    render_rounded_rect(&ui->target, bx, r2_y, half_w, bh, DP(11), color_alpha(th->success, 28), th->success, 1);
    text_draw_centered(&ui->target, "FSTRIM /data & CACHE", bx + half_w / 2, r2_y + DP(13), FONT_XS, th->success);

    bool is_turbo = (ui->bridge.anim_scale < 0.75f);
    uint32_t turbo_col = is_turbo ? th->warning : th->text_secondary;
    render_rounded_rect(&ui->target, b2_x, r2_y, half_w, bh, DP(11), color_alpha(turbo_col, 28), turbo_col, 1);
    char anim_lbl[32];
    snprintf(anim_lbl, sizeof(anim_lbl), "TURBO UI: %.1fx", ui->bridge.anim_scale);
    text_draw_centered(&ui->target, anim_lbl, b2_x + half_w / 2, r2_y + DP(13), FONT_XS, is_turbo ? th->warning : th->text_primary);

    int st_y = r2_y + bh + DP(10);
    render_rounded_rect(&ui->target, bx, st_y, bw_full, DP(44), DP(10), th->surface_inset, 0, 0);
    if (ui->bridge.last_action_msg[0] != '\0') {
        text_draw_centered(&ui->target, ui->bridge.last_action_msg, bx + bw_full / 2, st_y + DP(7), FONT_XS, th->success);
    } else if (ui->bridge.is_root) {
        text_draw_centered(&ui->target, "ROOT ACTIVO (su UID 0) · Control total del kernel", bx + bw_full / 2, st_y + DP(7), FONT_XS, th->success);
    } else if (ui->bridge.is_active) {
        text_draw_centered(&ui->target, "SHIZUKU / ADB ACTIVO (UID 2000) · Control del sistema", bx + bw_full / 2, st_y + DP(7), FONT_XS, th->success);
    } else {
        text_draw_centered(&ui->target, "Ve a AJUSTES para Re-Check Root o configurar Reloj ASCII", bx + bw_full / 2, st_y + DP(7), FONT_XS, th->text_secondary);
    }

    char sub_info[72];
    snprintf(sub_info, sizeof(sub_info), "%.2f W (%d mA)  ·  %.1f MB RAM App  ·  %.0f FPS",
             ui->batt.power_w, (int)ui->batt.current_ma, ui->engine.app_ram_anon_mb, ui->engine.fps);
    text_draw_centered(&ui->target, sub_info, bx + bw_full / 2, st_y + DP(24), FONT_XS, th->text_muted);
}

// PAGE 1: RED (CLEAN WI-FI SHOWCASE + 60S STREAM + SOCKETS)
static void render_page_network(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(66);
    int gap = DP(10);

    int speed_card_h = DP(122);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, speed_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int art_sz = DP(98);
    render_art_card(&ui->target, 2 + ui->art_offset, page_x + pad + DP(12), cur_y + DP(12), art_sz, art_sz, DP(14), th->accent);

    int s_cx = page_x + pad + DP(154);
    int s_cy = cur_y + DP(62);
    float ratio = (float)ui->net.link_speed_mbps / 300.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    render_ring(&ui->target, s_cx, s_cy, DP(34), DP(5), ratio, PI_F * 0.75f, PI_F * 1.5f, th->accent, th->surface_inset);
    char spd_txt[16];
    snprintf(spd_txt, sizeof(spd_txt), "%d", ui->net.link_speed_mbps);
    text_draw_centered(&ui->target, spd_txt, s_cx, s_cy - DP(17), FONT_XL, th->text_primary);
    text_draw_centered(&ui->target, "Mbps", s_cx, s_cy + DP(11), FONT_XS, th->text_muted);

    int meta_x = s_cx + DP(44);
    int meta_y = cur_y + DP(22);

    render_circle(&ui->target, meta_x + DP(3), meta_y + DP(7), DP(3), ui->net.is_wifi_connected ? th->success : th->danger);
    text_draw(&ui->target, ui->net.ssid, meta_x + DP(11), meta_y, FONT_SM, th->text_primary);

    meta_y += DP(24);
    render_icon_wifi(&ui->target, meta_x + DP(6), meta_y + DP(4), DP(12), ui->net.signal_level, th->accent, th->surface_inset);
    char rssi_buf[48];
    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm · %.8s", ui->net.rssi_dbm, ui->net.standard_str);
    text_draw(&ui->target, rssi_buf, meta_x + DP(18), meta_y, FONT_XS, th->text_secondary);

    meta_y += DP(22);
    text_draw(&ui->target, ui->net.local_ip, meta_x, meta_y, FONT_XS, th->accent);
    meta_y += DP(18);
    char gw_str[48];
    snprintf(gw_str, sizeof(gw_str), "GW %s", ui->net.gateway_ip);
    text_draw(&ui->target, gw_str, meta_x, meta_y, FONT_XS, th->text_muted);

    cur_y += speed_card_h + gap;

    int traf_card_h = DP(156);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, traf_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int rx_x = page_x + pad + DP(16);
    int tx_x = page_x + pad + w / 2 + DP(4);
    char rx_str[32], tx_str[32];
    if (ui->anim_rx_rate_kbps > 1024.0f) snprintf(rx_str, sizeof(rx_str), "%.2f MB/s", ui->anim_rx_rate_kbps / 1024.0f);
    else snprintf(rx_str, sizeof(rx_str), "%.1f KB/s", ui->anim_rx_rate_kbps);

    if (ui->anim_tx_rate_kbps > 1024.0f) snprintf(tx_str, sizeof(tx_str), "%.2f MB/s", ui->anim_tx_rate_kbps / 1024.0f);
    else snprintf(tx_str, sizeof(tx_str), "%.1f KB/s", ui->anim_tx_rate_kbps);

    render_icon_arrow_down(&ui->target, rx_x + DP(4), cur_y + DP(22), DP(11), th->success);
    text_draw(&ui->target, rx_str, rx_x + DP(15), cur_y + DP(13), FONT_LG, th->success);

    render_icon_arrow_up(&ui->target, tx_x + DP(4), cur_y + DP(22), DP(11), th->accent);
    text_draw(&ui->target, tx_str, tx_x + DP(15), cur_y + DP(13), FONT_LG, th->accent);

    char sess_str[64];
    snprintf(sess_str, sizeof(sess_str), "Sesion RX: %.1f MB   ·   TX: %.1f MB   ·   Pico: %.0f KB/s",
             (double)ui->net.session_rx_bytes / (1024.0 * 1024.0),
             (double)ui->net.session_tx_bytes / (1024.0 * 1024.0),
             ui->net.rx_peak_kbps > ui->net.tx_peak_kbps ? ui->net.rx_peak_kbps : ui->net.tx_peak_kbps);
    text_draw(&ui->target, sess_str, rx_x, cur_y + DP(42), FONT_XS, th->text_muted);

    float peak = ui->net.rx_peak_kbps > ui->net.tx_peak_kbps ? ui->net.rx_peak_kbps : ui->net.tx_peak_kbps;
    if (peak < 100.0f) peak = 100.0f;
    render_dual_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(64), w - DP(28), DP(78),
                          ui->net.rx_history, ui->net.tx_history, NET_HISTORY_LEN, ui->net.history_idx, peak,
                          th->success, th->accent);

    cur_y += traf_card_h + gap;

    int conn_card_h = DP(276);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, conn_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "SOCKETS ACTIVOS (/proc/net/tcp)", page_x + pad + DP(16), cur_y + DP(12), FONT_XS, th->text_muted);

    int c_row_y = cur_y + DP(34);
    int max_c = ui->bridge.conn_count < 6 ? ui->bridge.conn_count : 6;
    for (int i = 0; i < max_c; i++) {
        const ActiveConnEntry* ce = &ui->bridge.conns[i];
        bool is_estab = (strcmp(ce->state, "ESTAB") == 0);
        uint32_t st_col = is_estab ? th->success : th->accent;

        render_rounded_rect(&ui->target, page_x + pad + DP(12), c_row_y, w - DP(24), DP(32), DP(8), th->surface_inset, 0, 0);

        render_rounded_rect(&ui->target, page_x + pad + DP(16), c_row_y + DP(5), DP(56), DP(22), DP(5), color_alpha(st_col, 35), 0, 0);
        text_draw_centered(&ui->target, ce->service, page_x + pad + DP(44), c_row_y + DP(8), FONT_XS, st_col);

        char ep_str[64];
        snprintf(ep_str, sizeof(ep_str), ":%d -> %s", ce->local_port, ce->remote_addr);
        text_draw(&ui->target, ep_str, page_x + pad + DP(80), c_row_y + DP(8), FONT_XS, th->text_primary);

        text_draw_right(&ui->target, ce->state, page_x + pad + w - DP(20), c_row_y + DP(8), FONT_XS, st_col);
        c_row_y += DP(38);
    }
}

// PAGE 2: SISTEMA (8-CORE CPU + TANK/RAINBOW ART + THERMALS/BATTERY + STORAGE + PROCESS KILLER)
static void render_page_system(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(66);
    int gap = DP(8);

    int cores_card_h = DP(122);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, cores_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    char cpu_head[64];
    snprintf(cpu_head, sizeof(cpu_head), "%d CORES  ·  %d / %d MHz  ·  LOAD %.2f",
             ui->cpu.core_count, ui->cpu.cores[0].cur_freq_mhz, ui->cpu.cores[4].cur_freq_mhz, ui->cpu.load_1m);
    text_draw(&ui->target, cpu_head, page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_secondary);

    int gov_w = text_width(ui->bridge.cpu_governor, FONT_XS) + DP(16);
    int gov_x = page_x + pad + w - DP(12) - gov_w;
    render_rounded_rect(&ui->target, gov_x, cur_y + DP(7), gov_w, DP(18), DP(6), color_alpha(th->accent, 35), th->accent, 1);
    text_draw_centered(&ui->target, ui->bridge.cpu_governor, gov_x + gov_w / 2, cur_y + DP(9), FONT_XS, th->accent_glow);

    int num_cores = ui->cpu.core_count > 8 ? 8 : ui->cpu.core_count;
    if (num_cores <= 0) num_cores = 8;
    int bar_w = (w - DP(28) - (num_cores - 1) * DP(6)) / num_cores;
    int bar_max_h = DP(54);
    int bar_base_y = cur_y + DP(86);

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

    int tb_card_h = DP(104);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, tb_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int art_sz = DP(82);
    render_art_card(&ui->target, 1 + ui->art_offset, page_x + pad + DP(11), cur_y + DP(11), art_sz, art_sz, DP(13), th->card_border);

    int rx = page_x + pad + DP(104);
    int rw = w - DP(116);
    int col_w = rw / 4;
    float temps[4] = { ui->cpu.temp_cpu_c, ui->cpu.temp_gpu_c, ui->batt.temp_c, ui->cpu.temp_skin_c };
    const char* t_names[4] = { "CPU", "GPU", "BAT", "SKIN" };
    uint32_t t_cols[4] = { th->accent, th->warning, th->success, th->text_secondary };

    for (int i = 0; i < 4; i++) {
        int tx = rx + i * col_w + col_w / 2;
        char t_str[16];
        snprintf(t_str, sizeof(t_str), "%.1f°", temps[i]);
        text_draw_centered(&ui->target, t_str, tx, cur_y + DP(12), FONT_SM, t_cols[i]);
        text_draw_centered(&ui->target, t_names[i], tx, cur_y + DP(30), FONT_XS, th->text_muted);
    }

    render_rounded_rect(&ui->target, rx, cur_y + DP(50), rw, DP(42), DP(8), th->surface_inset, 0, 0);
    char b_line1[64], b_line2[64];
    snprintf(b_line1, sizeof(b_line1), "%.3f V  ·  %.0f mA  ·  %.2f W (%s)",
             ui->batt.voltage_v, ui->batt.current_ma, ui->batt.power_w, ui->batt.status_str);
    snprintf(b_line2, sizeof(b_line2), "%d / %d mAh  ·  ~%dh %02dm restante",
             ui->batt.charge_mah, ui->batt.full_mah, ui->batt.time_remaining_min / 60, ui->batt.time_remaining_min % 60);
    text_draw_centered(&ui->target, b_line1, rx + rw / 2, cur_y + DP(55), FONT_XS, th->text_primary);
    text_draw_centered(&ui->target, b_line2, rx + rw / 2, cur_y + DP(72), FONT_XS, th->text_muted);

    cur_y += tb_card_h + gap;

    int stor_card_h = DP(82);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, stor_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    char stor_stats[64];
    snprintf(stor_stats, sizeof(stor_stats), "/data: %.1f / %.1f GB (%.1f GB libre)",
             ui->mem.storage_used_gb, ui->mem.storage_total_gb, ui->mem.storage_free_gb);
    uint32_t stor_col = (ui->mem.storage_percent > 90.0f) ? th->danger : th->accent;
    text_draw(&ui->target, stor_stats, page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_primary);

    int seg_bar_w = w - DP(28);
    int seg_filled = (int)((ui->mem.storage_percent / 100.0f) * 16.0f + 0.5f);
    render_segmented_bar(&ui->target, page_x + pad + DP(14), cur_y + DP(29), seg_bar_w, DP(12), 16, seg_filled, stor_col, th->surface_inset, DP(3));

    char ram_detail[80];
    snprintf(ram_detail, sizeof(ram_detail), "RAM: %.0fM usada · %.0fM cache · %.0fM libre · ZRAM %.0f/%.0fM",
             ui->mem.used_mb, ui->mem.cached_mb, ui->mem.free_mb, ui->mem.swap_used_mb, ui->mem.swap_total_mb);
    text_draw(&ui->target, ram_detail, page_x + pad + DP(14), cur_y + DP(50), FONT_XS, th->text_secondary);

    cur_y += stor_card_h + gap;

    int proc_card_h = DP(204);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, proc_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "PROCESOS ACTIVOS (TOP CPU / RAM)", page_x + pad + DP(14), cur_y + DP(10), FONT_XS, th->text_muted);

    int p_row_y = cur_y + DP(30);
    int max_p = ui->bridge.proc_count < 4 ? ui->bridge.proc_count : 4;
    for (int i = 0; i < max_p; i++) {
        const TopProcEntry* pe = &ui->bridge.procs[i];
        render_rounded_rect(&ui->target, page_x + pad + DP(12), p_row_y, w - DP(24), DP(36), DP(8), th->surface_inset, 0, 0);

        char p_title[48];
        snprintf(p_title, sizeof(p_title), "%.24s", pe->name);
        text_draw(&ui->target, p_title, page_x + pad + DP(20), p_row_y + DP(4), FONT_XS, th->text_primary);

        char p_stats[64];
        snprintf(p_stats, sizeof(p_stats), "PID %d  ·  %.1f%% CPU  ·  %.0f MB", pe->pid, pe->cpu_pct, pe->rss_mb);
        text_draw(&ui->target, p_stats, page_x + pad + DP(20), p_row_y + DP(19), FONT_XS, th->text_muted);

        int k_w = DP(64);
        int k_x = page_x + pad + w - DP(18) - k_w;
        render_rounded_rect(&ui->target, k_x, p_row_y + DP(7), k_w, DP(22), DP(6), color_alpha(th->danger, 35), th->danger, 1);
        text_draw_centered(&ui->target, "KILL", k_x + k_w / 2, p_row_y + DP(10), FONT_XS, th->danger);

        p_row_y += DP(40);
    }
}

// PAGE 3: MOTOR C (FOREST ART SHOWCASE + C EFFICIENCY + NEON BENCHMARK)
static void render_page_motor_c(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(66);
    int gap = DP(8);

    int proof_card_h = DP(118);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, proof_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int art_sz = DP(94);
    render_art_card(&ui->target, 4 + ui->art_offset, page_x + pad + DP(12), cur_y + DP(12), art_sz, art_sz, DP(14), th->accent);

    int rx = page_x + pad + DP(118);
    int rw = w - DP(130);
    int col_w = rw / 3;
    int num_y = cur_y + DP(16);

    char app_cpu_txt[16];
    snprintf(app_cpu_txt, sizeof(app_cpu_txt), "%.1f%%", ui->engine.app_cpu_percent);
    text_draw_centered(&ui->target, app_cpu_txt, rx + col_w / 2, num_y, FONT_LG, th->success);
    text_draw_centered(&ui->target, "CPU APP", rx + col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    char app_ram_txt[16];
    snprintf(app_ram_txt, sizeof(app_ram_txt), "%.1fM", ui->engine.app_ram_anon_mb);
    text_draw_centered(&ui->target, app_ram_txt, rx + col_w + col_w / 2, num_y, FONT_LG, th->accent);
    text_draw_centered(&ui->target, "RAM C", rx + col_w + col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    text_draw_centered(&ui->target, "0", rx + 2 * col_w + col_w / 2, num_y, FONT_LG, th->text_primary);
    text_draw_centered(&ui->target, "ALLOCS", rx + 2 * col_w + col_w / 2, num_y + DP(26), FONT_XS, th->text_muted);

    char bin_badge[64];
    snprintf(bin_badge, sizeof(bin_badge), ".so %d KB  ·  Boot %d ms  ·  0 GC",
             ui->engine.binary_so_kb, ui->engine.cold_boot_ms);
    render_rounded_rect(&ui->target, rx, cur_y + DP(72), rw, DP(28), DP(8), th->surface_inset, th->accent, 1);
    text_draw_centered(&ui->target, bin_badge, rx + rw / 2, cur_y + DP(79), FONT_XS, th->accent_glow);

    cur_y += proof_card_h + gap;

    int frame_card_h = DP(94);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, frame_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    char frame_stats[80];
    snprintf(frame_stats, sizeof(frame_stats), "%.0f FPS  ·  Render %.1f ms  ·  p50 %.1f ms  ·  p99 %.1f ms",
             ui->engine.fps,
             ui->engine.frame_time_us / 1000.0f,
             ui->engine.frame_p50_us / 1000.0f,
             ui->engine.frame_p99_us / 1000.0f);
    text_draw(&ui->target, frame_stats, page_x + pad + DP(14), cur_y + DP(12), FONT_XS, th->text_primary);

    render_sparkline(&ui->target, page_x + pad + DP(14), cur_y + DP(34), w - DP(28), DP(46),
                     ui->engine.frame_history, FRAME_HISTORY_LEN, ui->engine.frame_history_idx, 0.0f, 16600.0f,
                     th->accent, color_alpha(th->accent, 35));

    cur_y += frame_card_h + gap;

    int bench_card_h = DP(308);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, bench_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int bx = page_x + pad + DP(14);
    int btn_w = w - DP(28);
    int by = cur_y + DP(14);

    if (ui->bench.is_running) {
        render_rounded_rect(&ui->target, bx, by, btn_w, DP(36), DP(10), th->surface_inset, 0, 0);
        int fill = (int)(ui->bench.progress * (float)btn_w);
        render_rounded_rect(&ui->target, bx, by, fill, DP(36), DP(10), th->accent, 0, 0);
        text_draw_centered(&ui->target, "EJECUTANDO SIMD NEON...", page_x + pad + w / 2, by + DP(10), FONT_SM, th->text_primary);
    } else {
        render_rounded_rect(&ui->target, bx, by, btn_w, DP(36), DP(10), color_alpha(th->accent, 35), th->accent, 1);
        text_draw_centered(&ui->target, "BENCHMARK ARM64 NEON (2s)", bx + btn_w / 2, by + DP(10), FONT_SM, th->accent_glow);
    }

    int row_y = by + DP(50);
    struct {
        const char* label;
        float val;
        float ref;
        const char* unit;
        uint32_t col;
    } items[4] = {
        { "Raster NEON",     ui->bench.pixel_fill_mpix_s, ui->bench.ref_pixel_mpix_s,     "MPix/s", th->success },
        { "Ancho Banda RAM", ui->bench.memcpy_gb_s,       ui->bench.ref_memcpy_gb_s,      "GB/s",   th->accent },
        { "Criba Primos C",  ui->bench.primes_mprimes_s,  ui->bench.ref_primes_mprimes_s, "M/s",    th->warning },
        { "SIMD FMA 128b",   ui->bench.neon_gflops,       ui->bench.ref_neon_gflops,      "GFLOPS", th->accent_glow }
    };

    for (int i = 0; i < 4; i++) {
        text_draw(&ui->target, items[i].label, bx, row_y, FONT_XS, th->text_secondary);
        char v_str[48];
        snprintf(v_str, sizeof(v_str), "%.1f %s", items[i].val, items[i].unit);
        text_draw_right(&ui->target, v_str, bx + btn_w, row_y, FONT_XS, th->text_primary);

        float ratio = items[i].val / (items[i].ref * 1.25f);
        if (ratio > 1.0f) ratio = 1.0f;
        if (ratio < 0.05f) ratio = 0.05f;
        int fill_w = (int)(ratio * (float)btn_w);
        render_rounded_rect(&ui->target, bx, row_y + DP(17), btn_w, DP(11), DP(5), th->surface_inset, 0, 0);
        render_rounded_rect(&ui->target, bx, row_y + DP(17), fill_w, DP(11), DP(5), items[i].col, 0, 0);

        row_y += DP(46);
    }

    int sc_y = cur_y + bench_card_h - DP(48);
    render_rounded_rect(&ui->target, bx, sc_y, btn_w, DP(36), DP(9), th->surface_inset, th->card_border, 1);
    text_draw(&ui->target, "SCORE ARM64 C/NEON", bx + DP(12), sc_y + DP(10), FONT_XS, th->text_secondary);
    char tot_str[32];
    snprintf(tot_str, sizeof(tot_str), "%d PTS", ui->bench.total_score);
    text_draw_right(&ui->target, tot_str, bx + btn_w - DP(12), sc_y + DP(8), FONT_MD, th->success);
}

// PAGE 4: AJUSTES (ROOT RE-CHECK ZONE + ASCII CLOCK & STABILITY SETTINGS)
static void render_page_settings(UIController* ui, const Theme* th, int page_x) {
    int pad = DP(16);
    int w = ui->target.width - 2 * pad;
    int cur_y = DP(66);
    int gap = DP(10);

    // 1. Root (su) Verification & Re-Check Zone
    int root_card_h = DP(192);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, root_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);

    int bx = page_x + pad + DP(14);
    int bw = w - DP(28);

    uint32_t st_col = ui->bridge.is_root ? th->success : (ui->bridge.is_active ? th->accent : th->warning);
    const char* st_title = ui->bridge.is_root ? "ESTADO: ROOT ACTIVO (UID 0 · KERNEL)"
                         : (ui->bridge.is_active ? "ESTADO: PUENTE SHIZUKU / ADB (UID 2000)"
                                                 : "ESTADO: MODO SANDBOX (SIN ROOT ACTIVO)");
    render_circle(&ui->target, bx + DP(5), cur_y + DP(19), DP(4), st_col);
    text_draw(&ui->target, st_title, bx + DP(15), cur_y + DP(12), FONT_SM, st_col);

    char bin_line[80];
    snprintf(bin_line, sizeof(bin_line), "Binario su: %s   ·   Governor: %s",
             ui->bridge.root_bin_path, ui->bridge.cpu_governor);
    text_draw(&ui->target, bin_line, bx, cur_y + DP(35), FONT_XS, th->text_secondary);

    render_rounded_rect(&ui->target, bx, cur_y + DP(54), bw, DP(24), DP(6), th->surface_inset, 0, 0);
    text_draw_centered(&ui->target, ui->bridge.root_diag_msg, bx + bw / 2, cur_y + DP(59), FONT_XS, th->text_primary);

    // Big Interactive Button: RE-CHECK ROOT
    int btn_recheck_y = cur_y + DP(86);
    render_rounded_rect(&ui->target, bx, btn_recheck_y, bw, DP(42), DP(11), color_alpha(th->success, 42), th->success, 1);
    text_draw_centered(&ui->target, "RE-CHECK ROOT (FORZAR VERIFICACION SU)", bx + bw / 2, btn_recheck_y + DP(13), FONT_SM, th->success);

    // Quick Root Test Row: Governor + Drop Caches
    int row_y = btn_recheck_y + DP(50);
    int half_w = (bw - DP(8)) / 2;
    render_rounded_rect(&ui->target, bx, row_y, half_w, DP(38), DP(9), color_alpha(th->accent, 30), th->accent, 1);
    char gov_txt[36];
    snprintf(gov_txt, sizeof(gov_txt), "CAMBIAR GOV (%s)", ui->bridge.cpu_governor);
    text_draw_centered(&ui->target, gov_txt, bx + half_w / 2, row_y + DP(11), FONT_XS, th->accent_glow);

    render_rounded_rect(&ui->target, bx + half_w + DP(8), row_y, half_w, DP(38), DP(9), color_alpha(th->accent, 30), th->accent, 1);
    text_draw_centered(&ui->target, "DROP CACHES KERNEL", bx + half_w + DP(8) + half_w / 2, row_y + DP(11), FONT_XS, th->accent_glow);

    cur_y += root_card_h + gap;

    // 2. ASCII Clock & Time Format Configuration (Zero-RAM Optimization)
    int clk_card_h = DP(144);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, clk_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "RELOJ ASCII (0 KB RAM) & FORMATO DE HORA", bx, cur_y + DP(11), FONT_XS, th->text_muted);

    static const char* ASCII_MODE_NAMES[3] = {
        "ESTILO RELOJ ASCII: [ # ] TERMINAL MATRIX",
        "ESTILO RELOJ ASCII: [ █ ] CYBER BLOCK",
        "ESTILO RELOJ ASCII: [ 1/0 ] MATRIZ BINARIA"
    };
    int b_ascii_y = cur_y + DP(32);
    render_rounded_rect(&ui->target, bx, b_ascii_y, bw, DP(42), DP(10), color_alpha(th->accent, 35), th->accent, 1);
    text_draw_centered(&ui->target, ASCII_MODE_NAMES[ui->ascii_clock_mode % 3], bx + bw / 2, b_ascii_y + DP(13), FONT_XS, th->accent_glow);

    int b_fmt_y = b_ascii_y + DP(50);
    const char* fmt_lbl = ui->use_24h_clock
        ? "FORMATO HORA: 24 HORAS (TOCA PARA 12H AM/PM)"
        : "FORMATO HORA: 12 HORAS AM/PM (SINCRONIZADO)";
    render_rounded_rect(&ui->target, bx, b_fmt_y, bw, DP(42), DP(10), color_alpha(th->success, 30), th->success, 1);
    text_draw_centered(&ui->target, fmt_lbl, bx + bw / 2, b_fmt_y + DP(13), FONT_XS, th->success);

    cur_y += clk_card_h + gap;

    // 3. Stability, Memory & Visual Customization
    int stab_card_h = DP(188);
    render_glass_card(&ui->target, page_x + pad, cur_y, w, stab_card_h, DP(18), th->card_bg_top, th->card_bg_bot, th->card_border, th->card_shadow);
    text_draw(&ui->target, "ESTABILIDAD, CONSUMO RAM & APARIENCIA", bx, cur_y + DP(11), FONT_XS, th->text_muted);

    int s_r1_y = cur_y + DP(32);
    uint32_t fps_col = ui->power_save_mode ? th->warning : th->success;
    render_rounded_rect(&ui->target, bx, s_r1_y, half_w, DP(42), DP(10), color_alpha(fps_col, 32), fps_col, 1);
    text_draw_centered(&ui->target, ui->power_save_mode ? "FPS: 30 HZ ECO" : "FPS: 60 HZ FLUIDO",
                       bx + half_w / 2, s_r1_y + DP(13), FONT_XS, fps_col);

    render_rounded_rect(&ui->target, bx + half_w + DP(8), s_r1_y, half_w, DP(42), DP(10), color_alpha(th->accent, 32), th->accent, 1);
    char art_lbl[36];
    snprintf(art_lbl, sizeof(art_lbl), "ARTE ACTIVO: SET #%d", (ui->art_offset % 5) + 1);
    text_draw_centered(&ui->target, art_lbl, bx + half_w + DP(8) + half_w / 2, s_r1_y + DP(13), FONT_XS, th->accent_glow);

    int s_r2_y = s_r1_y + DP(50);
    char th_lbl[64];
    snprintf(th_lbl, sizeof(th_lbl), "CAMBIAR TEMA VISUAL (ACTUAL: %s)", th->name);
    render_rounded_rect(&ui->target, bx, s_r2_y, bw, DP(42), DP(10), color_alpha(th->accent, 35), th->accent, 1);
    text_draw_centered(&ui->target, th_lbl, bx + bw / 2, s_r2_y + DP(13), FONT_XS, th->text_primary);

    int st_foot_y = s_r2_y + DP(50);
    render_rounded_rect(&ui->target, bx, st_foot_y, bw, DP(34), DP(8), th->surface_inset, 0, 0);
    char stab_info[80];
    snprintf(stab_info, sizeof(stab_info), "RAM Propia: %.1f MB  ·  Reloj ASCII: 0 KB  ·  SIGPIPE Guard: ON",
             ui->engine.app_ram_anon_mb);
    text_draw_centered(&ui->target, stab_info, bx + bw / 2, st_foot_y + DP(10), FONT_XS, th->text_secondary);
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
            case PAGE_HOME:     render_page_home(ui, th, page_x);     break;
            case PAGE_NETWORK:  render_page_network(ui, th, page_x);  break;
            case PAGE_SYSTEM:   render_page_system(ui, th, page_x);   break;
            case PAGE_MOTOR_C:  render_page_motor_c(ui, th, page_x);  break;
            case PAGE_SETTINGS: render_page_settings(ui, th, page_x); break;
            default: break;
        }
    }

    render_top_bar(ui, th);
    render_bottom_navigation(ui, th);
}
