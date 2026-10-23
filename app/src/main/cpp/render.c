#include "render.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

static inline void fill_span_opaque(uint32_t* row, int count, uint32_t color) {
    if (count <= 0) return;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    uint32x4_t col4 = vdupq_n_u32(color);
    int i = 0;
    for (; i <= count - 8; i += 8) {
        vst1q_u32(&row[i], col4);
        vst1q_u32(&row[i + 4], col4);
    }
    for (; i <= count - 4; i += 4) {
        vst1q_u32(&row[i], col4);
    }
    for (; i < count; i++) {
        row[i] = color;
    }
#else
    for (int i = 0; i < count; i++) {
        row[i] = color;
    }
#endif
}

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

    uint32_t inv_sa = 255 - sa;
    uint32_t r = (sr * sa + dr * inv_sa) >> 8;
    uint32_t g = (sg * sa + dg * inv_sa) >> 8;
    uint32_t b = (sb * sa + db * inv_sa) >> 8;

    *p = RGBA(r, g, b, 255);
}

static inline void put_pixel_coverage(const RenderTarget* target, int x, int y, uint32_t color, float coverage) {
    if (coverage <= 0.005f) return;
    if (coverage >= 0.995f) {
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
    fill_span_opaque(target->pixels, total_pixels, color);
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
        int count = x1 - x0;
        for (int y = y0; y < y1; y++) {
            fill_span_opaque(&target->pixels[y * target->stride + x0], count, color);
        }
    } else if (a > 0) {
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                put_pixel_blend(target, x, y, color);
            }
        }
    }
}

static inline void eval_sdf_corner_pixel(const RenderTarget* target, int px, int py, float qx, float qy, float r_f, uint32_t bg, uint32_t border, int border_w) {
    float dist;
    if (qx > 0.0f && qy > 0.0f) {
        dist = sqrtf(qx * qx + qy * qy) - r_f;
    } else {
        dist = (qx > qy ? qx : qy) - r_f;
    }
    if (dist > 1.0f) return;

    float coverage = 0.5f - dist;
    if (coverage > 1.0f) coverage = 1.0f;
    if (coverage < 0.0f) coverage = 0.0f;

    if (border_w > 0 && border != 0) {
        float bw_half = (float)border_w * 0.5f;
        float border_dist = fabsf(dist + bw_half) - bw_half;
        float border_cov = 0.5f - border_dist;
        if (border_cov > 1.0f) border_cov = 1.0f;
        if (border_cov < 0.0f) border_cov = 0.0f;

        if (dist <= 0.0f) {
            if (bg != 0) put_pixel_coverage(target, px, py, bg, coverage);
            if (border_cov > 0.0f) put_pixel_coverage(target, px, py, border, border_cov);
        } else {
            put_pixel_coverage(target, px, py, border, border_cov * coverage);
        }
    } else if (bg != 0) {
        put_pixel_coverage(target, px, py, bg, coverage);
    }
}

// Fast SDF Anti-aliased Rounded Rectangle (NEON span-accelerated interior)
void render_rounded_rect(const RenderTarget* target, int x, int y, int w, int h, int radius, uint32_t bg, uint32_t border, int border_w) {
    if (!target || !target->pixels || w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    int x0 = (x < 0) ? 0 : x;
    int x1 = (x + w > target->width) ? target->width : x + w;
    int y0 = (y < 0) ? 0 : y;
    int y1 = (y + h > target->height) ? target->height : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    float half_w = (float)w * 0.5f;
    float half_h = (float)h * 0.5f;
    float cx = (float)x + half_w;
    float cy = (float)y + half_h;
    float r_f = (float)radius;
    float bx = half_w - r_f;
    float by = half_h - r_f;

    int inner_x0 = x + radius + border_w + 1;
    int inner_x1 = x + w - radius - border_w - 1;
    if (inner_x0 < x0) inner_x0 = x0;
    if (inner_x1 > x1) inner_x1 = x1;

    uint32_t bg_a = (bg >> 24) & 0xFF;

    for (int py = y0; py < y1; py++) {
        float qy = fabsf((float)py + 0.5f - cy) - by;
        bool is_middle_row = (py >= y + radius + border_w && py < y + h - radius - border_w);

        if (is_middle_row && inner_x1 > inner_x0) {
            int left_end = x + border_w + 1;
            if (left_end > x1) left_end = x1;
            for (int px = x0; px < left_end; px++) {
                float qx = fabsf((float)px + 0.5f - cx) - bx;
                eval_sdf_corner_pixel(target, px, py, qx, qy, r_f, bg, border, border_w);
            }

            int right_start = x + w - border_w - 1;
            if (right_start < left_end) right_start = left_end;
            if (bg_a == 255) {
                fill_span_opaque(&target->pixels[py * target->stride + left_end], right_start - left_end, bg);
            } else if (bg_a > 0) {
                for (int px = left_end; px < right_start; px++) {
                    put_pixel_blend(target, px, py, bg);
                }
            }

            for (int px = right_start; px < x1; px++) {
                float qx = fabsf((float)px + 0.5f - cx) - bx;
                eval_sdf_corner_pixel(target, px, py, qx, qy, r_f, bg, border, border_w);
            }
        } else {
            for (int px = x0; px < x1; px++) {
                if (px >= inner_x0 && px < inner_x1 && py >= y + border_w + 1 && py < y + h - border_w - 1) {
                    if (bg_a == 255) {
                        fill_span_opaque(&target->pixels[py * target->stride + inner_x0], inner_x1 - inner_x0, bg);
                    } else if (bg_a > 0) {
                        for (int k = inner_x0; k < inner_x1; k++) put_pixel_blend(target, k, py, bg);
                    }
                    px = inner_x1 - 1;
                    continue;
                }
                float qx = fabsf((float)px + 0.5f - cx) - bx;
                eval_sdf_corner_pixel(target, px, py, qx, qy, r_f, bg, border, border_w);
            }
        }
    }
}

// Fast NEON Glassmorphism Card with vertical gradient, 1px AA border, and bottom drop shadow
void render_glass_card(const RenderTarget* target, int x, int y, int w, int h, int radius, uint32_t bg_top, uint32_t bg_bot, uint32_t border, uint32_t shadow) {
    if (!target || !target->pixels || w <= 0 || h <= 0) return;

    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    // Only draw bottom drop-shadow strip (3px below card) instead of full card area
    if (shadow != 0 && radius > 0) {
        render_fill_rect(target, x + radius / 2, y + h, w - radius, 3, shadow);
    }

    int x0 = (x < 0) ? 0 : x;
    int x1 = (x + w > target->width) ? target->width : x + w;
    int y0 = (y < 0) ? 0 : y;
    int y1 = (y + h > target->height) ? target->height : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    float half_w = (float)w * 0.5f;
    float half_h = (float)h * 0.5f;
    float cx = (float)x + half_w;
    float cy = (float)y + half_h;
    float r_f = (float)radius;
    float bx = half_w - r_f;
    float by = half_h - r_f;
    float inv_h = 1.0f / (float)h;

    int corner_left_end = x + radius + 1;
    int corner_right_start = x + w - radius - 1;
    if (corner_left_end < x0) corner_left_end = x0;
    if (corner_right_start > x1) corner_right_start = x1;

    for (int py = y0; py < y1; py++) {
        float t = (float)(py - y) * inv_h;
        uint32_t row_bg = color_blend(bg_top, bg_bot, t) | 0xFF000000U;
        float qy = fabsf((float)py + 0.5f - cy) - by;

        if (qy <= 0.0f) {
            // Fast path for flat vertical side rows (80%+ of card height!)
            if (x >= 0 && x < target->width) {
                put_pixel_blend(target, x, py, border ? border : row_bg);
            }
            int span_start = (x + 1 > x0) ? (x + 1) : x0;
            int span_end = (x + w - 1 < x1) ? (x + w - 1) : x1;
            if (span_end > span_start) {
                fill_span_opaque(&target->pixels[py * target->stride + span_start], span_end - span_start, row_bg);
            }
            if (x + w - 1 >= 0 && x + w - 1 < target->width) {
                put_pixel_blend(target, x + w - 1, py, border ? border : row_bg);
            }
        } else {
            // Top / bottom rounded corner rows: only evaluate sqrtf in the two corners
            for (int px = x0; px < corner_left_end && px < x1; px++) {
                float qx = fabsf((float)px + 0.5f - cx) - bx;
                eval_sdf_corner_pixel(target, px, py, qx, qy, r_f, row_bg, border, 1);
            }
            if (corner_right_start > corner_left_end) {
                if (py == y || py == y + h - 1) {
                    uint32_t edge_col = border ? border : row_bg;
                    for (int px = corner_left_end; px < corner_right_start; px++) {
                        put_pixel_blend(target, px, py, edge_col);
                    }
                } else {
                    fill_span_opaque(&target->pixels[py * target->stride + corner_left_end],
                                     corner_right_start - corner_left_end, row_bg);
                }
            }
            int right_begin = (corner_right_start > corner_left_end) ? corner_right_start : corner_left_end;
            for (int px = right_begin; px < x1; px++) {
                float qx = fabsf((float)px + 0.5f - cx) - bx;
                eval_sdf_corner_pixel(target, px, py, qx, qy, r_f, row_bg, border, 1);
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
    float r_f = (float)radius;
    float r_max_sq = (r_f + 1.0f) * (r_f + 1.0f);

    for (int y = min_y; y <= max_y; y++) {
        float dy = (float)y + 0.5f - (float)cy;
        float dy2 = dy * dy;
        for (int x = min_x; x <= max_x; x++) {
            float dx = (float)x + 0.5f - (float)cx;
            float d2 = dx * dx + dy2;
            if (d2 > r_max_sq) continue;
            float dist = sqrtf(d2) - r_f;
            float cov = 0.5f - dist;
            if (cov > 1.0f) cov = 1.0f;
            if (cov > 0.0f) put_pixel_coverage(target, x, y, color, cov);
        }
    }
}

// Fast Anti-aliased Ring / Gauge Arc with squared-radius early culling
void render_ring(const RenderTarget* target, int cx, int cy, int radius, int thickness, float percent, float start_angle, float sweep_angle, uint32_t active_color, uint32_t track_color) {
    if (!target || radius <= thickness || sweep_angle <= 0.0f) return;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    float r_mid = (float)radius - (float)thickness * 0.5f;
    float half_thick = (float)thickness * 0.5f;
    float active_sweep = sweep_angle * percent;

    float r_inner = r_mid - half_thick - 1.0f;
    if (r_inner < 0.0f) r_inner = 0.0f;
    float r_outer = r_mid + half_thick + 1.0f;
    float r_min_sq = r_inner * r_inner;
    float r_max_sq = r_outer * r_outer;

    int bb_min_x = cx - radius - 2;
    int bb_max_x = cx + radius + 2;
    int bb_min_y = cy - radius - 2;
    int bb_max_y = cy + radius + 2;

    bool full_circle_active = (percent >= 0.999f && sweep_angle >= 2.0f * PI_F - 0.01f);

    for (int y = bb_min_y; y <= bb_max_y; y++) {
        float dy = (float)y + 0.5f - (float)cy;
        float dy2 = dy * dy;
        for (int x = bb_min_x; x <= bb_max_x; x++) {
            float dx = (float)x + 0.5f - (float)cx;
            float r2 = dx * dx + dy2;
            if (r2 < r_min_sq || r2 > r_max_sq) continue;

            float dist_from_center = sqrtf(r2);
            float dist_from_mid = fabsf(dist_from_center - r_mid);
            float ring_cov = 0.5f - (dist_from_mid - half_thick);
            if (ring_cov <= 0.0f) continue;
            if (ring_cov > 1.0f) ring_cov = 1.0f;

            if (full_circle_active) {
                put_pixel_coverage(target, x, y, active_color, ring_cov);
                continue;
            }

            float angle = atan2f(dy, dx);
            float rel_angle = angle - start_angle;
            while (rel_angle < 0.0f) rel_angle += 2.0f * PI_F;
            while (rel_angle >= 2.0f * PI_F) rel_angle -= 2.0f * PI_F;

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

    uint32_t fill1 = (color1 & 0x00FFFFFF) | 0x24000000;
    uint32_t fill2 = (color2 & 0x00FFFFFF) | 0x24000000;

    render_sparkline(target, x, y, w, h, data1, count, cur_idx, 0.0f, max_val, color1, fill1);
    render_sparkline(target, x, y, w, h, data2, count, cur_idx, 0.0f, max_val, color2, fill2);
}

// Procedural Vector Icons
void render_icon_clock(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int r = size / 2;
    render_ring(target, cx, cy, r, 2, 1.0f, 0.0f, 2.0f * PI_F, color, 0);
    render_circle(target, cx, cy, 2, color);
    render_fill_rect(target, cx - 1, cy - r + 3, 2, r - 3, color);
    render_fill_rect(target, cx, cy - 1, r / 2 + 1, 2, color);
}

void render_icon_cpu(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    int s = (size * 6) / 10;
    render_rounded_rect(target, cx - s / 2, cy - s / 2, s, s, 2, 0, color, 2);
    render_fill_rect(target, cx - s / 4, cy - s / 4, s / 2, s / 2, color);

    int pin_len = (size - s) / 2;
    int offset = s / 4;
    render_fill_rect(target, cx - offset, cy - s / 2 - pin_len, 2, pin_len, color);
    render_fill_rect(target, cx + offset, cy - s / 2 - pin_len, 2, pin_len, color);
    render_fill_rect(target, cx - offset, cy + s / 2, 2, pin_len, color);
    render_fill_rect(target, cx + offset, cy + s / 2, 2, pin_len, color);
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

    render_rounded_rect(target, x0, y0, bw - 3, bh, 2, 0, color, 2);
    render_fill_rect(target, x0 + bw - 3, y0 + bh / 4, 3, bh / 2, color);

    if (percent > 0) {
        int max_fill = bw - 9;
        int fill_w = (max_fill * percent) / 100;
        if (fill_w < 2) fill_w = 2;
        render_fill_rect(target, x0 + 3, y0 + 3, fill_w, bh - 6, color);
    }

    if (is_charging) {
        int lx = cx - 2;
        int ly = cy - bh / 2;
        render_fill_rect(target, lx + 2, ly + 1, 2, bh / 2, RGB(255, 255, 255));
        render_fill_rect(target, lx, ly + bh / 3, 4, 2, RGB(255, 255, 255));
        render_fill_rect(target, lx, ly + bh / 3 + 2, 2, bh / 2, RGB(255, 255, 255));
    }
}

void render_icon_wifi(const RenderTarget* target, int cx, int cy, int size, int level, uint32_t color, uint32_t track_color) {
    int bar_w = size / 6;
    if (bar_w < 2) bar_w = 2;
    int spacing = 2;
    int total_w = 4 * bar_w + 3 * spacing;
    int start_x = cx - total_w / 2;

    for (int i = 0; i < 4; i++) {
        int h = ((i + 1) * size) / 4;
        int bx = start_x + i * (bar_w + spacing);
        int by = cy + size / 2 - h;
        uint32_t col = (i < level) ? color : track_color;
        render_fill_rect(target, bx, by, bar_w, h, col);
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
        if (w > 0) render_fill_rect(target, cx - w / 2, cy + y, w, 1, color);
    }
}

void render_icon_warning(const RenderTarget* target, int cx, int cy, int size, uint32_t color) {
    for (int y = 0; y < size; y++) {
        int w = (y * 2) / 3;
        render_fill_rect(target, cx - w / 2, cy - size / 2 + y, w, 1, color);
    }
    render_fill_rect(target, cx - 1, cy - size / 4, 2, size / 3, RGB(10, 11, 14));
    render_fill_rect(target, cx - 1, cy + size / 4, 2, 2, RGB(10, 11, 14));
}

#include "embedded_art.h"

void render_art_init_jni(void* jni_env_ptr) {
    // Zero-RAM ROM-indexed artwork: stays in .rodata, requires 0 bytes of RAM and 0 JNI allocations!
    (void)jni_env_ptr;
}

static inline void unpack_rgb565(uint16_t c, uint32_t* r, uint32_t* g, uint32_t* b) {
    *r = ((c >> 11) & 0x1F) * 255 / 31;
    *g = ((c >> 5)  & 0x3F) * 255 / 63;
    *b = (c         & 0x1F) * 255 / 31;
}

static inline uint32_t sample_art_bilinear_fp(const uint16_t* pal, const uint8_t* idx_tex, int fx_fp, int fy_fp) {
    int x0 = fx_fp >> 16;
    int y0 = fy_fp >> 16;
    if (x0 < 0) x0 = 0;
    else if (x0 >= ART_DIM - 1) x0 = ART_DIM - 2;
    if (y0 < 0) y0 = 0;
    else if (y0 >= ART_DIM - 1) y0 = ART_DIM - 2;

    int wx = (fx_fp >> 8) & 0xFF;
    int wy = (fy_fp >> 8) & 0xFF;
    int iwx = 256 - wx;
    int iwy = 256 - wy;

    const uint8_t* row0 = &idx_tex[y0 * ART_DIM + x0];
    const uint8_t* row1 = row0 + ART_DIM;

    uint32_t r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
    unpack_rgb565(pal[row0[0]], &r00, &g00, &b00);
    unpack_rgb565(pal[row0[1]], &r10, &g10, &b10);
    unpack_rgb565(pal[row1[0]], &r01, &g01, &b01);
    unpack_rgb565(pal[row1[1]], &r11, &g11, &b11);

    int w00 = (iwx * iwy) >> 8;
    int w10 = (wx  * iwy) >> 8;
    int w01 = (iwx * wy)  >> 8;
    int w11 = (wx  * wy)  >> 8;

    uint32_t r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 8;
    uint32_t g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 8;
    uint32_t b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 8;

    return RGBA(r, g, b, 255);
}

void render_art_card(const RenderTarget* target, int art_idx, int x, int y, int w, int h, int radius, uint32_t border_col) {
    if (!target || !target->pixels || w <= 0 || h <= 0) return;
    if (art_idx < 0) art_idx = 0;
    art_idx = art_idx % NUM_EMBEDDED_ARTS;

    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    int x0 = (x < 0) ? 0 : x;
    int x1 = (x + w > target->width) ? target->width : x + w;
    int y0 = (y < 0) ? 0 : y;
    int y1 = (y + h > target->height) ? target->height : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    const uint16_t* pal = g_art_pal[art_idx];
    const uint8_t* idx_tex = g_art_idx[art_idx];
    float half_w = (float)w * 0.5f;
    float half_h = (float)h * 0.5f;
    float cx = (float)x + half_w;
    float cy = (float)y + half_h;
    float r_f = (float)radius;
    float bx = half_w - r_f;
    float by = half_h - r_f;

    int step_x_fp = ((ART_DIM - 1) << 16) / w;
    int step_y_fp = ((ART_DIM - 1) << 16) / h;

    for (int py = y0; py < y1; py++) {
        int fy_fp = (py - y) * step_y_fp + (step_y_fp >> 1);
        float qy = fabsf((float)py + 0.5f - cy) - by;
        bool is_mid_y = (qy <= -1.0f);
        uint32_t* dst_row = &target->pixels[py * target->stride];

        int fx_fp = (x0 - x) * step_x_fp + (step_x_fp >> 1);
        for (int px = x0; px < x1; px++, fx_fp += step_x_fp) {
            if (is_mid_y && px > x + 1 && px < x + w - 2) {
                dst_row[px] = sample_art_bilinear_fp(pal, idx_tex, fx_fp, fy_fp);
                continue;
            }
            float qx = fabsf((float)px + 0.5f - cx) - bx;
            if (qx <= -1.0f && qy <= -1.0f) {
                dst_row[px] = sample_art_bilinear_fp(pal, idx_tex, fx_fp, fy_fp);
                continue;
            }
            uint32_t tex_col = sample_art_bilinear_fp(pal, idx_tex, fx_fp, fy_fp);
            eval_sdf_corner_pixel(target, px, py, qx, qy, r_f, tex_col, border_col, border_col ? 1 : 0);
        }
    }
}

// 5x7 Bitwise ROM Table for Zero-RAM ASCII Clock (0..9 and ':')
static const uint8_t ASCII_CLOCK_GLYPHS[11][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
    { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, // 2
    { 0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E }, // 3
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, // 7
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9
    { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 }  // :
};

int render_ascii_clock(const RenderTarget* target, const char* str, int x, int y, int cell_px, int ascii_mode, uint32_t on_color, uint32_t off_color) {
    if (!target || !target->pixels || !str || cell_px <= 1) return 0;
    int cur_x = x;
    int gap_px = cell_px + (cell_px >> 1);
    uint32_t glow_bg = color_alpha(on_color, 55);

    for (const char* p = str; *p; p++) {
        int idx = -1;
        int cols = 5;
        if (*p >= '0' && *p <= '9') {
            idx = *p - '0';
        } else if (*p == ':') {
            idx = 10;
            cols = 3;
        } else {
            cur_x += cols * cell_px;
            continue;
        }

        const uint8_t* rows = ASCII_CLOCK_GLYPHS[idx];
        for (int r = 0; r < 7; r++) {
            uint8_t row_bits = rows[r];
            int cy = y + r * cell_px;
            for (int c = 0; c < cols; c++) {
                int bit_pos = (cols == 3) ? (3 - c) : (4 - c);
                bool is_on = ((row_bits >> bit_pos) & 1) != 0;
                int cx = cur_x + c * cell_px;
                int bw = cell_px - 1;
                if (bw < 2) bw = 2;

                if (is_on) {
                    if (ascii_mode == 0) {
                        // Mode 0: Crisp '#' ASCII Terminal Matrix
                        render_fill_rect(target, cx, cy, bw, bw, glow_bg);
                        if (bw >= 5) {
                            render_fill_rect(target, cx + 1, cy, 1, bw, on_color);
                            render_fill_rect(target, cx + bw - 2, cy, 1, bw, on_color);
                            render_fill_rect(target, cx, cy + 1, bw, 1, on_color);
                            render_fill_rect(target, cx, cy + bw - 2, bw, 1, on_color);
                        } else {
                            render_fill_rect(target, cx, cy, bw, bw, on_color);
                        }
                    } else if (ascii_mode == 1) {
                        // Mode 1: Solid '█' Cyber Block with Scanline
                        render_fill_rect(target, cx, cy, bw, bw, on_color);
                    } else {
                        // Mode 2: '1/0' Binary Matrix
                        render_fill_rect(target, cx, cy, bw, bw, glow_bg);
                        render_fill_rect(target, cx + bw / 2, cy, 2, bw, on_color);
                    }
                } else if (off_color != 0) {
                    // Subtle terminal background grid dot '.'
                    render_fill_rect(target, cx + bw / 2, cy + bw / 2, 1, 1, off_color);
                }
            }
        }
        cur_x += cols * cell_px + ((cols == 3) ? (cell_px / 2) : gap_px);
    }
    return cur_x - x;
}

