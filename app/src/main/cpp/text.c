#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "text.h"
#include "embedded_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RGBA(r, g, b, a) (((uint32_t)(r) & 0xFF) | (((uint32_t)(g) & 0xFF) << 8) | (((uint32_t)(b) & 0xFF) << 16) | (((uint32_t)(a) & 0xFF) << 24))

typedef struct {
    int width;
    int height;
    int xoff;
    int yoff;
    int xadvance;
    uint8_t* alpha; // width * height
} GlyphBitmap;

typedef struct {
    int pixel_height;
    int ascent;
    int descent;
    int line_gap;
    int digit_width; // for tabular digits
    GlyphBitmap ascii[96];      // 32..127
    GlyphBitmap latin1[96];     // 160..255 (á, é, í, ó, ú, ñ, °, µ, etc.)
} FontAtlas;

static FontAtlas g_atlases[FONT_COUNT];
static bool g_font_initialized = false;
static stbtt_fontinfo g_font_info;

static const float BASE_DP_SIZES[FONT_COUNT] = {
    11.0f, // FONT_XS
    13.0f, // FONT_SM
    17.0f, // FONT_MD
    24.0f, // FONT_LG
    34.0f, // FONT_XL
    52.0f  // FONT_HERO
};

static uint32_t decode_utf8(const char** p) {
    const unsigned char* s = (const unsigned char*)*p;
    if (!*s) return 0;
    
    if (s[0] < 0x80) {
        (*p)++;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *p += 2;
        return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *p += 3;
        return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    (*p)++;
    return '?';
}

static void free_glyph(GlyphBitmap* g) {
    if (g->alpha) {
        stbtt_FreeBitmap(g->alpha, NULL);
        g->alpha = NULL;
    }
    g->width = 0;
    g->height = 0;
}

static void rasterize_glyph(stbtt_fontinfo* info, float scale, int cp, GlyphBitmap* g) {
    int advance, lsb;
    stbtt_GetCodepointHMetrics(info, cp, &advance, &lsb);
    g->xadvance = (int)ceilf(advance * scale);

    if (cp == ' ' || cp == 0xA0) {
        g->width = 0;
        g->height = 0;
        g->xoff = 0;
        g->yoff = 0;
        g->alpha = NULL;
        return;
    }

    g->alpha = stbtt_GetCodepointBitmap(
        info, 0, scale, cp,
        &g->width, &g->height,
        &g->xoff, &g->yoff
    );
}

bool text_init(float dp_scale) {
    if (dp_scale < 1.0f) dp_scale = 1.0f;

    if (g_font_initialized) {
        text_cleanup();
    }

    if (!stbtt_InitFont(&g_font_info, g_embedded_font_ttf, 0)) {
        return false;
    }

    for (int s = 0; s < FONT_COUNT; s++) {
        FontAtlas* atlas = &g_atlases[s];
        atlas->pixel_height = (int)(BASE_DP_SIZES[s] * dp_scale);
        if (atlas->pixel_height < 10) atlas->pixel_height = 10;

        float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)atlas->pixel_height);

        int ascent_raw, descent_raw, line_gap_raw;
        stbtt_GetFontVMetrics(&g_font_info, &ascent_raw, &descent_raw, &line_gap_raw);
        atlas->ascent = (int)ceilf(ascent_raw * scale);
        atlas->descent = (int)ceilf(descent_raw * scale);
        atlas->line_gap = (int)ceilf(line_gap_raw * scale);

        // Rasterize ASCII
        for (int ch = 32; ch < 128; ch++) {
            rasterize_glyph(&g_font_info, scale, ch, &atlas->ascii[ch - 32]);
        }

        // Tabular digit width: fixed advance from '0'
        atlas->digit_width = atlas->ascii['0' - 32].xadvance;

        // Rasterize Latin-1 supplement (160..255)
        for (int ch = 160; ch < 256; ch++) {
            rasterize_glyph(&g_font_info, scale, ch, &atlas->latin1[ch - 160]);
        }
    }

    g_font_initialized = true;
    return true;
}

void text_cleanup(void) {
    if (!g_font_initialized) return;

    for (int s = 0; s < FONT_COUNT; s++) {
        FontAtlas* atlas = &g_atlases[s];
        for (int ch = 0; ch < 96; ch++) {
            free_glyph(&atlas->ascii[ch]);
            free_glyph(&atlas->latin1[ch]);
        }
    }
    g_font_initialized = false;
}

static const GlyphBitmap* get_glyph(const FontAtlas* atlas, uint32_t cp) {
    if (cp >= 32 && cp < 128) {
        return &atlas->ascii[cp - 32];
    }
    if (cp >= 160 && cp < 256) {
        return &atlas->latin1[cp - 160];
    }
    // Fallback: question mark
    return &atlas->ascii['?' - 32];
}

int text_width(const char* str, FontSize size) {
    if (!g_font_initialized || !str || size < 0 || size >= FONT_COUNT) return 0;
    const FontAtlas* atlas = &g_atlases[size];
    int total_w = 0;
    const char* p = str;
    while (*p) {
        uint32_t cp = decode_utf8(&p);
        if (cp == 0) break;
        const GlyphBitmap* g = get_glyph(atlas, cp);
        total_w += g->xadvance;
    }
    return total_w;
}

int text_height(FontSize size) {
    if (!g_font_initialized || size < 0 || size >= FONT_COUNT) return 16;
    return g_atlases[size].pixel_height;
}

static void draw_text_internal(const RenderTarget* target, const char* str, int x, int y, FontSize size, uint32_t color, bool tabular) {
    if (!g_font_initialized || !target || !target->pixels || !str || size < 0 || size >= FONT_COUNT) return;

    uint32_t sa = (color >> 24) & 0xFF;
    if (sa == 0) return;
    uint32_t sr = color & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = (color >> 16) & 0xFF;

    const FontAtlas* atlas = &g_atlases[size];
    int baseline_y = y + atlas->ascent;
    int cur_x = x;
    const char* p = str;

    while (*p) {
        uint32_t cp = decode_utf8(&p);
        if (cp == 0) break;
        if (cp == '\n') {
            baseline_y += atlas->pixel_height + atlas->line_gap;
            cur_x = x;
            continue;
        }

        const GlyphBitmap* g = get_glyph(atlas, cp);
        int advance = g->xadvance;
        int x_offset = 0;

        if (tabular && cp >= '0' && cp <= '9') {
            advance = atlas->digit_width;
            x_offset = (advance - g->xadvance) / 2;
        }

        if (g->alpha && g->width > 0 && g->height > 0) {
            int gx0 = cur_x + x_offset + g->xoff;
            int gy0 = baseline_y + g->yoff;

            for (int gy = 0; gy < g->height; gy++) {
                int py = gy0 + gy;
                if (py < 0 || py >= target->height) continue;

                const uint8_t* alpha_row = &g->alpha[gy * g->width];
                uint32_t* dst_row = &target->pixels[py * target->stride];

                for (int gx = 0; gx < g->width; gx++) {
                    int px = gx0 + gx;
                    if (px < 0 || px >= target->width) continue;

                    uint32_t glyph_alpha = alpha_row[gx];
                    if (glyph_alpha == 0) continue;

                    uint32_t final_alpha = (glyph_alpha * sa) / 255;
                    uint32_t dst = dst_row[px];
                    uint32_t dr = dst & 0xFF;
                    uint32_t dg = (dst >> 8) & 0xFF;
                    uint32_t db = (dst >> 16) & 0xFF;

                    uint32_t r = (sr * final_alpha + dr * (255 - final_alpha)) / 255;
                    uint32_t g_val = (sg * final_alpha + dg * (255 - final_alpha)) / 255;
                    uint32_t b = (sb * final_alpha + db * (255 - final_alpha)) / 255;

                    dst_row[px] = RGBA(r, g_val, b, 255);
                }
            }
        }
        cur_x += advance;
    }
}

void text_draw(const RenderTarget* target, const char* str, int x, int y, FontSize size, uint32_t color) {
    draw_text_internal(target, str, x, y, size, color, false);
}

void text_draw_tabular(const RenderTarget* target, const char* str, int x, int y, FontSize size, uint32_t color) {
    draw_text_internal(target, str, x, y, size, color, true);
}

void text_draw_centered(const RenderTarget* target, const char* str, int cx, int y, FontSize size, uint32_t color) {
    int w = text_width(str, size);
    draw_text_internal(target, str, cx - w / 2, y, size, color, false);
}

void text_draw_right(const RenderTarget* target, const char* str, int rx, int y, FontSize size, uint32_t color) {
    int w = text_width(str, size);
    draw_text_internal(target, str, rx - w, y, size, color, false);
}
