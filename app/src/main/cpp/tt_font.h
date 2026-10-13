#ifndef TT_FONT_H
#define TT_FONT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TT_SIZE_XS,   // ~12dp: captions, badges, metadata
    TT_SIZE_SM,   // ~14dp: card headers, subheadings
    TT_SIZE_MD,   // ~18dp: regular text, values, buttons
    TT_SIZE_LG,   // ~26dp: main titles, card titles
    TT_SIZE_XL,   // ~36dp: large speed numbers
    TT_SIZE_XXL,  // ~52dp: hero clock display
    TT_NUM_SIZES
} TTFontSize;

// Forward declaration
typedef struct UIRenderer UIRenderer;

bool tt_font_init(float dp_scale);
void tt_font_cleanup(void);

// Drawing functions
void tt_font_draw(UIRenderer* ui, const char* text, int x, int y, TTFontSize size, uint32_t color);
void tt_font_draw_centered(UIRenderer* ui, const char* text, int cx, int y, TTFontSize size, uint32_t color);
int tt_font_get_width(const char* text, TTFontSize size);
int tt_font_get_height(TTFontSize size);

#endif // TT_FONT_H
