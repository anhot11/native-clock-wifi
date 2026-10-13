#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FONT_XS = 0,   // ~11dp: tiny uppercase labels, tags, indicators
    FONT_SM = 1,   // ~13dp: secondary metrics, table rows
    FONT_MD = 2,   // ~17dp: card headers, primary values
    FONT_LG = 3,   // ~24dp: major metrics, ring center values
    FONT_XL = 4,   // ~34dp: big telemetry numbers (speed, etc.)
    FONT_HERO = 5, // ~52dp: hero clock display (HH:MM)
    FONT_COUNT = 6
} FontSize;

typedef struct {
    uint32_t* pixels;
    int width;
    int height;
    int stride;
    float dp_scale;
} RenderTarget;

bool text_init(float dp_scale);
void text_cleanup(void);

// Text measurements
int text_width(const char* str, FontSize size);
int text_height(FontSize size);

// Drawing primitives
void text_draw(const RenderTarget* target, const char* str, int x, int y, FontSize size, uint32_t color);
void text_draw_tabular(const RenderTarget* target, const char* str, int x, int y, FontSize size, uint32_t color);
void text_draw_centered(const RenderTarget* target, const char* str, int cx, int y, FontSize size, uint32_t color);
void text_draw_right(const RenderTarget* target, const char* str, int rx, int y, FontSize size, uint32_t color);

#endif // TEXT_H
