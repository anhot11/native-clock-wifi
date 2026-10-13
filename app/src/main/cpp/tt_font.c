#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "tt_font.h"
#include "ui_renderer.h"
#include "font8x16.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <android/log.h>

#define TAG "TTFont"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// RGBA 8888 Little Endian
#define RGBA(r, g, b, a) (((uint32_t)(r) & 0xFF) | (((uint32_t)(g) & 0xFF) << 8) | (((uint32_t)(b) & 0xFF) << 16) | (((uint32_t)(a) & 0xFF) << 24))

typedef struct {
    int width;
    int height;
    int xoff;
    int yoff;
    int xadvance;
    uint8_t* alpha; // width * height
} CachedGlyph;

typedef struct {
    int pixel_height;
    int ascent;
    int descent;
    int line_gap;
    CachedGlyph glyphs[96]; // ASCII 32 to 127
} FontAtlas;

static FontAtlas g_atlases[TT_NUM_SIZES];
static bool g_ttf_loaded = false;
static uint8_t* g_font_buffer = NULL;
static float g_current_scale = 1.0f;

static const float BASE_DP_SIZES[TT_NUM_SIZES] = {
    12.0f, // XS
    14.0f, // SM
    18.0f, // MD
    26.0f, // LG
    42.0f  // XL
};

static const char* FONT_PATHS[] = {
    "/system/fonts/Roboto-Medium.ttf",
    "/system/fonts/Roboto-Regular.ttf",
    "/system/fonts/Roboto-Bold.ttf",
    "/system/fonts/DroidSans.ttf",
    NULL
};

bool tt_font_init(float dp_scale) {
    if (dp_scale < 1.0f) dp_scale = 1.0f;
    g_current_scale = dp_scale;

    // Load TTF font file
    if (!g_font_buffer) {
        for (int i = 0; FONT_PATHS[i] != NULL; i++) {
            FILE* f = fopen(FONT_PATHS[i], "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (size > 0 && size < 15 * 1024 * 1024) {
                    g_font_buffer = (uint8_t*)malloc(size);
                    if (g_font_buffer) {
                        fread(g_font_buffer, 1, size, f);
                        LOGI("Loaded TrueType font: %s (%ld bytes)", FONT_PATHS[i], size);
                        g_ttf_loaded = true;
                        fclose(f);
                        break;
                    }
                }
                fclose(f);
            }
        }
    }

    if (!g_ttf_loaded || !g_font_buffer) {
        LOGE("Failed to load any system TrueType font, using fallback");
        return false;
    }

    stbtt_fontinfo font_info;
    if (!stbtt_InitFont(&font_info, g_font_buffer, 0)) {
        LOGE("stbtt_InitFont failed");
        g_ttf_loaded = false;
        return false;
    }

    // Rasterize and cache ASCII glyphs for each size
    for (int s = 0; s < TT_NUM_SIZES; s++) {
        FontAtlas* atlas = &g_atlases[s];
        atlas->pixel_height = (int)(BASE_DP_SIZES[s] * dp_scale);
        if (atlas->pixel_height < 10) atlas->pixel_height = 10;

        float scale = stbtt_ScaleForPixelHeight(&font_info, (float)atlas->pixel_height);

        int ascent_raw, descent_raw, line_gap_raw;
        stbtt_GetFontVMetrics(&font_info, &ascent_raw, &descent_raw, &line_gap_raw);
        atlas->ascent = (int)ceilf(ascent_raw * scale);
        atlas->descent = (int)ceilf(descent_raw * scale);
        atlas->line_gap = (int)ceilf(line_gap_raw * scale);

        for (int ch = 32; ch < 127; ch++) {
            int idx = ch - 32;
            CachedGlyph* g = &atlas->glyphs[idx];

            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font_info, ch, &advance, &lsb);
            g->xadvance = (int)ceilf(advance * scale);

            if (ch == ' ') {
                g->width = 0;
                g->height = 0;
                g->xoff = 0;
                g->yoff = 0;
                g->alpha = NULL;
                continue;
            }

            g->alpha = stbtt_GetCodepointBitmap(
                &font_info,
                0,
                scale,
                ch,
                &g->width,
                &g->height,
                &g->xoff,
                &g->yoff
            );
        }
    }

    LOGI("TrueType fonts initialized smoothly for all mobile sizes (dp_scale=%.2f)", dp_scale);
    return true;
}

void tt_font_cleanup(void) {
    if (g_ttf_loaded) {
        for (int s = 0; s < TT_NUM_SIZES; s++) {
            FontAtlas* atlas = &g_atlases[s];
            for (int ch = 32; ch < 127; ch++) {
                int idx = ch - 32;
                CachedGlyph* g = &atlas->glyphs[idx];
                if (g->alpha) {
                    stbtt_FreeBitmap(g->alpha, NULL);
                    g->alpha = NULL;
                }
            }
        }
    }
    if (g_font_buffer) {
        free(g_font_buffer);
        g_font_buffer = NULL;
    }
    g_ttf_loaded = false;
}

int tt_font_get_width(const char* text, TTFontSize size) {
    if (!text || size < 0 || size >= TT_NUM_SIZES) return 0;
    if (!g_ttf_loaded) {
        int scale = (size == TT_SIZE_XL) ? 4 : (size == TT_SIZE_LG) ? 3 : (size == TT_SIZE_MD) ? 2 : 1;
        return (int)strlen(text) * 8 * scale;
    }

    const FontAtlas* atlas = &g_atlases[size];
    int total_w = 0;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c >= 127) c = '?';
        total_w += atlas->glyphs[c - 32].xadvance;
    }
    return total_w;
}

int tt_font_get_height(TTFontSize size) {
    if (size < 0 || size >= TT_NUM_SIZES) return 16;
    if (!g_ttf_loaded) {
        int scale = (size == TT_SIZE_XL) ? 4 : (size == TT_SIZE_LG) ? 3 : (size == TT_SIZE_MD) ? 2 : 1;
        return 16 * scale;
    }
    return g_atlases[size].pixel_height;
}

void tt_font_draw(UIRenderer* ui, const char* text, int x, int y, TTFontSize size, uint32_t color) {
    if (!ui || !ui->pixels || !text || size < 0 || size >= TT_NUM_SIZES) return;

    if (!g_ttf_loaded) {
        // Fallback: draw with bitmap font
        int scale = (size == TT_SIZE_XL) ? 4 : (size == TT_SIZE_LG) ? 3 : (size == TT_SIZE_MD) ? 2 : 1;
        int cur_x = x;
        int step = 7 * scale;
        while (*text) {
            unsigned char c = (unsigned char)*text++;
            if (c == '\n') {
                y += 18 * scale;
                cur_x = x;
            } else if (c == ' ') {
                cur_x += 4 * scale;
            } else {
                if (c < 32 || c > 127) c = '?';
                int index = c - 32;
                const uint8_t* rows = font8x16_data[index];
                for (int r = 0; r < 16; r++) {
                    uint8_t bits = rows[r];
                    if (!bits) continue;
                    for (int col = 0; col < 8; col++) {
                        if (bits & (1 << (7 - col))) {
                            for (int sy = 0; sy < scale; sy++) {
                                for (int sx = 0; sx < scale; sx++) {
                                    int px = cur_x + col * scale + sx;
                                    int py = y + r * scale + sy;
                                    if (px >= 0 && px < ui->width && py >= 0 && py < ui->height) {
                                        ui->pixels[py * ui->stride + px] = color;
                                    }
                                }
                            }
                        }
                    }
                }
                cur_x += step;
            }
        }
        return;
    }

    const FontAtlas* atlas = &g_atlases[size];
    int baseline_y = y + atlas->ascent;
    int cur_x = x;

    uint32_t sr = color & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = (color >> 16) & 0xFF;
    uint32_t sa = (color >> 24) & 0xFF;
    if (sa == 0) return;

    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c == '\n') {
            baseline_y += atlas->pixel_height + atlas->line_gap;
            cur_x = x;
            continue;
        }
        if (c < 32 || c >= 127) c = '?';

        const CachedGlyph* g = &atlas->glyphs[c - 32];
        if (g->alpha && g->width > 0 && g->height > 0) {
            int gx0 = cur_x + g->xoff;
            int gy0 = baseline_y + g->yoff;

            for (int gy = 0; gy < g->height; gy++) {
                int py = gy0 + gy;
                if (py < 0 || py >= ui->height) continue;

                const uint8_t* alpha_row = &g->alpha[gy * g->width];
                uint32_t* dst_row = &ui->pixels[py * ui->stride];

                for (int gx = 0; gx < g->width; gx++) {
                    int px = gx0 + gx;
                    if (px < 0 || px >= ui->width) continue;

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
        cur_x += g->xadvance;
    }
}

void tt_font_draw_centered(UIRenderer* ui, const char* text, int cx, int y, TTFontSize size, uint32_t color) {
    int w = tt_font_get_width(text, size);
    tt_font_draw(ui, text, cx - w / 2, y, size, color);
}
