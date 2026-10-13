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
    { // 0: Neon Cyan / Cyberpunk
        .bg_color       = RGB(10, 15, 26),
        .card_bg        = RGB(20, 29, 47),
        .card_border    = RGB(36, 52, 84),
        .text_primary   = RGB(241, 245, 249),
        .text_secondary = RGB(148, 163, 184),
        .text_muted     = RGB(100, 116, 139),
        .accent         = RGB(6, 182, 212),
        .accent_glow    = RGB(34, 211, 238),
        .success        = RGB(16, 185, 129),
        .warning        = RGB(245, 158, 11),
        .danger         = RGB(239, 68, 68),
        .theme_name     = "Cyber Cyan"
    },
    { // 1: Emerald Neon
        .bg_color       = RGB(6, 20, 16),
        .card_bg        = RGB(14, 38, 30),
        .card_border    = RGB(24, 69, 54),
        .text_primary   = RGB(240, 253, 244),
        .text_secondary = RGB(134, 239, 172),
        .text_muted     = RGB(74, 222, 128),
        .accent         = RGB(16, 185, 129),
        .accent_glow    = RGB(52, 211, 153),
        .success        = RGB(16, 185, 129),
        .warning        = RGB(234, 179, 8),
        .danger         = RGB(244, 63, 94),
        .theme_name     = "Emerald Matrix"
    },
    { // 2: Solar Amber
        .bg_color       = RGB(20, 15, 8),
        .card_bg        = RGB(42, 30, 15),
        .card_border    = RGB(75, 54, 24),
        .text_primary   = RGB(254, 243, 199),
        .text_secondary = RGB(252, 211, 77),
        .text_muted     = RGB(217, 119, 6),
        .accent         = RGB(245, 158, 11),
        .accent_glow    = RGB(251, 191, 36),
        .success        = RGB(52, 211, 153),
        .warning        = RGB(245, 158, 11),
        .danger         = RGB(239, 68, 68),
        .theme_name     = "Solar Amber"
    },
    { // 3: Sunset Purple
        .bg_color       = RGB(15, 10, 28),
        .card_bg        = RGB(32, 21, 56),
        .card_border    = RGB(60, 40, 102),
        .text_primary   = RGB(250, 245, 255),
        .text_secondary = RGB(216, 180, 254),
        .text_muted     = RGB(168, 85, 247),
        .accent         = RGB(168, 85, 247),
        .accent_glow    = RGB(192, 132, 252),
        .success        = RGB(52, 211, 153),
        .warning        = RGB(251, 191, 36),
        .danger         = RGB(244, 63, 94),
        .theme_name     = "Sunset Purple"
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

    // Mobile DP scale based on 360dp width baseline
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

    fill_rect(ui, x + radius, y, w - 2 * radius, 1, border);
    fill_rect(ui, x + radius, y + h - 1, w - 2 * radius, 1, border);
    fill_rect(ui, x, y + radius, 1, h - 2 * radius, border);
    fill_rect(ui, x + w - 1, y + radius, 1, h - 2 * radius, border);
}

// 7-segment digital digits
static const uint8_t SEVEN_SEG_DIGITS[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
};

static void draw_digital_digit(UIRenderer* ui, int digit, int x, int y, int w, int h, int thickness, uint32_t on_color, uint32_t off_color) {
    if (digit < 0 || digit > 9) return;
    uint8_t mask = SEVEN_SEG_DIGITS[digit];
    int mid_y = y + h / 2;

    fill_rect(ui, x + thickness, y, w - 2 * thickness, thickness, (mask & 1) ? on_color : off_color);
    fill_rect(ui, x + w - thickness, y + thickness, thickness, (h / 2) - thickness, (mask & 2) ? on_color : off_color);
    fill_rect(ui, x + w - thickness, mid_y, thickness, (h / 2) - thickness, (mask & 4) ? on_color : off_color);
    fill_rect(ui, x + thickness, y + h - thickness, w - 2 * thickness, thickness, (mask & 8) ? on_color : off_color);
    fill_rect(ui, x, mid_y, thickness, (h / 2) - thickness, (mask & 16) ? on_color : off_color);
    fill_rect(ui, x, y + thickness, thickness, (h / 2) - thickness, (mask & 32) ? on_color : off_color);
    fill_rect(ui, x + thickness, mid_y - thickness / 2, w - 2 * thickness, thickness, (mask & 64) ? on_color : off_color);
}

static void draw_speedometer_arc(UIRenderer* ui, int cx, int cy, int radius, int thickness, float percent, uint32_t active_color, uint32_t bg_color) {
    float start_angle = 135.0f * (PI / 180.0f);
    float sweep = 270.0f * (PI / 180.0f);
    float current_angle = start_angle + sweep * percent;

    for (int y = cy - radius - 2; y <= cy + radius + 2; y++) {
        for (int x = cx - radius - 2; x <= cx + radius + 2; x++) {
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
                        put_pixel_blend(ui, x, y, bg_color);
                    }
                }
            }
        }
    }
}

static void draw_wifi_bars(UIRenderer* ui, int x, int y, int bar_w, int bar_max_h, int level, uint32_t active_color, uint32_t inactive_color) {
    int spacing = bar_w / 2;
    if (spacing < 2) spacing = 2;
    for (int i = 0; i < 4; i++) {
        int bar_h = (bar_max_h * (i + 1)) / 4;
        int bx = x + i * (bar_w + spacing);
        int by = y + (bar_max_h - bar_h);
        uint32_t col = (i < level) ? active_color : inactive_color;
        fill_rect(ui, bx, by, bar_w, bar_h, col);
    }
}

static void draw_sparkline(UIRenderer* ui, int x, int y, int w, int h, const float* history, int count, int cur_idx, uint32_t line_color, uint32_t fill_color) {
    if (count <= 1 || w <= 0 || h <= 0) return;

    float max_val = 100.0f;
    for (int i = 0; i < count; i++) {
        if (history[i] > max_val) max_val = history[i];
    }

    float step_x = (float)w / (float)(count - 1);
    for (int i = 0; i < count; i++) {
        int idx = (cur_idx + i) % count;
        float val = history[idx];
        int px = x + (int)(i * step_x);
        int py = y + h - (int)((val / max_val) * (h - 4));
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

    // 1. Background
    fill_rect(ui, 0, 0, ui->width, ui->height, th->bg_color);

    // Dynamic Mobile Layout Calculations (DP scaled)
    int pad = DP(14);
    int safe_top = DP(34); // Space for Android status bar
    int content_w = ui->width - 2 * pad;
    int card_radius = DP(14);
    int card_gap = DP(12);

    // Header gradient glow
    uint32_t header_glow = RGBA((th->accent >> 0) & 0xFF, (th->accent >> 8) & 0xFF, (th->accent >> 16) & 0xFF, 28);
    fill_rect(ui, 0, 0, ui->width, safe_top + DP(48), header_glow);

    // 2. Mobile Header Bar
    int title_y = safe_top + DP(4);
    tt_font_draw(ui, "NATIVE ANDROID", pad, title_y, TT_SIZE_LG, th->accent_glow);

    char sub_header[96];
    snprintf(sub_header, sizeof(sub_header), "Android 11-16  |  Tema: %s", th->theme_name);
    tt_font_draw(ui, sub_header, pad, title_y + DP(20), TT_SIZE_XS, th->text_secondary);

    // Mobile "CAMBIAR TEMA" touch button
    int btn_w = DP(90);
    int btn_h = DP(28);
    int btn_x = ui->width - pad - btn_w;
    int btn_y = safe_top + DP(6);
    draw_rounded_card(ui, btn_x, btn_y, btn_w, btn_h, DP(8), th->card_bg, th->accent);
    tt_font_draw_centered(ui, "CAMBIAR TEMA", btn_x + btn_w / 2, btn_y + DP(6), TT_SIZE_XS, th->accent_glow);

    int cur_y = safe_top + DP(48);

    // 3. CARD 1: RELOJ DIGITAL DEL SISTEMA (Large Mobile Clock)
    time_t raw_time;
    time(&raw_time);
    struct tm* tm_info = localtime(&raw_time);

    struct sysinfo si;
    sysinfo(&si);
    int up_h = si.uptime / 3600;
    int up_m = (si.uptime % 3600) / 60;
    int up_s = si.uptime % 60;

    static const char* MONTHS_ES[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
    static const char* DAYS_ES[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};
    char date_str[64];
    snprintf(date_str, sizeof(date_str), "%s, %02d de %s %04d",
             DAYS_ES[tm_info->tm_wday], tm_info->tm_mday, MONTHS_ES[tm_info->tm_mon], tm_info->tm_year + 1900);

    int clock_card_h = DP(128);
    draw_rounded_card(ui, pad, cur_y, content_w, clock_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "RELOJ DEL SISTEMA", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->text_muted);

    // Big 7-Segment Digits: HH : MM : SS
    int d_w = DP(18);
    int d_h = DP(36);
    int d_thick = DP(4);
    if (d_thick < 2) d_thick = 2;
    int d_gap = DP(4);
    int colon_w = DP(8);
    int total_clock_w = 6 * d_w + 4 * d_gap + 2 * colon_w + DP(10);
    int start_clock_x = pad + (content_w - total_clock_w) / 2;
    int clock_y = cur_y + DP(28);

    uint32_t off_seg = RGBA(45, 55, 75, 180);

    // Hours
    draw_digital_digit(ui, tm_info->tm_hour / 10, start_clock_x, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
    draw_digital_digit(ui, tm_info->tm_hour % 10, start_clock_x + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

    // Colon 1
    int cx1 = start_clock_x + 2 * (d_w + d_gap);
    uint32_t colon_col = ((int)(ui->anim_phase * 2) % 2 == 0) ? th->accent : off_seg;
    fill_rect(ui, cx1 + DP(2), clock_y + DP(8), d_thick, d_thick, colon_col);
    fill_rect(ui, cx1 + DP(2), clock_y + DP(22), d_thick, d_thick, colon_col);

    // Minutes
    int mx = cx1 + colon_w + DP(3);
    draw_digital_digit(ui, tm_info->tm_min / 10, mx, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
    draw_digital_digit(ui, tm_info->tm_min % 10, mx + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

    // Colon 2
    int cx2 = mx + 2 * (d_w + d_gap);
    fill_rect(ui, cx2 + DP(2), clock_y + DP(8), d_thick, d_thick, colon_col);
    fill_rect(ui, cx2 + DP(2), clock_y + DP(22), d_thick, d_thick, colon_col);

    // Seconds
    int sx = cx2 + colon_w + DP(3);
    draw_digital_digit(ui, tm_info->tm_sec / 10, sx, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
    draw_digital_digit(ui, tm_info->tm_sec % 10, sx + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

    // Date & Uptime using TrueType fonts
    tt_font_draw_centered(ui, date_str, pad + content_w / 2, cur_y + DP(74), TT_SIZE_MD, th->text_primary);

    char meta_time[64];
    snprintf(meta_time, sizeof(meta_time), "Uptime: %02dh %02dm %02ds  |  TZ: %s", up_h, up_m, up_s, tm_info->tm_zone ? tm_info->tm_zone : "Local");
    tt_font_draw_centered(ui, meta_time, pad + content_w / 2, cur_y + DP(98), TT_SIZE_XS, th->text_muted);

    cur_y += clock_card_h + card_gap;

    // 4. CARD 2: ESTADO Y VELOCIDAD WI-FI (Prominent Mobile Gauge)
    int wifi_card_h = DP(155);
    draw_rounded_card(ui, pad, cur_y, content_w, wifi_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "ESTADO Y VELOCIDAD WI-FI", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->text_muted);

    if (wifi->is_connected) {
        int b_w = DP(75);
        int b_h = DP(18);
        draw_rounded_card(ui, pad + content_w - b_w - DP(12), cur_y + DP(8), b_w, b_h, DP(5), th->card_border, th->success);
        tt_font_draw_centered(ui, "CONECTADO", pad + content_w - b_w / 2 - DP(12), cur_y + DP(10), TT_SIZE_XS, th->success);
    } else {
        int b_w = DP(90);
        int b_h = DP(18);
        draw_rounded_card(ui, pad + content_w - b_w - DP(12), cur_y + DP(8), b_w, b_h, DP(5), th->card_border, th->danger);
        tt_font_draw_centered(ui, "DESCONECTADO", pad + content_w - b_w / 2 - DP(12), cur_y + DP(10), TT_SIZE_XS, th->danger);
    }

    // Speedometer Gauge on Left
    int gauge_r = DP(40);
    int gauge_cx = pad + gauge_r + DP(14);
    int gauge_cy = cur_y + DP(75);

    float max_speed_f = (wifi->max_tx_speed_mbps > 0) ? (float)wifi->max_tx_speed_mbps : 150.0f;
    if (max_speed_f < 100.0f) max_speed_f = 100.0f;
    float cur_speed_f = (float)wifi->link_speed_mbps;
    float speed_ratio = cur_speed_f / max_speed_f;
    if (speed_ratio > 1.0f) speed_ratio = 1.0f;
    if (speed_ratio < 0.0f) speed_ratio = 0.0f;

    draw_speedometer_arc(ui, gauge_cx, gauge_cy, gauge_r, DP(7), speed_ratio, th->accent, th->card_border);

    char speed_num[16];
    snprintf(speed_num, sizeof(speed_num), "%d", wifi->link_speed_mbps);
    tt_font_draw_centered(ui, speed_num, gauge_cx, gauge_cy - DP(14), TT_SIZE_LG, th->text_primary);
    tt_font_draw_centered(ui, "Mbps", gauge_cx, gauge_cy + DP(6), TT_SIZE_XS, th->accent);

    // Wi-Fi Details on Right
    int info_x = gauge_cx + gauge_r + DP(14);
    int info_y = cur_y + DP(34);

    tt_font_draw(ui, "Red SSID:", info_x, info_y, TT_SIZE_XS, th->text_secondary);
    tt_font_draw(ui, wifi->ssid, info_x, info_y + DP(14), TT_SIZE_SM, th->text_primary);

    info_y += DP(32);
    tt_font_draw(ui, "Senal:", info_x, info_y, TT_SIZE_XS, th->text_secondary);
    draw_wifi_bars(ui, info_x + DP(36), info_y, DP(4), DP(14), wifi->signal_level, th->accent, th->card_border);
    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm", wifi->rssi_dbm);
    tt_font_draw(ui, rssi_str, info_x + DP(65), info_y, TT_SIZE_XS, th->text_primary);

    info_y += DP(22);
    char freq_str[64];
    if (wifi->frequency_mhz > 0) snprintf(freq_str, sizeof(freq_str), "%s (%d MHz)", wifi->standard_str, wifi->frequency_mhz);
    else snprintf(freq_str, sizeof(freq_str), "%s", wifi->standard_str);
    tt_font_draw(ui, freq_str, info_x, info_y, TT_SIZE_XS, th->accent_glow);

    // Footer of Wi-Fi Card: Tx / Rx / Max
    int w_foot_y = cur_y + wifi_card_h - DP(22);
    fill_rect(ui, pad + DP(12), w_foot_y - DP(6), content_w - DP(24), 1, th->card_border);
    char link_details[96];
    snprintf(link_details, sizeof(link_details), "Tx: %d Mbps  |  Rx: %d Mbps  |  Max: %d Mbps",
             wifi->tx_link_speed_mbps, wifi->rx_link_speed_mbps, wifi->max_tx_speed_mbps);
    tt_font_draw_centered(ui, link_details, pad + content_w / 2, w_foot_y, TT_SIZE_XS, th->text_secondary);

    cur_y += wifi_card_h + card_gap;

    // 5. CARD 3: TRAFICO EN TIEMPO REAL (Live Bandwidth & Sparkline)
    int net_card_h = DP(125);
    draw_rounded_card(ui, pad, cur_y, content_w, net_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "TRAFICO EN TIEMPO REAL (TrafficStats)", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->text_muted);

    int col_w = content_w / 2;
    int d_x = pad + DP(14);
    int u_x = pad + col_w + DP(6);
    int speed_y = cur_y + DP(28);

    tt_font_draw(ui, "Descarga", d_x, speed_y, TT_SIZE_XS, th->text_secondary);
    char rx_str[32];
    if (wifi->rx_rate_kbps > 1024.0) snprintf(rx_str, sizeof(rx_str), "%.2f MB/s", wifi->rx_rate_kbps / 1024.0);
    else snprintf(rx_str, sizeof(rx_str), "%.1f KB/s", wifi->rx_rate_kbps);
    tt_font_draw(ui, rx_str, d_x, speed_y + DP(14), TT_SIZE_MD, th->success);

    tt_font_draw(ui, "Subida", u_x, speed_y, TT_SIZE_XS, th->text_secondary);
    char tx_str[32];
    if (wifi->tx_rate_kbps > 1024.0) snprintf(tx_str, sizeof(tx_str), "%.2f MB/s", wifi->tx_rate_kbps / 1024.0);
    else snprintf(tx_str, sizeof(tx_str), "%.1f KB/s", wifi->tx_rate_kbps);
    tt_font_draw(ui, tx_str, u_x, speed_y + DP(14), TT_SIZE_MD, th->warning);

    // Live Sparkline Graph
    int graph_x = pad + DP(14);
    int graph_y = cur_y + DP(68);
    int graph_w = content_w - DP(28);
    int graph_h = DP(34);
    uint32_t spark_fill = RGBA(16, 185, 129, 35);
    draw_sparkline(ui, graph_x, graph_y, graph_w, graph_h, wifi->rx_history, HISTORY_LEN, wifi->history_idx, th->success, spark_fill);

    char total_str[96];
    double total_rx_mb = (double)wifi->total_rx_bytes / (1024.0 * 1024.0);
    double total_tx_mb = (double)wifi->total_tx_bytes / (1024.0 * 1024.0);
    snprintf(total_str, sizeof(total_str), "Total transferido: Rx: %.1f MB  |  Tx: %.1f MB", total_rx_mb, total_tx_mb);
    tt_font_draw_centered(ui, total_str, pad + content_w / 2, cur_y + DP(106), TT_SIZE_XS, th->text_muted);

    cur_y += net_card_h + card_gap;

    // 6. CARD 4: CONEXIONES DE ANDROID (Detailed Network Diagnostics at Bottom)
    int diag_card_h = DP(140);
    draw_rounded_card(ui, pad, cur_y, content_w, diag_card_h, card_radius, th->card_bg, th->card_border);
    tt_font_draw(ui, "CONEXIONES DE ANDROID (Redes & IP)", pad + DP(14), cur_y + DP(10), TT_SIZE_XS, th->accent_glow);

    int row1_y = cur_y + DP(28);
    tt_font_draw(ui, "IP Local (v4):", d_x, row1_y, TT_SIZE_XS, th->text_secondary);
    tt_font_draw(ui, wifi->local_ipv4, d_x, row1_y + DP(12), TT_SIZE_SM, th->text_primary);

    tt_font_draw(ui, "Puerta Enlace (GW):", u_x, row1_y, TT_SIZE_XS, th->text_secondary);
    tt_font_draw(ui, wifi->gateway_ip, u_x, row1_y + DP(12), TT_SIZE_SM, th->text_primary);

    int row2_y = row1_y + DP(30);
    tt_font_draw(ui, "Mascara de Red:", d_x, row2_y, TT_SIZE_XS, th->text_secondary);
    tt_font_draw(ui, wifi->netmask, d_x, row2_y + DP(12), TT_SIZE_SM, th->text_primary);

    tt_font_draw(ui, "Servidor DNS:", u_x, row2_y, TT_SIZE_XS, th->text_secondary);
    char dns_str[48];
    snprintf(dns_str, sizeof(dns_str), "%s", strcmp(wifi->dns1, "--") != 0 ? wifi->dns1 : "8.8.8.8");
    tt_font_draw(ui, dns_str, u_x, row2_y + DP(12), TT_SIZE_SM, th->text_primary);

    // Active Interfaces summary
    int iface_y = row2_y + DP(32);
    fill_rect(ui, pad + DP(12), iface_y - DP(4), content_w - DP(24), 1, th->card_border);

    char ifaces_str[128] = "Interfaces: ";
    for (int i = 0; i < wifi->iface_count && i < 3; i++) {
        char item[32];
        snprintf(item, sizeof(item), "%s (%s)%s",
                 wifi->interfaces[i].name,
                 wifi->interfaces[i].ip,
                 (i < wifi->iface_count - 1 && i < 2) ? ", " : "");
        strncat(ifaces_str, item, sizeof(ifaces_str) - strlen(ifaces_str) - 1);
    }
    tt_font_draw(ui, ifaces_str, pad + DP(14), iface_y + DP(4), TT_SIZE_XS, th->text_muted);

    char conn_badge[64];
    snprintf(conn_badge, sizeof(conn_badge), "Tipo: %s  |  Internet: %s",
             wifi->conn_type,
             wifi->is_connected ? "Validado" : "Desconectado");
    tt_font_draw(ui, conn_badge, pad + DP(14), iface_y + DP(20), TT_SIZE_XS, th->text_secondary);

    cur_y += diag_card_h + DP(10);

    // 7. Footer Badge (if fits on screen)
    if (cur_y + DP(24) <= ui->height) {
        tt_font_draw_centered(ui, "NDK DIRECT-FB  |  60 FPS  |  ORIENTACION VERTICAL", pad + content_w / 2, cur_y + DP(2), TT_SIZE_XS, th->text_muted);
    }

    // Touch Ripple effect
    if (ui->touch_ripple > 0.0f && ui->touch_x >= 0 && ui->touch_y >= 0) {
        int r = (int)((1.0f - ui->touch_ripple) * (float)DP(80));
        uint32_t rip_col = RGBA(255, 255, 255, (int)(ui->touch_ripple * 90.0f));
        for (int y = ui->touch_y - r; y <= ui->touch_y + r; y++) {
            for (int x = ui->touch_x - r; x <= ui->touch_x + r; x++) {
                int dx = x - ui->touch_x;
                int dy = y - ui->touch_y;
                int d2 = dx * dx + dy * dy;
                if (d2 >= (r - 4) * (r - 4) && d2 <= r * r) {
                    put_pixel_blend(ui, x, y, rip_col);
                }
            }
        }
    }
}
