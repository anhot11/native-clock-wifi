#include "ui_renderer.h"
#include "tt_font.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/sysinfo.h>

#define PI 3.14159265358979323846f

// RGBA 8888 Little Endian
#define RGBA(r, g, b, a) (((uint32_t)(r) & 0xFF) | (((uint32_t)(g) & 0xFF) << 8) | (((uint32_t)(b) & 0xFF) << 16) | (((uint32_t)(a) & 0xFF) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 255)
#define DP(v) ((int)((float)(v) * ui->dp_scale))

static const UITheme THEMES[] = {
    { // 0: Obsidian Luxe (Deep black, cyan accent, crisp typography)
        .bg_color       = RGB(10, 11, 14),
        .card_bg        = RGB(17, 19, 24),
        .card_border    = RGB(32, 36, 46),
        .card_inner_bg  = RGB(23, 26, 34),
        .text_primary   = RGB(248, 250, 252),
        .text_secondary = RGB(156, 163, 175),
        .text_muted     = RGB(85, 93, 108),
        .accent         = RGB(14, 165, 233),   // Sky blue
        .accent_glow    = RGB(56, 189, 248),
        .success        = RGB(34, 197, 94),    // Green
        .warning        = RGB(245, 158, 11),   // Amber
        .danger         = RGB(239, 68, 68),    // Red
        .theme_name     = "Obsidian Sky"
    },
    { // 1: Emerald Studio (Dark charcoal, sleek emerald green)
        .bg_color       = RGB(8, 14, 12),
        .card_bg        = RGB(14, 23, 20),
        .card_border    = RGB(24, 43, 36),
        .card_inner_bg  = RGB(19, 32, 28),
        .text_primary   = RGB(240, 253, 244),
        .text_secondary = RGB(167, 243, 208),
        .text_muted     = RGB(74, 120, 100),
        .accent         = RGB(16, 185, 129),   // Emerald
        .accent_glow    = RGB(52, 211, 153),
        .success        = RGB(16, 185, 129),
        .warning        = RGB(234, 179, 8),
        .danger         = RGB(244, 63, 94),
        .theme_name     = "Emerald Studio"
    },
    { // 2: Nordic Indigo (Clean Scandinavian dark indigo)
        .bg_color       = RGB(11, 12, 20),
        .card_bg        = RGB(19, 21, 35),
        .card_border    = RGB(38, 42, 68),
        .card_inner_bg  = RGB(26, 29, 48),
        .text_primary   = RGB(248, 249, 255),
        .text_secondary = RGB(165, 175, 214),
        .text_muted     = RGB(92, 99, 136),
        .accent         = RGB(99, 102, 241),   // Indigo
        .accent_glow    = RGB(129, 140, 248),
        .success        = RGB(34, 197, 94),
        .warning        = RGB(251, 191, 36),
        .danger         = RGB(244, 63, 94),
        .theme_name     = "Nordic Indigo"
    },
    { // 3: Pure Titanium (Monochrome luxury)
        .bg_color       = RGB(13, 13, 15),
        .card_bg        = RGB(21, 21, 24),
        .card_border    = RGB(42, 42, 48),
        .card_inner_bg  = RGB(30, 30, 35),
        .text_primary   = RGB(255, 255, 255),
        .text_secondary = RGB(180, 180, 190),
        .text_muted     = RGB(110, 110, 120),
        .accent         = RGB(225, 225, 235),
        .accent_glow    = RGB(255, 255, 255),
        .success        = RGB(74, 222, 128),
        .warning        = RGB(250, 204, 21),
        .danger         = RGB(248, 113, 113),
        .theme_name     = "Titanium Mono"
    }
};

static const int NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

void ui_renderer_init(UIRenderer* ui) {
    memset(ui, 0, sizeof(UIRenderer));
    ui->theme_index = 0;
    ui->anim_phase = 0.0f;
    ui->touch_x = -1;
    ui->touch_y = -1;
    ui->touch_ripple = 0.0f;
    ui->fps = 60;
    ui->dp_scale = 2.0f;
    ui->fonts_initialized = false;
}

void ui_renderer_set_buffer(UIRenderer* ui, ANativeWindow_Buffer* buffer) {
    if (!buffer) {
        ui->pixels = NULL;
        ui->width = 0;
        ui->height = 0;
        ui->stride = 0;
        return;
    }
    ui->width = buffer->width;
    ui->height = buffer->height;
    ui->stride = buffer->stride;
    ui->pixels = (uint32_t*)buffer->bits;

    // Mobile DP scale: standard 360dp width baseline
    float calc_scale = (float)ui->width / 360.0f;
    if (calc_scale < 1.2f) calc_scale = 1.2f;

    if (!ui->fonts_initialized || fabsf(ui->dp_scale - calc_scale) > 0.1f) {
        ui->dp_scale = calc_scale;
        tt_font_cleanup();
        tt_font_init(ui->dp_scale);
        ui->fonts_initialized = true;
    }
}

void ui_renderer_cycle_theme(UIRenderer* ui) {
    ui->theme_index = (ui->theme_index + 1) % NUM_THEMES;
}

void ui_renderer_on_touch(UIRenderer* ui, int x, int y) {
    ui->touch_x = x;
    ui->touch_y = y;
    ui->touch_ripple = 1.0f;
    ui_renderer_cycle_theme(ui);
}

static inline void put_pixel_blend(UIRenderer* ui, int x, int y, uint32_t color) {
    if (x < 0 || x >= ui->width || y < 0 || y >= ui->height) return;
    uint32_t* p = &ui->pixels[y * ui->stride + x];
    uint32_t sa = (color >> 24) & 0xFF;
    if (sa == 255) {
        *p = color;
        return;
    }
    if (sa == 0) return;

    uint32_t dst = *p;
    uint32_t sr = color & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = (color >> 16) & 0xFF;

    uint32_t dr = dst & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = (dst >> 16) & 0xFF;

    uint32_t r = (sr * sa + dr * (255 - sa)) / 255;
    uint32_t g = (sg * sa + dg * (255 - sa)) / 255;
    uint32_t b = (sb * sa + db * (255 - sa)) / 255;

    *p = RGBA(r, g, b, 255);
}

static void fill_rect(UIRenderer* ui, int x0, int y0, int w, int h, uint32_t color) {
    if (x0 >= ui->width || y0 >= ui->height || w <= 0 || h <= 0) return;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ui->width) x1 = ui->width;
    if (y1 > ui->height) y1 = ui->height;

    uint32_t a = (color >> 24) & 0xFF;
    if (a == 255) {
        for (int y = y0; y < y1; y++) {
            uint32_t* row = &ui->pixels[y * ui->stride + x0];
            for (int x = x0; x < x1; x++) {
                *row++ = color;
            }
        }
    } else {
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                put_pixel_blend(ui, x, y, color);
            }
        }
    }
}

static void draw_rounded_card(UIRenderer* ui, int x, int y, int w, int h, int radius, uint32_t bg, uint32_t border) {
    if (radius > h / 2) radius = h / 2;
    if (radius > w / 2) radius = w / 2;

    fill_rect(ui, x + radius, y, w - 2 * radius, h, bg);
    fill_rect(ui, x, y + radius, radius, h - 2 * radius, bg);
    fill_rect(ui, x + w - radius, y + radius, radius, h - 2 * radius, bg);

    for (int cy = 0; cy < radius; cy++) {
        for (int cx = 0; cx < radius; cx++) {
            int dx = radius - 1 - cx;
            int dy = radius - 1 - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                put_pixel_blend(ui, x + cx, y + cy, bg);
                put_pixel_blend(ui, x + w - 1 - cx, y + cy, bg);
                put_pixel_blend(ui, x + cx, y + h - 1 - cy, bg);
                put_pixel_blend(ui, x + w - 1 - cx, y + h - 1 - cy, bg);
            }
        }
    }

    if (border != 0) {
        fill_rect(ui, x + radius, y, w - 2 * radius, 1, border);
        fill_rect(ui, x + radius, y + h - 1, w - 2 * radius, 1, border);
        fill_rect(ui, x, y + radius, 1, h - 2 * radius, border);
        fill_rect(ui, x + w - 1, y + radius, 1, h - 2 * radius, border);
    }
}

static void draw_smooth_pill(UIRenderer* ui, int x, int y, int w, int h, uint32_t bg, uint32_t border) {
    int radius = h / 2;
    draw_rounded_card(ui, x, y, w, h, radius, bg, border);
}

static void draw_circle(UIRenderer* ui, int cx, int cy, int radius, uint32_t color) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                put_pixel_blend(ui, x, y, color);
            }
        }
    }
}

static void draw_minimal_speed_arc(UIRenderer* ui, int cx, int cy, int radius, int thickness, float percent, uint32_t active_color, uint32_t track_color) {
    float start_angle = 135.0f * (PI / 180.0f);
    float sweep = 270.0f * (PI / 180.0f);
    float current_angle = start_angle + sweep * percent;

    for (int y = cy - radius - 1; y <= cy + radius + 1; y++) {
        for (int x = cx - radius - 1; x <= cx + radius + 1; x++) {
            int dx = x - cx;
            int dy = y - cy;
            float dist = sqrtf((float)(dx * dx + dy * dy));
            if (dist >= radius - thickness && dist <= radius) {
                float angle = atan2f((float)dy, (float)dx);
                if (angle < 0.0f) angle += 2.0f * PI;

                float test_angle = angle;
                if (test_angle < start_angle - 0.05f) test_angle += 2.0f * PI;

                if (test_angle >= start_angle && test_angle <= start_angle + sweep) {
                    if (test_angle <= current_angle) {
                        put_pixel_blend(ui, x, y, active_color);
                    } else {
                        put_pixel_blend(ui, x, y, track_color);
                    }
                }
            }
        }
    }
}

static void draw_minimal_signal_bars(UIRenderer* ui, int x, int y, int bar_w, int bar_max_h, int level, uint32_t active_color, uint32_t inactive_color) {
    int spacing = DP(2);
    for (int i = 0; i < 4; i++) {
        int bar_h = (bar_max_h * (i + 1)) / 4;
        int bx = x + i * (bar_w + spacing);
        int by = y + (bar_max_h - bar_h);
        uint32_t col = (i < level) ? active_color : inactive_color;
        draw_rounded_card(ui, bx, by, bar_w, bar_h, DP(1), col, 0);
    }
}

static void draw_smooth_sparkline(UIRenderer* ui, int x, int y, int w, int h, const float* history, int count, int cur_idx, uint32_t line_color, uint32_t fill_color) {
    if (count <= 1 || w <= 0 || h <= 0) return;

    float max_val = 50.0f;
    for (int i = 0; i < count; i++) {
        if (history[i] > max_val) max_val = history[i];
    }

    float step_x = (float)w / (float)(count - 1);
    for (int i = 0; i < count; i++) {
        int idx = (cur_idx + i) % count;
        float val = history[idx];
        int px = x + (int)(i * step_x);
        int py = y + h - (int)((val / max_val) * (h - DP(4)));
        if (py < y) py = y;
        if (py > y + h) py = y + h;

        fill_rect(ui, px, py, (int)step_x + 1, (y + h) - py, fill_color);
        fill_rect(ui, px, py, (int)step_x + 1, DP(2), line_color);
    }
}

void ui_renderer_draw_frame(UIRenderer* ui, const WifiStats* wifi, float dt) {
    if (!ui->pixels || ui->width <= 0 || ui->height <= 0) return;

    const UITheme* th = &THEMES[ui->theme_index];
    ui->anim_phase += dt * 2.0f;
    if (ui->anim_phase > 2.0f * PI) ui->anim_phase -= 2.0f * PI;

    if (ui->touch_ripple > 0.0f) {
        ui->touch_ripple -= dt * 2.5f;
        if (ui->touch_ripple < 0.0f) ui->touch_ripple = 0.0f;
    }

    // 1. Minimal Canvas Background
    fill_rect(ui, 0, 0, ui->width, ui->height, th->bg_color);

    // Dynamic Mobile Layout Calculations (DP scaled)
    int pad = DP(16);
    int safe_top = DP(36); // Space for Android camera cutout / status bar
    int content_w = ui->width - 2 * pad;
    int card_radius = DP(16);
    int card_gap = DP(12);

    // 2. Minimalist Header: Title + Theme Pill Button
    int header_y = safe_top;
    tt_font_draw(ui, "MONITOR NATIVO", pad, header_y + DP(2), TT_SIZE_LG, th->text_primary);
    
    char sub_app[48];
    snprintf(sub_app, sizeof(sub_app), "Android 11-16  |  %s", th->theme_name);
    tt_font_draw(ui, sub_app, pad, header_y + DP(22), TT_SIZE_XS, th->text_muted);

    // Theme selector pill (Top Right)
    int btn_w = DP(76);
    int btn_h = DP(24);
    int btn_x = ui->width - pad - btn_w;
    int btn_y = header_y + DP(4);
    draw_smooth_pill(ui, btn_x, btn_y, btn_w, btn_h, th->card_bg, th->card_border);
    tt_font_draw_centered(ui, "TEMA", btn_x + btn_w / 2, btn_y + DP(5), TT_SIZE_XS, th->text_secondary);

    int cur_y = header_y + DP(46);

    // 3. Minimalist Quick-Status Metric Row (Ping, Battery, RAM)
    int pill_gap = DP(8);
    int pill_w = (content_w - 2 * pill_gap) / 3;
    int pill_h = DP(34);

    // Metric 1: Ping / Latency
    draw_rounded_card(ui, pad, cur_y, pill_w, pill_h, DP(10), th->card_bg, th->card_border);
    tt_font_draw(ui, "LATENCIA", pad + DP(8), cur_y + DP(4), TT_SIZE_XS, th->text_muted);
    char ping_str[24];
    if (wifi->ping_ms >= 0) snprintf(ping_str, sizeof(ping_str), "%d ms", wifi->ping_ms);
    else snprintf(ping_str, sizeof(ping_str), "-- ms");
    uint32_t ping_color = (wifi->ping_ms < 50 && wifi->ping_ms >= 0) ? th->success : (wifi->ping_ms < 150) ? th->warning : th->danger;
    tt_font_draw(ui, ping_str, pad + DP(8), cur_y + DP(16), TT_SIZE_SM, ping_color);

    // Metric 2: Battery Level
    int b_x = pad + pill_w + pill_gap;
    draw_rounded_card(ui, b_x, cur_y, pill_w, pill_h, DP(10), th->card_bg, th->card_border);
    tt_font_draw(ui, "BATERIA", b_x + DP(8), cur_y + DP(4), TT_SIZE_XS, th->text_muted);
    char bat_str[24];
    snprintf(bat_str, sizeof(bat_str), "%d%%%s", wifi->battery_level, wifi->is_charging ? " +" : "");
    uint32_t bat_color = (wifi->battery_level > 20) ? th->text_primary : th->danger;
    tt_font_draw(ui, bat_str, b_x + DP(8), cur_y + DP(16), TT_SIZE_SM, bat_color);

    // Metric 3: RAM Usage
    int r_x = b_x + pill_w + pill_gap;
    draw_rounded_card(ui, r_x, cur_y, pill_w, pill_h, DP(10), th->card_bg, th->card_border);
    tt_font_draw(ui, "MEMORIA RAM", r_x + DP(8), cur_y + DP(4), TT_SIZE_XS, th->text_muted);
    char ram_str[24];
    snprintf(ram_str, sizeof(ram_str), "%d%% (%.1fG)", wifi->ram_percent, wifi->ram_used_gb);
    tt_font_draw(ui, ram_str, r_x + DP(8), cur_y + DP(16), TT_SIZE_SM, th->accent);

    cur_y += pill_h + card_gap;

    // 4. HERO SECTION: RELOJ DIGITAL MINIMALISTA (TrueType Roboto Typography)
    time_t raw_time;
    time(&raw_time);
    struct tm* tm_info = localtime(&raw_time);

    struct sysinfo si;
    sysinfo(&si);
    int up_h = si.uptime / 3600;
    int up_m = (si.uptime % 3600) / 60;

    static const char* MONTHS_ES[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
    static const char* DAYS_ES[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};
    char date_str[64];
    snprintf(date_str, sizeof(date_str), "%s, %02d de %s %04d",
             DAYS_ES[tm_info->tm_wday], tm_info->tm_mday, MONTHS_ES[tm_info->tm_mon], tm_info->tm_year + 1900);

    int clock_card_h = DP(122);
    draw_rounded_card(ui, pad, cur_y, content_w, clock_card_h, card_radius, th->card_bg, th->card_border);
    
    // Header tag
    tt_font_draw(ui, "RELOJ PRINCIPAL", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->text_muted);

    // Modern clean digital time using TrueType XXL
    char time_hm[16];
    char time_s[8];
    snprintf(time_hm, sizeof(time_hm), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    snprintf(time_s, sizeof(time_s), ":%02d", tm_info->tm_sec);

    int hm_w = tt_font_get_width(time_hm, TT_SIZE_XXL);
    int s_w = tt_font_get_width(time_s, TT_SIZE_LG);
    int total_time_w = hm_w + s_w + DP(2);
    int time_start_x = pad + (content_w - total_time_w) / 2;
    int time_y = cur_y + DP(26);

    tt_font_draw(ui, time_hm, time_start_x, time_y, TT_SIZE_XXL, th->text_primary);
    tt_font_draw(ui, time_s, time_start_x + hm_w + DP(2), time_y + DP(14), TT_SIZE_LG, th->text_secondary);

    // Elegant date display
    tt_font_draw_centered(ui, date_str, pad + content_w / 2, cur_y + DP(78), TT_SIZE_MD, th->text_primary);

    // Minimal uptime / timezone
    char meta_uptime[64];
    snprintf(meta_uptime, sizeof(meta_uptime), "Activo: %02dh %02dm  |  Zona: %s", up_h, up_m, tm_info->tm_zone ? tm_info->tm_zone : "Local");
    tt_font_draw_centered(ui, meta_uptime, pad + content_w / 2, cur_y + DP(100), TT_SIZE_XS, th->text_muted);

    cur_y += clock_card_h + card_gap;

    // 5. CARD: ESTADO Y VELOCIDAD WI-FI (Minimalist Arc & Details)
    int wifi_card_h = DP(150);
    draw_rounded_card(ui, pad, cur_y, content_w, wifi_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "RED WI-FI & ENLACE", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->text_muted);

    // Connection badge (Top Right)
    if (wifi->is_connected) {
        int badge_w = DP(70);
        int badge_h = DP(18);
        draw_smooth_pill(ui, pad + content_w - badge_w - DP(12), cur_y + DP(8), badge_w, badge_h, th->card_inner_bg, th->success);
        tt_font_draw_centered(ui, "CONECTADO", pad + content_w - badge_w / 2 - DP(12), cur_y + DP(10), TT_SIZE_XS, th->success);
    } else {
        int badge_w = DP(84);
        int badge_h = DP(18);
        draw_smooth_pill(ui, pad + content_w - badge_w - DP(12), cur_y + DP(8), badge_w, badge_h, th->card_inner_bg, th->danger);
        tt_font_draw_centered(ui, "DESCONECTADO", pad + content_w - badge_w / 2 - DP(12), cur_y + DP(10), TT_SIZE_XS, th->danger);
    }

    // Elegant Minimal Gauge on the left
    int gauge_r = DP(38);
    int gauge_cx = pad + gauge_r + DP(14);
    int gauge_cy = cur_y + DP(72);

    float max_speed_f = (wifi->max_tx_speed_mbps > 0) ? (float)wifi->max_tx_speed_mbps : 150.0f;
    if (max_speed_f < 100.0f) max_speed_f = 100.0f;
    float cur_speed_f = (float)wifi->link_speed_mbps;
    float speed_ratio = cur_speed_f / max_speed_f;
    if (speed_ratio > 1.0f) speed_ratio = 1.0f;
    if (speed_ratio < 0.0f) speed_ratio = 0.0f;

    draw_minimal_speed_arc(ui, gauge_cx, gauge_cy, gauge_r, DP(5), speed_ratio, th->accent, th->card_border);

    char speed_num[16];
    snprintf(speed_num, sizeof(speed_num), "%d", wifi->link_speed_mbps);
    tt_font_draw_centered(ui, speed_num, gauge_cx, gauge_cy - DP(18), TT_SIZE_XL, th->text_primary);
    tt_font_draw_centered(ui, "Mbps", gauge_cx, gauge_cy + DP(6), TT_SIZE_XS, th->text_secondary);

    // Wi-Fi Details on the right
    int info_x = gauge_cx + gauge_r + DP(14);
    int info_y = cur_y + DP(34);

    // Green pulse dot if connected
    if (wifi->is_connected) {
        draw_circle(ui, info_x + DP(3), info_y + DP(6), DP(3), th->success);
        tt_font_draw(ui, wifi->ssid, info_x + DP(10), info_y, TT_SIZE_SM, th->text_primary);
    } else {
        tt_font_draw(ui, "Sin Red Wi-Fi", info_x, info_y, TT_SIZE_SM, th->text_muted);
    }

    info_y += DP(22);
    tt_font_draw(ui, "Calidad / Senal:", info_x, info_y, TT_SIZE_XS, th->text_muted);
    draw_minimal_signal_bars(ui, info_x + DP(75), info_y, DP(3), DP(11), wifi->signal_level, th->accent, th->card_border);
    
    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm (%d%%)", wifi->rssi_dbm, wifi->link_quality_score);
    tt_font_draw(ui, rssi_str, info_x + DP(96), info_y, TT_SIZE_XS, th->text_secondary);

    info_y += DP(20);
    char freq_str[64];
    if (wifi->frequency_mhz > 0) snprintf(freq_str, sizeof(freq_str), "%s  |  %d MHz", wifi->standard_str, wifi->frequency_mhz);
    else snprintf(freq_str, sizeof(freq_str), "%s", wifi->standard_str);
    tt_font_draw(ui, freq_str, info_x, info_y, TT_SIZE_XS, th->accent_glow);

    // Subtle divider & link speed breakdown
    int w_foot_y = cur_y + wifi_card_h - DP(20);
    fill_rect(ui, pad + DP(12), w_foot_y - DP(6), content_w - DP(24), 1, th->card_border);
    char link_details[96];
    snprintf(link_details, sizeof(link_details), "Tx: %d Mbps  |  Rx: %d Mbps  |  Capacidad: %d Mbps",
             wifi->tx_link_speed_mbps, wifi->rx_link_speed_mbps, wifi->max_tx_speed_mbps);
    tt_font_draw_centered(ui, link_details, pad + content_w / 2, w_foot_y, TT_SIZE_XS, th->text_secondary);

    cur_y += wifi_card_h + card_gap;

    // 6. CARD: FLUJO DE DATOS EN TIEMPO REAL (TrafficStats & Sparkline)
    int net_card_h = DP(118);
    draw_rounded_card(ui, pad, cur_y, content_w, net_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "FLUJO DE DATOS EN TIEMPO REAL", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->text_muted);

    int col_w = content_w / 2;
    int d_x = pad + DP(14);
    int u_x = pad + col_w + DP(6);
    int speed_y = cur_y + DP(26);

    tt_font_draw(ui, "Bajada", d_x, speed_y, TT_SIZE_XS, th->text_muted);
    char rx_str[32];
    if (wifi->rx_rate_kbps > 1024.0) snprintf(rx_str, sizeof(rx_str), "%.2f MB/s", wifi->rx_rate_kbps / 1024.0);
    else snprintf(rx_str, sizeof(rx_str), "%.1f KB/s", wifi->rx_rate_kbps);
    tt_font_draw(ui, rx_str, d_x, speed_y + DP(13), TT_SIZE_MD, th->success);

    tt_font_draw(ui, "Subida", u_x, speed_y, TT_SIZE_XS, th->text_muted);
    char tx_str[32];
    if (wifi->tx_rate_kbps > 1024.0) snprintf(tx_str, sizeof(tx_str), "%.2f MB/s", wifi->tx_rate_kbps / 1024.0);
    else snprintf(tx_str, sizeof(tx_str), "%.1f KB/s", wifi->tx_rate_kbps);
    tt_font_draw(ui, tx_str, u_x, speed_y + DP(13), TT_SIZE_MD, th->accent);

    // Sleek sparkline graph
    int graph_x = pad + DP(14);
    int graph_y = cur_y + DP(64);
    int graph_w = content_w - DP(28);
    int graph_h = DP(32);
    uint32_t spark_fill = RGBA((th->success >> 0) & 0xFF, (th->success >> 8) & 0xFF, (th->success >> 16) & 0xFF, 25);
    draw_smooth_sparkline(ui, graph_x, graph_y, graph_w, graph_h, wifi->rx_history, HISTORY_LEN, wifi->history_idx, th->success, spark_fill);

    char total_str[96];
    double total_rx_mb = (double)wifi->total_rx_bytes / (1024.0 * 1024.0);
    double total_tx_mb = (double)wifi->total_tx_bytes / (1024.0 * 1024.0);
    snprintf(total_str, sizeof(total_str), "Sesion acumulada: Rx %.1f MB  |  Tx %.1f MB", total_rx_mb, total_tx_mb);
    tt_font_draw_centered(ui, total_str, pad + content_w / 2, cur_y + DP(100), TT_SIZE_XS, th->text_muted);

    cur_y += net_card_h + card_gap;

    // 7. CARD: CONEXIONES DE ANDROID (Detailed Network Diagnostics at Bottom)
    int diag_card_h = DP(136);
    draw_rounded_card(ui, pad, cur_y, content_w, diag_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "CONEXIONES DE ANDROID (DIAGNOSTICO)", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->accent_glow);

    int row1_y = cur_y + DP(26);
    tt_font_draw(ui, "IP Local (v4)", d_x, row1_y, TT_SIZE_XS, th->text_muted);
    tt_font_draw(ui, wifi->local_ipv4, d_x, row1_y + DP(11), TT_SIZE_SM, th->text_primary);

    tt_font_draw(ui, "Puerta de Enlace (GW)", u_x, row1_y, TT_SIZE_XS, th->text_muted);
    tt_font_draw(ui, wifi->gateway_ip, u_x, row1_y + DP(11), TT_SIZE_SM, th->text_primary);

    int row2_y = row1_y + DP(28);
    tt_font_draw(ui, "Mascara de Red", d_x, row2_y, TT_SIZE_XS, th->text_muted);
    tt_font_draw(ui, wifi->netmask, d_x, row2_y + DP(11), TT_SIZE_SM, th->text_primary);

    tt_font_draw(ui, "Servidor DNS", u_x, row2_y, TT_SIZE_XS, th->text_muted);
    char dns_str[48];
    snprintf(dns_str, sizeof(dns_str), "%s", strcmp(wifi->dns1, "--") != 0 ? wifi->dns1 : "8.8.8.8");
    tt_font_draw(ui, dns_str, u_x, row2_y + DP(11), TT_SIZE_SM, th->text_primary);

    // Active kernel network interfaces
    int iface_y = row2_y + DP(30);
    fill_rect(ui, pad + DP(12), iface_y - DP(4), content_w - DP(24), 1, th->card_border);

    char ifaces_str[128] = "Interfaces: ";
    for (int i = 0; i < wifi->iface_count && i < 2; i++) {
        char item[48];
        snprintf(item, sizeof(item), "%s (%s)%s",
                 wifi->interfaces[i].name,
                 wifi->interfaces[i].ip,
                 (i < wifi->iface_count - 1 && i < 1) ? "  |  " : "");
        strncat(ifaces_str, item, sizeof(ifaces_str) - strlen(ifaces_str) - 1);
    }
    tt_font_draw(ui, ifaces_str, pad + DP(14), iface_y + DP(3), TT_SIZE_XS, th->text_muted);

    char conn_badge[64];
    snprintf(conn_badge, sizeof(conn_badge), "Tipo: %s  |  Internet: %s",
             wifi->conn_type,
             wifi->is_connected ? "Conectado" : "Desconectado");
    tt_font_draw(ui, conn_badge, pad + DP(14), iface_y + DP(18), TT_SIZE_XS, th->text_secondary);

    cur_y += diag_card_h + DP(10);

    // 8. Minimalist Subtle Footer
    if (cur_y + DP(20) <= ui->height) {
        tt_font_draw_centered(ui, "100% C PURO  |  NDK DIRECT-FB  |  60 FPS", pad + content_w / 2, cur_y + DP(2), TT_SIZE_XS, th->text_muted);
    }

    // Touch Ripple feedback
    if (ui->touch_ripple > 0.0f && ui->touch_x >= 0 && ui->touch_y >= 0) {
        int r = (int)((1.0f - ui->touch_ripple) * (float)DP(70));
        uint32_t rip_col = RGBA(255, 255, 255, (int)(ui->touch_ripple * 80.0f));
        for (int y = ui->touch_y - r; y <= ui->touch_y + r; y++) {
            for (int x = ui->touch_x - r; x <= ui->touch_x + r; x++) {
                int dx = x - ui->touch_x;
                int dy = y - ui->touch_y;
                int d2 = dx * dx + dy * dy;
                if (d2 >= (r - 3) * (r - 3) && d2 <= r * r) {
                    put_pixel_blend(ui, x, y, rip_col);
                }
            }
        }
    }
}
