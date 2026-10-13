#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <android/native_window.h>
#include <stdint.h>
#include <stdbool.h>
#include "wifi_stats.h"

typedef struct {
    uint32_t bg_color;
    uint32_t card_bg;
    uint32_t card_border;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_muted;
    uint32_t accent;
    uint32_t accent_glow;
    uint32_t success;
    uint32_t warning;
    uint32_t danger;
    const char* theme_name;
} UITheme;

typedef struct UIRenderer {
    int width;
    int height;
    int stride;
    uint32_t* pixels;
    int theme_index;
    float anim_phase;       // 0.0 to 1.0 for pulsing animations
    int touch_x;
    int touch_y;
    float touch_ripple;     // 0.0 to 1.0
    int fps;
    float dp_scale;
    bool fonts_initialized;
} UIRenderer;

void ui_renderer_init(UIRenderer* ui);
void ui_renderer_set_buffer(UIRenderer* ui, ANativeWindow_Buffer* buffer);
void ui_renderer_cycle_theme(UIRenderer* ui);
void ui_renderer_on_touch(UIRenderer* ui, int x, int y);
void ui_renderer_draw_frame(UIRenderer* ui, const WifiStats* wifi, float dt);

#endif // UI_RENDERER_H
