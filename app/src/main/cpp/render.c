#include "render.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

static inline void put_pixel_blend(const RenderTarget* target, int x, int y, uint32_t color) {
    if (x < 0 || x >= target->width || y < 0 || y >= target->height) return;

    uint32_t sa = (color >> 24) & 0xFF;
    if (sa == 0) return;

    uint32_t* p = &target->pixels[y * target->stride + x];
    if (sa == 255) {
        *p = color;
        return;
    }

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

static inline void put_pixel_coverage(const RenderTarget* target, int x, int y, uint32_t color, float coverage) {
    if (coverage <= 0.001f) return;
    if (coverage >= 0.999f) {
        put_pixel_blend(target, x, y, color);
        return;
    }
    uint32_t a = (uint32_t)(((color >> 24) & 0xFF) * coverage);
    if (a > 255) a = 255;
    put_pixel_blend(target, x, y, (color & 0x00FFFFFF) | (a << 24));
}

void render_clear(const RenderTarget* target, uint32_t color) {
    if (!target || !target->pixels || target->width <= 0 || target->height <= 0) return;

    int total_pixels = target->height * target->stride;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    uint32x4_t col4 = vdupq_n_u32(color);
    int i = 0;
    for (; i <= total_pixels - 8; i += 8) {
        vst1q_u32(&target->pixels[i], col4);
        vst1q_u32(&target->pixels[i + 4], col4);
    }
    for (; i < total_pixels; i++) {
        target->pixels[i] = color;
    }
#else
    for (int i = 0; i < total_pixels; i++) {
        target->pixels[i] = color;
    }
#endif
}

void render_fill_rect(const RenderTarget* target, int x0, int y0, int w, int h, uint32_t color) {
    if (!target || !target->pixels || w <= 0 || h <= 0) return;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > target->width) x1 = target->width;
    if (y1 > target->height) y1 = target->height;
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t a = (color >> 24) & 0xFF;
    if (a == 255) {
        for (int y = y0; y < y1; y++) {
            uint32_t* row = &target->pixels[y * target->stride + x0];
            int count = x1 - x0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
            uint32x4_t col4 = vdupq_n_u32(color);
            int i = 0;
            for (; i <= count - 4; i += 4) {
                vst1q_u32(&row[i], col4);
            }
            for (; i < count; i++) {
                row[i] = color;
            }
#else
            for (int x = 0; x < count; x++) {
                row[x] = color;
            }
#endif
        }
    } else if (a > 0) {
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                put_pixel_blend(target, x, y, color);
            }
        }
    }
}

// SDF Anti-aliased Rounded Rectangle
void render_rounded_rect(const RenderTarget* target, int x, int y, int w, int h, int radius, uint32_t bg, uint32_t border, int border_w) {
    if (!target || !target->pixels || w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    float half_w = (float)w * 0.5f;
    float half_h = (float)h * 0.5f;
    float cx = (float)x + half_w;
    float cy = (float)y + half_h;
    float bx = half_w - (float)radius;
    float by = half_h - (float)radius;

    int min_x = (x0 < 0) ? 0 : x0;
    int max_x = (x1 > target->width) ? target->width : x1;
    int min_y = (y0 < 0) ? 0 : y0;
    int max_y = (y1 > target->height) ? target->height : y1;

    for (int py = min_y; py < max_y; py++) {
        float qy = fabsf((float)py + 0.5f - cy) - by;
        for (int px = min_x; px < max_x; px++) {
            float qx = fabsf((float)px + 0.5f - cx) - bx;
            
            float dist;
            if (qx > 0.0f && qy > 0.0f) {
                dist = sqrtf(qx * qx + qy * qy) - (float)radius;
            } else {
                dist = (qx > qy ? qx : qy) - (float)radius;
            }

            if (dist > 1.0f) continue; // fully outside

            float coverage = 0.5f - dist;
            if (coverage > 1.0f) coverage = 1.0f;
            if (coverage < 0.0f) coverage = 0.0f;

            if (border_w > 0 && border != 0) {
                float border_dist = fabsf(dist + (float)border_w * 0.5f) - (float)border_w * 0.5f;
                float border_cov = 0.5f - border_dist;
                if (border_cov > 1.0f) border_cov = 1.0f;
                if (border_cov < 0.0f) border_cov = 0.0f;

                if (dist <= 0.0f) {
                    put_pixel_coverage(target, px, py, bg, coverage);
                    if (border_cov > 0.0f) {
                        put_pixel_coverage(target, px, py, border, border_cov);
                    }
                } else {
                    put_pixel_coverage(target, px, py, border, border_cov * coverage);
                }
            } else {
                put_pixel_coverage(target, px, py, bg, coverage);
            }
        }
    }
}

// Glassmorphism Card with subtle vertical gradient and ambient shadow
void render_glass_card(const RenderTarget* target, int x, int y, int w, int h, int radius, uint32_t bg_top, uint32_t bg_bot, uint32_t border, uint32_t shadow) {
    if (!target || !target->pixels || w <= 0 || h <= 0) return;

    // Ambient drop shadow (offset +2y, blur +2)
    if (shadow != 0) {
        render_rounded_rect(target, x, y + 3, w, h, radius, shadow, 0, 0);
    }

    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    float half_w = (float)w * 0.5f;
    float half_h = (float)h * 0.5f;
    float cx = (float)x + half_w;
    float cy = (float)y + half_h;
    float bx = half_w - (float)radius;
    float by = half_h - (float)radius;

    int min_x = (x < 0) ? 0 : x;
    int max_x = (x + w > target->width) ? target->width : x + w;
    int min_y = (y < 0) ? 0 : y;
    int max_y = (y + h > target->height) ? target->height : y + h;

    for (int py = min_y; py < max_y; py++) {
        float qy = fabsf((float)py + 0.5f - cy) - by;
        float t = (float)(py - y) / (float)h;
        uint32_t row_bg = color_blend(bg_top, bg_bot, t);

        for (int px = min_x; px < max_x; px++) {
            float qx = fabsf((float)px + 0.5f - cx) - bx;
            float dist;
            if (qx > 0.0f && qy > 0.0f) {
                dist = sqrtf(qx * qx + qy * qy) - (float)radius;
            } else {
                dist = (qx > qy ? qx : qy) - (float)radius;
            }

            if (dist > 1.0f) continue;

            float coverage = 0.5f - dist;
            if (coverage > 1.0f) coverage = 1.0f;
            if (coverage < 0.0f) coverage = 0.0f;

            // 1px anti-aliased border
            float border_dist = fabsf(dist + 0.5f) - 0.5f;
            float border_cov = 0.5f - border_dist;
            if (border_cov > 1.0f) border_cov = 1.0f;
            if (border_cov < 0.0f) border_cov = 0.0f;

            if (dist <= 0.0f) {
                put_pixel_coverage(target, px, py, row_bg, coverage);
                if (border_cov > 0.0f && border != 0) {
                    put_pixel_coverage(target, px, py, border, border_cov);
                }
            } else if (border != 0) {
                put_pixel_coverage(target, px, py, border, border_cov * coverage);
            }
        }
    }
}

void render_circle(const RenderTarget* target, int cx, int cy, int radius, uint32_t color) {
    if (!target || radius <= 0) return;
    int min_x = cx - radius - 1;
    int max_x = cx + radius + 1;
    int min_y = cy - radius - 1;
    int max_y = cy + radius + 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float dx = (float)x + 0.5f - (float)cx;
            float dy = (float)y + 0.5f - (float)cy;
            float dist = sqrtf(dx * dx + dy * dy) - (float)radius;
            if (dist <= 1.0f) {
                float cov = 0.5f - dist;
                if (cov > 1.0f) cov = 1.0f;
                if (cov > 0.0f) put_pixel_coverage(target, x, y, color, cov);
            }
        }
    }
}

// Anti-aliased Ring / Gauge Arc with smooth rounded caps
void render_ring(const RenderTarget* target, int cx, int cy, int radius, int thickness, float percent, float start_angle, float sweep_angle, uint32_t active_color, uint32_t track_color) {
    if (!target || radius <= thickness || sweep_angle <= 0.0f) return;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    float r_mid = (float)radius - (float)thickness * 0.5f;
    float half_thick = (float)thickness * 0.5f;
    float active_sweep = sweep_angle * percent;

    int bb_min_x = cx - radius - 2;
    int bb_max_x = cx + radius + 2;
    int bb_min_y = cy - radius - 2;
    int bb_max_y = cy + radius + 2;

    for (int y = bb_min_y; y <= bb_max_y; y++) {
        for (int x = bb_min_x; x <= bb_max_x; x++) {
            float dx = (float)x + 0.5f - (float)cx;
            float dy = (float)y + 0.5f - (float)cy;
            float dist_from_center = sqrtf(dx * dx + dy * dy);
            float dist_from_mid = fabsf(dist_from_center - r_mid);

            if (dist_from_mid > half_thick + 1.0f) continue;

            float angle = atan2f(dy, dx);
            if (angle < 0.0f) angle += 2.0f * PI_F;

            // Offset angle relative to start_angle
            float rel_angle = angle - start_angle;
            while (rel_angle < 0.0f) rel_angle += 2.0f * PI_F;
            while (rel_angle >= 2.0f * PI_F) rel_angle -= 2.0f * PI_F;

            float ring_cov = 0.5f - (dist_from_mid - half_thick);
            if (ring_cov > 1.0f) ring_cov = 1.0f;
            if (ring_cov <= 0.0f) continue;

            if (rel_angle <= sweep_angle) {
                if (rel_angle <= active_sweep) {
                    put_pixel_coverage(target, x, y, active_color, ring_cov);
                } else if (track_color != 0) {
                    put_pixel_coverage(target, x, y, track_color, ring_cov);
                }
            }
        }
    }
}

void render_segmented_bar(const RenderTarget* target, int x, int y, int w, int h, int segments, int filled_segments, uint32_t active_color, uint32_t track_color, int gap) {
    if (!target || segments <= 0 || w <= 0 || h <= 0) return;
    int total_gaps = (segments - 1) * gap;
    int seg_w = (w - total_gaps) / segments;
    if (seg_w < 2) seg_w = 2;

    int radius = h / 3;
    if (radius > 3) radius = 3;

    for (int i = 0; i < segments; i++) {
        int sx = x + i * (seg_w + gap);
        uint32_t col = (i < filled_segments) ? active_color : track_color;
        render_rounded_rect(target, sx, y, seg_w, h, radius, col, 0, 0);
    }
}

// Smooth Sparkline with optional anti-aliasing and vertical area fill
void render_sparkline(const RenderTarget* target, int x, int y, int w, int h, const float* data, int count, int cur_idx, float min_val, float max_val, uint32_t line_color, uint32_t fill_color) {
    if (!target || !data || count <= 1 || w <= 4 || h <= 4) return;
    if (max_val <= min_val) max_val = min_val + 1.0f;

    float step_x = (float)w / (float)(count - 1);
    float range = max_val - min_val;

    for (int i = 0; i < count; i++) {
        int idx = (cur_idx + i) % count;
        float val = data[idx];
        if (val < min_val) val = min_val;
        if (val > max_val) val = max_val;

        float norm = (val - min_val) / range;
        int py = y + h - (int)(norm * (float)(h - 3));
        int px = x + (int)((float)i * step_x);
        int seg_w = (int)ceilf(step_x);

        if (fill_color != 0 && py < y + h) {
            render_fill_rect(target, px, py, seg_w, (y + h) - py, fill_color);
        }
        render_fill_rect(target, px, py, seg_w, 2, line_color);
    }
}

void render_dual_sparkline(const RenderTarget* target, int x, int y, int w, int h, const float* data1, const float* data2, int count, int cur_idx, float max_val, uint32_t color1, uint32_t color2) {
    if (!target || count <= 1 || w <= 4 || h <= 4) return;
    if (max_val < 1.0f) max_val = 1.0f;

    uint32_t fill1 = (color1 & 0x00FFFFFF) | 0x22000000;
    uint32_t fill2 = (color2 & 0x00FFFFFF) | 0x22000000;

    render_sparkline(target, x, y, w, h, data1, count, cur_idx, 0.0f, max_val, color1, fill1);
    render_sparkline(target, x, y, w, h, data2, count, cur_idx, 0.0f, max_val, color2, fill2);
}

// Procedural Vector Icons
void render_icon_clock(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int r = size / 2;
    render_ring(target, cx, cy, r, 2, 1.0f, 0.0f, 2.0f * PI_F, color, 0);
    render_circle(target, cx, cy, 2, color);
    render_fill_rect(target, cx - 1, cy - r + 3, 2, r - 3, color);      // minute hand
    render_fill_rect(target, cx, cy - 1, r / 2 + 1, 2, color);          // hour hand
}

void render_icon_cpu(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int s = (size * 6) / 10;
    int r = 2;
    render_rounded_rect(target, cx - s / 2, cy - s / 2, s, s, r, 0, color, 2);
    render_rounded_rect(target, cx - s / 4, cy - s / 4, s / 2, s / 2, 1, color, 0, 0);

    // External pins
    int pin_len = (size - s) / 2;
    int offset = s / 4;
    // Top & Bottom pins
    render_fill_rect(target, cx - offset, cy - s / 2 - pin_len, 2, pin_len, color);
    render_fill_rect(target, cx + offset, cy - s / 2 - pin_len, 2, pin_len, color);
    render_fill_rect(target, cx - offset, cy + s / 2, 2, pin_len, color);
    render_fill_rect(target, cx + offset, cy + s / 2, 2, pin_len, color);
    // Left & Right pins
    render_fill_rect(target, cx - s / 2 - pin_len, cy - offset, pin_len, 2, color);
    render_fill_rect(target, cx - s / 2 - pin_len, cy + offset, pin_len, 2, color);
    render_fill_rect(target, cx + s / 2, cy - offset, pin_len, 2, color);
    render_fill_rect(target, cx + s / 2, cy + offset, pin_len, 2, color);
}

void render_icon_ram(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int w = size;
    int h = (size * 5) / 10;
    int x0 = cx - w / 2;
    int y0 = cy - h / 2;
    render_rounded_rect(target, x0, y0, w, h, 2, 0, color, 2);

    // Memory chips
    int chip_w = (w - 10) / 3;
    int chip_h = h - 6;
    for (int i = 0; i < 3; i++) {
        render_fill_rect(target, x0 + 3 + i * (chip_w + 2), y0 + 3, chip_w, chip_h, color);
    }
}

void render_icon_battery(const RenderTarget* target, int cx, int cy, int size, int percent, bool is_charging, uint32_t color) {
    int bw = size;
    int bh = (size * 5) / 10;
    int x0 = cx - bw / 2;
    int y0 = cy - bh / 2;

    render_rounded_rect(target, x0, y0, bw - 3, bh, 3, 0, color, 2);
    // Battery cathode cap
    render_rounded_rect(target, x0 + bw - 3, y0 + bh / 4, 3, bh / 2, 1, color, 0, 0);

    // Internal level
    if (percent > 0) {
        int max_fill = bw - 9;
        int fill_w = (max_fill * percent) / 100;
        if (fill_w < 2) fill_w = 2;
        render_rounded_rect(target, x0 + 3, y0 + 3, fill_w, bh - 6, 1, color, 0, 0);
    }

    if (is_charging) {
        // Charging lightning bolt
        int lx = cx - 2;
        int ly = cy - bh / 2;
        render_fill_rect(target, lx + 2, ly + 2, 2, bh / 2, RGB(255, 255, 255));
        render_fill_rect(target, lx, ly + bh / 3, 4, 2, RGB(255, 255, 255));
        render_fill_rect(target, lx, ly + bh / 3 + 2, 2, bh / 2, RGB(255, 255, 255));
    }
}

void render_icon_wifi(const RenderTarget* target, int cx, int cy, int size, int level, uint32_t color, uint32_t track_color) {
    int bar_w = size / 6;
    int spacing = 2;
    int total_w = 4 * bar_w + 3 * spacing;
    int start_x = cx - total_w / 2;

    for (int i = 0; i < 4; i++) {
        int h = ((i + 1) * size) / 4;
        int bx = start_x + i * (bar_w + spacing);
        int by = cy + size / 2 - h;
        uint32_t col = (i < level) ? color : track_color;
        render_rounded_rect(target, bx, by, bar_w, h, 1, col, 0, 0);
    }
}

void render_icon_arrow_down(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int stem_h = (size * 6) / 10;
    int y0 = cy - size / 2;
    render_fill_rect(target, cx - 1, y0, 2, stem_h, color);

    for (int i = 0; i < 4; i++) {
        render_fill_rect(target, cx - i, y0 + stem_h - i, 1 + 2 * i, 1, color);
    }
}

void render_icon_arrow_up(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int stem_h = (size * 6) / 10;
    int y0 = cy - size / 2;

    for (int i = 0; i < 4; i++) {
        render_fill_rect(target, cx - i, y0 + i, 1 + 2 * i, 1, color);
    }
    render_fill_rect(target, cx - 1, y0 + 4, 2, stem_h, color);
}

void render_icon_temp(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int bulb_r = size / 4;
    int stem_w = bulb_r;
    int stem_h = size - bulb_r;
    int y0 = cy - size / 2;

    render_rounded_rect(target, cx - stem_w / 2, y0, stem_w, stem_h, stem_w / 2, 0, color, 1);
    render_circle(target, cx, cy + size / 2 - bulb_r, bulb_r, color);
    render_fill_rect(target, cx - 1, y0 + stem_h / 2, 2, stem_h / 2, color);
}

void render_icon_storage(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int w = (size * 8) / 10;
    int h = (size * 7) / 10;
    render_rounded_rect(target, cx - w / 2, cy - h / 2, w, h, 2, 0, color, 2);
    render_fill_rect(target, cx - w / 2 + 3, cy - 1, w - 6, 2, color);
    render_circle(target, cx + w / 4, cy + h / 4, 1, color);
}

void render_icon_engine(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int r = size / 3;
    render_circle(target, cx, cy, r, color);
    render_circle(target, cx, cy, r / 2, RGB(10, 11, 14));

    // 4 gear teeth
    int tw = 3;
    int th = size / 2;
    render_fill_rect(target, cx - tw / 2, cy - th, tw, size, color);
    render_fill_rect(target, cx - th, cy - tw / 2, size, tw, color);
}

void render_icon_heart(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int r = size / 4;
    render_circle(target, cx - r, cy - r / 2, r, color);
    render_circle(target, cx + r, cy - r / 2, r, color);

    for (int y = 0; y < size / 2; y++) {
        int w = size - (y * 2);
        if (w > 0) {
            render_fill_rect(target, cx - w / 2, cy + y, w, 1, color);
        }
    }
}

void render_icon_warning(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    for (int y = 0; y < size; y++) {
        int w = (y * 2) / 3;
        render_fill_rect(target, cx - w / 2, cy - size / 2 + y, w, 1, color);
    }
    // Exclamation mark
    render_fill_rect(target, cx - 1, cy - size / 4, 2, size / 3, RGB(10, 11, 14));
    render_fill_rect(target, cx - 1, cy + size / 4, 2, 2, RGB(10, 11, 14));
}
