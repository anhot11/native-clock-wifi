#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include "text.h"

#define PI_F 3.14159265358979323846f

// RGBA 8888 Little Endian
#ifndef RGBA
#define RGBA(r, g, b, a) (((uint32_t)(r) & 0xFF) | (((uint32_t)(g) & 0xFF) << 8) | (((uint32_t)(b) & 0xFF) << 16) | (((uint32_t)(a) & 0xFF) << 24))
#endif
#ifndef RGB
#define RGB(r, g, b) RGBA(r, g, b, 255)
#endif

// Color blending helpers
static inline uint32_t color_alpha(uint32_t color, uint8_t alpha) {
    return (color & 0x00FFFFFF) | (((uint32_t)alpha) << 24);
}

static inline uint32_t color_blend(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    int r1 = c1 & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF, a1 = (c1 >> 24) & 0xFF;
    int r2 = c2 & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF, a2 = (c2 >> 24) & 0xFF;
    int r = r1 + (int)((float)(r2 - r1) * t);
    int g = g1 + (int)((float)(g2 - g1) * t);
    int b = b1 + (int)((float)(b2 - b1) * t);
    int a = a1 + (int)((float)(a2 - a1) * t);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    if (a < 0) a = 0; else if (a > 255) a = 255;
    return RGBA(r, g, b, a);
}

// Primitives
void render_clear(const RenderTarget* target, uint32_t color);
void render_fill_rect(const RenderTarget* target, int x, int y, int w, int h, uint32_t color);
void render_rounded_rect(const RenderTarget* target, int x, int y, int w, int h, int radius, uint32_t bg, uint32_t border, int border_w);
void render_glass_card(const RenderTarget* target, int x, int y, int w, int h, int radius, uint32_t bg_top, uint32_t bg_bot, uint32_t border, uint32_t shadow);
void render_circle(const RenderTarget* target, int cx, int cy, int radius, uint32_t color);

// Rings and Arcs (Anti-aliased with smooth rounded caps)
void render_ring(const RenderTarget* target, int cx, int cy, int radius, int thickness, float percent, float start_angle, float sweep_angle, uint32_t active_color, uint32_t track_color);
void render_segmented_bar(const RenderTarget* target, int x, int y, int w, int h, int segments, int filled_segments, uint32_t active_color, uint32_t track_color, int gap);

// Sparkline & Charts
void render_sparkline(const RenderTarget* target, int x, int y, int w, int h, const float* data, int count, int cur_idx, float min_val, float max_val, uint32_t line_color, uint32_t fill_color);
void render_dual_sparkline(const RenderTarget* target, int x, int y, int w, int h, const float* data1, const float* data2, int count, int cur_idx, float max_val, uint32_t color1, uint32_t color2);

// Procedural Vector Icons (Pure C graphics)
void render_icon_clock(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_cpu(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_ram(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_battery(const RenderTarget* target, int cx, int cy, int size, int percent, bool is_charging, uint32_t color);
void render_icon_wifi(const RenderTarget* target, int cx, int cy, int size, int level, uint32_t color, uint32_t track_color);
void render_icon_arrow_down(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_arrow_up(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_temp(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_storage(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_engine(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_heart(const RenderTarget* target, int cx, int cy, int size, uint32_t color);
void render_icon_warning(const RenderTarget* target, int cx, int cy, int size, uint32_t color);

// Embedded High-Resolution User Artwork (240x240 Bilinear + SDF Rounded Corners)
void render_art_init_jni(void* jni_env_ptr);
void render_art_card(const RenderTarget* target, int art_idx, int x, int y, int w, int h, int radius, uint32_t border_col);

#endif // RENDER_H

