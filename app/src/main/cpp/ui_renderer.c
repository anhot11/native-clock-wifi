#include "ui_renderer.h"
#include "font8x16.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/sysinfo.h>

#define PI 3.14159265358979323846f

// RGBA 8888 in Little Endian (Byte 0: R, Byte 1: G, Byte 2: B, Byte 3: A)
#define RGBA(r, g, b, a) (((uint32_t)(r) & 0xFF) | (((uint32_t)(g) & 0xFF) << 8) | (((uint32_t)(b) & 0xFF) << 16) | (((uint32_t)(a) & 0xFF) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 255)

static const UITheme THEMES[] = {
    { // 0: Neon Cyan / Cyberpunk
        .bg_color       = RGB(10, 14, 23),
        .card_bg        = RGB(19, 27, 43),
        .card_border    = RGB(33, 47, 74),
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
        .card_border    = RGB(23, 67, 52),
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

// Tight, clean bitmap font rendering
static void draw_char(UIRenderer* ui, char c, int x, int y, int scale, uint32_t color) {
    if (c < 32 || c > 127) c = '?';
    int index = c - 32;
    const uint8_t* rows = font8x16_data[index];

    for (int r = 0; r < 16; r++) {
        uint8_t bits = rows[r];
        if (bits == 0) continue;
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << (7 - col))) {
                if (scale == 1) {
                    put_pixel_blend(ui, x + col, y + r, color);
                } else {
                    fill_rect(ui, x + col * scale, y + r * scale, scale, scale, color);
                }
            }
        }
    }
}

static void draw_text(UIRenderer* ui, const char* str, int x, int y, int scale, uint32_t color) {
    if (!str) return;
    int cur_x = x;
    int step = 7 * scale; // Tightened 7-pixel step for natural kerning
    while (*str) {
        if (*str == '\n') {
            y += 18 * scale;
            cur_x = x;
        } else if (*str == ' ') {
            cur_x += 4 * scale;
        } else {
            draw_char(ui, *str, cur_x, y, scale, color);
            cur_x += step;
        }
        str++;
    }
}

static int get_text_width(const char* str, int scale) {
    if (!str) return 0;
    int w = 0;
    int step = 7 * scale;
    while (*str) {
        if (*str == ' ') w += 4 * scale;
        else w += step;
        str++;
    }
    return w;
}

static void draw_text_centered(UIRenderer* ui, const char* str, int cx, int y, int scale, uint32_t color) {
    int w = get_text_width(str, scale);
    draw_text(ui, str, cx - w / 2, y, scale, color);
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

static void draw_wifi_bars(UIRenderer* ui, int x, int y, int level, uint32_t active_color, uint32_t inactive_color) {
    int bar_w = 6;
    int spacing = 3;
    for (int i = 0; i < 4; i++) {
        int bar_h = 8 + i * 6;
        int bx = x + i * (bar_w + spacing);
        int by = y + (26 - bar_h);
        uint32_t col = (i < level) ? active_color : inactive_color;
        fill_rect(ui, bx, by, bar_w, bar_h, col);
    }
}

// Sparkline graph for live throughput
static void draw_sparkline(UIRenderer* ui, int x, int y, int w, int h, const float* history, int count, int cur_idx, uint32_t line_color, uint32_t fill_color) {
    if (count <= 1 || w <= 0 || h <= 0) return;

    float max_val = 100.0f; // minimum 100 KB/s scale
    for (int i = 0; i < count; i++) {
        if (history[i] > max_val) max_val = history[i];
    }

    float step_x = (float)w / (float)(count - 1);
    int prev_px = x;
    int prev_py = y + h - (int)((history[cur_idx] / max_val) * (h - 4));

    for (int i = 0; i < count; i++) {
        int idx = (cur_idx + i) % count;
        float val = history[idx];
        int px = x + (int)(i * step_x);
        int py = y + h - (int)((val / max_val) * (h - 4));
        if (py < y) py = y;
        if (py > y + h) py = y + h;

        // Fill below line
        fill_rect(ui, px, py, 2, (y + h) - py, fill_color);

        // Draw line segment
        fill_rect(ui, px, py, 2, 2, line_color);
        prev_px = px;
        prev_py = py;
        (void)prev_px; (void)prev_py;
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

    // 1. Clear background
    fill_rect(ui, 0, 0, ui->width, ui->height, th->bg_color);

    // Responsive detection: Landscape vs Portrait
    bool is_landscape = (ui->width >= ui->height);

    // Safe Area Margins (respect notch / Android 11 status bar)
    int safe_top = 70;
    int pad = 24;

    // Subtle header gradient glow
    uint32_t header_glow = RGBA((th->accent >> 0) & 0xFF, (th->accent >> 8) & 0xFF, (th->accent >> 16) & 0xFF, 25);
    fill_rect(ui, 0, 0, ui->width, safe_top + 60, header_glow);

    // 2. Header Bar
    int title_y = safe_top + 4;
    draw_text(ui, "NATIVE APP (100% C)", pad, title_y, 2, th->accent_glow);

    char sub_header[96];
    snprintf(sub_header, sizeof(sub_header), "Android 11 - 16  |  Tema: %s", th->theme_name);
    draw_text(ui, sub_header, pad, title_y + 24, 1, th->text_secondary);

    // Theme Switcher Button on Top Right
    int btn_w = 110;
    int btn_h = 28;
    int btn_x = ui->width - pad - btn_w;
    int btn_y = safe_top + 6;
    draw_rounded_card(ui, btn_x, btn_y, btn_w, btn_h, 8, th->card_bg, th->accent);
    draw_text_centered(ui, "CAMBIAR TEMA", btn_x + btn_w / 2, btn_y + 8, 1, th->accent_glow);

    // Time Data
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

    uint32_t off_seg = RGBA(45, 55, 75, 200);

    if (is_landscape) {
        // ========== LANDSCAPE 2-COLUMN DASHBOARD LAYOUT ==========
        int col_gap = 20;
        int usable_w = ui->width - 2 * pad - col_gap;
        int col1_w = usable_w * 48 / 100;
        int col2_w = usable_w - col1_w;

        int col1_x = pad;
        int col2_x = pad + col1_w + col_gap;
        int start_content_y = safe_top + 60;

        // --- LEFT COLUMN CARD 1: BIG DIGITAL CLOCK ---
        int clock_card_h = 160;
        draw_rounded_card(ui, col1_x, start_content_y, col1_w, clock_card_h, 14, th->card_bg, th->card_border);
        draw_text(ui, "RELOJ DEL SISTEMA", col1_x + 16, start_content_y + 12, 1, th->text_muted);

        // Big Digital Digits: HH : MM : SS
        int d_w = 24;
        int d_h = 46;
        int d_thick = 4;
        int d_gap = 5;
        int colon_w = 10;
        int total_clock_w = 6 * d_w + 4 * d_gap + 2 * colon_w + 14;
        int start_clock_x = col1_x + (col1_w - total_clock_w) / 2;
        int clock_y = start_content_y + 34;

        draw_digital_digit(ui, tm_info->tm_hour / 10, start_clock_x, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
        draw_digital_digit(ui, tm_info->tm_hour % 10, start_clock_x + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

        int cx1 = start_clock_x + 2 * (d_w + d_gap);
        uint32_t colon_col = ((int)(ui->anim_phase * 2) % 2 == 0) ? th->accent : off_seg;
        fill_rect(ui, cx1 + 3, clock_y + 12, d_thick, d_thick, colon_col);
        fill_rect(ui, cx1 + 3, clock_y + 30, d_thick, d_thick, colon_col);

        int mx = cx1 + colon_w + 4;
        draw_digital_digit(ui, tm_info->tm_min / 10, mx, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
        draw_digital_digit(ui, tm_info->tm_min % 10, mx + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

        int cx2 = mx + 2 * (d_w + d_gap);
        fill_rect(ui, cx2 + 3, clock_y + 12, d_thick, d_thick, colon_col);
        fill_rect(ui, cx2 + 3, clock_y + 30, d_thick, d_thick, colon_col);

        int sx = cx2 + colon_w + 4;
        draw_digital_digit(ui, tm_info->tm_sec / 10, sx, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
        draw_digital_digit(ui, tm_info->tm_sec % 10, sx + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

        draw_text_centered(ui, date_str, col1_x + col1_w / 2, start_content_y + 92, 1, th->text_primary);

        char meta_time[64];
        snprintf(meta_time, sizeof(meta_time), "Uptime: %02dh %02dm %02ds  |  TZ: %s", up_h, up_m, up_s, tm_info->tm_zone ? tm_info->tm_zone : "Local");
        draw_text_centered(ui, meta_time, col1_x + col1_w / 2, start_content_y + 116, 1, th->text_muted);

        // --- LEFT COLUMN CARD 2: REAL-TIME TRAFFIC & SPARKLINE GRAPH ---
        int net_card_y = start_content_y + clock_card_h + 16;
        int net_card_h = ui->height - net_card_y - pad;
        if (net_card_h < 130) net_card_h = 130;

        draw_rounded_card(ui, col1_x, net_card_y, col1_w, net_card_h, 14, th->card_bg, th->card_border);
        draw_text(ui, "TRAFICO EN TIEMPO REAL (TrafficStats)", col1_x + 16, net_card_y + 12, 1, th->text_muted);

        int sub_col = col1_w / 2;
        int d_x = col1_x + 16;
        int u_x = col1_x + sub_col + 10;
        int speed_labels_y = net_card_y + 36;

        draw_text(ui, "Descarga:", d_x, speed_labels_y, 1, th->text_secondary);
        char rx_str[32];
        if (wifi->rx_rate_kbps > 1024.0) snprintf(rx_str, sizeof(rx_str), "%.2f MB/s", wifi->rx_rate_kbps / 1024.0);
        else snprintf(rx_str, sizeof(rx_str), "%.1f KB/s", wifi->rx_rate_kbps);
        draw_text(ui, rx_str, d_x, speed_labels_y + 16, 2, th->success);

        draw_text(ui, "Subida:", u_x, speed_labels_y, 1, th->text_secondary);
        char tx_str[32];
        if (wifi->tx_rate_kbps > 1024.0) snprintf(tx_str, sizeof(tx_str), "%.2f MB/s", wifi->tx_rate_kbps / 1024.0);
        else snprintf(tx_str, sizeof(tx_str), "%.1f KB/s", wifi->tx_rate_kbps);
        draw_text(ui, tx_str, u_x, speed_labels_y + 16, 2, th->warning);

        // Live Sparkline Graph inside the card
        int graph_x = col1_x + 16;
        int graph_y = net_card_y + 82;
        int graph_w = col1_w - 32;
        int graph_h = net_card_h - 100;
        if (graph_h > 24) {
            uint32_t spark_fill = RGBA(16, 185, 129, 35);
            draw_sparkline(ui, graph_x, graph_y, graph_w, graph_h, wifi->rx_history, HISTORY_LEN, wifi->history_idx, th->success, spark_fill);
        }

        // --- RIGHT COLUMN: WI-FI SPEEDOMETER & DETAILS CARD ---
        int wifi_card_h = ui->height - start_content_y - pad;
        draw_rounded_card(ui, col2_x, start_content_y, col2_w, wifi_card_h, 14, th->card_bg, th->card_border);

        draw_text(ui, "ESTADO Y VELOCIDAD WI-FI", col2_x + 16, start_content_y + 12, 1, th->text_muted);
        if (wifi->is_connected) {
            draw_rounded_card(ui, col2_x + col2_w - 105, start_content_y + 10, 92, 22, 6, th->card_border, th->success);
            draw_text(ui, "CONECTADO", col2_x + col2_w - 95, start_content_y + 15, 1, th->success);
        } else {
            draw_rounded_card(ui, col2_x + col2_w - 120, start_content_y + 10, 106, 22, 6, th->card_border, th->danger);
            draw_text(ui, "DESCONECTADO", col2_x + col2_w - 114, start_content_y + 15, 1, th->danger);
        }

        // Speedometer Gauge in Center
        int gauge_cx = col2_x + 95;
        int gauge_cy = start_content_y + 120;
        int gauge_r = 65;
        float max_speed_f = (wifi->max_tx_speed_mbps > 0) ? (float)wifi->max_tx_speed_mbps : 150.0f;
        if (max_speed_f < 100.0f) max_speed_f = 100.0f;
        float cur_speed_f = (float)wifi->link_speed_mbps;
        float speed_ratio = cur_speed_f / max_speed_f;
        if (speed_ratio > 1.0f) speed_ratio = 1.0f;
        if (speed_ratio < 0.0f) speed_ratio = 0.0f;

        draw_speedometer_arc(ui, gauge_cx, gauge_cy, gauge_r, 10, speed_ratio, th->accent, th->card_border);

        char speed_num[16];
        snprintf(speed_num, sizeof(speed_num), "%d", wifi->link_speed_mbps);
        draw_text_centered(ui, speed_num, gauge_cx, gauge_cy - 16, 3, th->text_primary);
        draw_text_centered(ui, "Mbps", gauge_cx, gauge_cy + 12, 1, th->accent);

        // Details next to gauge
        int dt_x = col2_x + 190;
        int dt_y = start_content_y + 55;

        draw_text(ui, "Red SSID:", dt_x, dt_y, 1, th->text_secondary);
        draw_text(ui, wifi->ssid, dt_x, dt_y + 16, 1, th->text_primary);

        dt_y += 40;
        draw_text(ui, "Senal:", dt_x, dt_y + 4, 1, th->text_secondary);
        draw_wifi_bars(ui, dt_x + 55, dt_y, wifi->signal_level, th->accent, th->card_border);
        char rssi_str[32];
        snprintf(rssi_str, sizeof(rssi_str), "%d dBm", wifi->rssi_dbm);
        draw_text(ui, rssi_str, dt_x + 100, dt_y + 4, 1, th->text_primary);

        dt_y += 36;
        draw_text(ui, "Estandar & Freq:", dt_x, dt_y, 1, th->text_secondary);
        char freq_str[64];
        if (wifi->frequency_mhz > 0) snprintf(freq_str, sizeof(freq_str), "%s (%d MHz)", wifi->standard_str, wifi->frequency_mhz);
        else snprintf(freq_str, sizeof(freq_str), "%s", wifi->standard_str);
        draw_text(ui, freq_str, dt_x, dt_y + 16, 1, th->accent_glow);

        // Footer details
        int foot_y = start_content_y + wifi_card_h - 40;
        fill_rect(ui, col2_x + 16, foot_y - 10, col2_w - 32, 1, th->card_border);
        char link_details[96];
        snprintf(link_details, sizeof(link_details), "Tx: %d Mbps  |  Rx: %d Mbps  |  Max: %d Mbps",
                 wifi->tx_link_speed_mbps, wifi->rx_link_speed_mbps, wifi->max_tx_speed_mbps);
        draw_text_centered(ui, link_details, col2_x + col2_w / 2, foot_y, 1, th->text_secondary);

    } else {
        // ========== PORTRAIT LAYOUT ==========
        int content_w = ui->width - 2 * pad;
        int cur_y = safe_top + 60;

        // Clock Card
        int clock_card_h = 160;
        draw_rounded_card(ui, pad, cur_y, content_w, clock_card_h, 14, th->card_bg, th->card_border);
        draw_text(ui, "RELOJ DEL SISTEMA", pad + 16, cur_y + 12, 1, th->text_muted);

        int d_w = 26;
        int d_h = 48;
        int d_thick = 4;
        int d_gap = 6;
        int colon_w = 10;
        int total_clock_w = 6 * d_w + 4 * d_gap + 2 * colon_w + 14;
        int start_clock_x = pad + (content_w - total_clock_w) / 2;
        int clock_y = cur_y + 36;

        draw_digital_digit(ui, tm_info->tm_hour / 10, start_clock_x, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
        draw_digital_digit(ui, tm_info->tm_hour % 10, start_clock_x + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

        int cx1 = start_clock_x + 2 * (d_w + d_gap);
        uint32_t colon_col = ((int)(ui->anim_phase * 2) % 2 == 0) ? th->accent : off_seg;
        fill_rect(ui, cx1 + 3, clock_y + 12, d_thick, d_thick, colon_col);
        fill_rect(ui, cx1 + 3, clock_y + 30, d_thick, d_thick, colon_col);

        int mx = cx1 + colon_w + 4;
        draw_digital_digit(ui, tm_info->tm_min / 10, mx, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
        draw_digital_digit(ui, tm_info->tm_min % 10, mx + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

        int cx2 = mx + 2 * (d_w + d_gap);
        fill_rect(ui, cx2 + 3, clock_y + 12, d_thick, d_thick, colon_col);
        fill_rect(ui, cx2 + 3, clock_y + 30, d_thick, d_thick, colon_col);

        int sx = cx2 + colon_w + 4;
        draw_digital_digit(ui, tm_info->tm_sec / 10, sx, clock_y, d_w, d_h, d_thick, th->accent, off_seg);
        draw_digital_digit(ui, tm_info->tm_sec % 10, sx + d_w + d_gap, clock_y, d_w, d_h, d_thick, th->accent, off_seg);

        draw_text_centered(ui, date_str, pad + content_w / 2, cur_y + 96, 1, th->text_primary);
        char meta_time[64];
        snprintf(meta_time, sizeof(meta_time), "Uptime: %02dh %02dm %02ds  |  TZ: %s", up_h, up_m, up_s, tm_info->tm_zone ? tm_info->tm_zone : "Local");
        draw_text_centered(ui, meta_time, pad + content_w / 2, cur_y + 120, 1, th->text_muted);

        cur_y += clock_card_h + 16;

        // Wi-Fi Card
        int wifi_card_h = 240;
        draw_rounded_card(ui, pad, cur_y, content_w, wifi_card_h, 14, th->card_bg, th->card_border);
        draw_text(ui, "ESTADO Y VELOCIDAD WI-FI", pad + 16, cur_y + 12, 1, th->text_muted);
        if (wifi->is_connected) {
            draw_rounded_card(ui, pad + content_w - 105, cur_y + 10, 92, 22, 6, th->card_border, th->success);
            draw_text(ui, "CONECTADO", pad + content_w - 95, cur_y + 15, 1, th->success);
        } else {
            draw_rounded_card(ui, pad + content_w - 120, cur_y + 10, 106, 22, 6, th->card_border, th->danger);
            draw_text(ui, "DESCONECTADO", pad + content_w - 114, cur_y + 15, 1, th->danger);
        }

        int gauge_cx = pad + 85;
        int gauge_cy = cur_y + 115;
        int gauge_r = 55;
        float max_speed_f = (wifi->max_tx_speed_mbps > 0) ? (float)wifi->max_tx_speed_mbps : 150.0f;
        if (max_speed_f < 100.0f) max_speed_f = 100.0f;
        float cur_speed_f = (float)wifi->link_speed_mbps;
        float speed_ratio = cur_speed_f / max_speed_f;
        if (speed_ratio > 1.0f) speed_ratio = 1.0f;
        if (speed_ratio < 0.0f) speed_ratio = 0.0f;

        draw_speedometer_arc(ui, gauge_cx, gauge_cy, gauge_r, 9, speed_ratio, th->accent, th->card_border);

        char speed_num[16];
        snprintf(speed_num, sizeof(speed_num), "%d", wifi->link_speed_mbps);
        draw_text_centered(ui, speed_num, gauge_cx, gauge_cy - 14, 2, th->text_primary);
        draw_text_centered(ui, "Mbps", gauge_cx, gauge_cy + 10, 1, th->accent);

        int info_x = pad + 165;
        int info_y = cur_y + 50;
        draw_text(ui, "Red SSID:", info_x, info_y, 1, th->text_secondary);
        draw_text(ui, wifi->ssid, info_x, info_y + 16, 1, th->text_primary);

        info_y += 36;
        draw_text(ui, "Senal:", info_x, info_y + 4, 1, th->text_secondary);
        draw_wifi_bars(ui, info_x + 55, info_y, wifi->signal_level, th->accent, th->card_border);
        char rssi_str[32];
        snprintf(rssi_str, sizeof(rssi_str), "%d dBm", wifi->rssi_dbm);
        draw_text(ui, rssi_str, info_x + 100, info_y + 4, 1, th->text_primary);

        info_y += 34;
        draw_text(ui, "Estandar:", info_x, info_y, 1, th->text_secondary);
        char freq_str[64];
        if (wifi->frequency_mhz > 0) snprintf(freq_str, sizeof(freq_str), "%s (%d MHz)", wifi->standard_str, wifi->frequency_mhz);
        else snprintf(freq_str, sizeof(freq_str), "%s", wifi->standard_str);
        draw_text(ui, freq_str, info_x, info_y + 16, 1, th->accent_glow);

        int foot_y = cur_y + 195;
        fill_rect(ui, pad + 16, foot_y - 8, content_w - 32, 1, th->card_border);
        char link_details[96];
        snprintf(link_details, sizeof(link_details), "Tx: %d Mbps | Rx: %d Mbps | Max: %d Mbps",
                 wifi->tx_link_speed_mbps, wifi->rx_link_speed_mbps, wifi->max_tx_speed_mbps);
        draw_text_centered(ui, link_details, pad + content_w / 2, foot_y, 1, th->text_secondary);

        cur_y += wifi_card_h + 16;

        // TrafficStats Card with Sparkline
        int net_card_h = 160;
        draw_rounded_card(ui, pad, cur_y, content_w, net_card_h, 14, th->card_bg, th->card_border);
        draw_text(ui, "TRAFICO EN TIEMPO REAL (TrafficStats)", pad + 16, cur_y + 12, 1, th->text_muted);

        int col_w = content_w / 2;
        int d_x = pad + 16;
        int u_x = pad + col_w + 8;
        int speed_y = cur_y + 36;

        draw_text(ui, "Descarga Actual", d_x, speed_y, 1, th->text_secondary);
        char rx_str[32];
        if (wifi->rx_rate_kbps > 1024.0) snprintf(rx_str, sizeof(rx_str), "%.2f MB/s", wifi->rx_rate_kbps / 1024.0);
        else snprintf(rx_str, sizeof(rx_str), "%.1f KB/s", wifi->rx_rate_kbps);
        draw_text(ui, rx_str, d_x, speed_y + 16, 2, th->success);

        draw_text(ui, "Subida Actual", u_x, speed_y, 1, th->text_secondary);
        char tx_str[32];
        if (wifi->tx_rate_kbps > 1024.0) snprintf(tx_str, sizeof(tx_str), "%.2f MB/s", wifi->tx_rate_kbps / 1024.0);
        else snprintf(tx_str, sizeof(tx_str), "%.1f KB/s", wifi->tx_rate_kbps);
        draw_text(ui, tx_str, u_x, speed_y + 16, 2, th->warning);

        int graph_x = pad + 16;
        int graph_y = cur_y + 88;
        int graph_w = content_w - 32;
        int graph_h = 56;
        uint32_t spark_fill = RGBA(16, 185, 129, 35);
        draw_sparkline(ui, graph_x, graph_y, graph_w, graph_h, wifi->rx_history, HISTORY_LEN, wifi->history_idx, th->success, spark_fill);
    }

    // Touch Ripple effect
    if (ui->touch_ripple > 0.0f && ui->touch_x >= 0 && ui->touch_y >= 0) {
        int r = (int)((1.0f - ui->touch_ripple) * 120.0f);
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
