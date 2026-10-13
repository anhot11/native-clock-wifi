#include "theme.h"
#include <stdio.h>
#include <string.h>

static ThemeId g_current_theme = THEME_OBSIDIAN;

static const Theme G_THEMES[THEME_COUNT] = {
    [THEME_OBSIDIAN] = {
        .id             = THEME_OBSIDIAN,
        .name           = "Obsidian",
        .bg             = RGB(10, 11, 14),
        .card_bg_top    = RGBA(22, 25, 33, 235),
        .card_bg_bot    = RGBA(15, 17, 23, 245),
        .card_border    = RGBA(55, 65, 81, 120),
        .card_shadow    = RGBA(0, 0, 0, 90),
        .surface_inset  = RGBA(25, 29, 39, 200),
        .text_primary   = RGB(248, 250, 252),
        .text_secondary = RGB(156, 163, 175),
        .text_muted     = RGBA(148, 163, 184, 130),
        .accent         = RGB(14, 165, 233),   // Sky cyan
        .accent_glow    = RGBA(56, 189, 248, 160),
        .success        = RGB(34, 197, 94),    // Green
        .warning        = RGB(245, 158, 11),   // Amber
        .danger         = RGB(239, 68, 68)     // Red
    },
    [THEME_AMOLED] = {
        .id             = THEME_AMOLED,
        .name           = "AMOLED Puro",
        .bg             = RGB(0, 0, 0),
        .card_bg_top    = RGBA(12, 12, 12, 255),
        .card_bg_bot    = RGBA(5, 5, 5, 255),
        .card_border    = RGBA(40, 40, 40, 180),
        .card_shadow    = RGBA(0, 0, 0, 0),
        .surface_inset  = RGBA(20, 20, 20, 255),
        .text_primary   = RGB(255, 255, 255),
        .text_secondary = RGB(161, 161, 170),
        .text_muted     = RGBA(113, 113, 122, 150),
        .accent         = RGB(16, 185, 129),   // Emerald green
        .accent_glow    = RGBA(52, 211, 153, 160),
        .success        = RGB(16, 185, 129),
        .warning        = RGB(234, 179, 8),
        .danger         = RGB(244, 63, 94)
    },
    [THEME_CYBER] = {
        .id             = THEME_CYBER,
        .name           = "Cyber Neón",
        .bg             = RGB(8, 7, 18),
        .card_bg_top    = RGBA(24, 18, 42, 240),
        .card_bg_bot    = RGBA(14, 11, 28, 245),
        .card_border    = RGBA(112, 48, 160, 140),
        .card_shadow    = RGBA(18, 8, 32, 120),
        .surface_inset  = RGBA(32, 22, 54, 200),
        .text_primary   = RGB(250, 245, 255),
        .text_secondary = RGB(216, 180, 254),
        .text_muted     = RGBA(168, 85, 247, 140),
        .accent         = RGB(217, 70, 239),   // Neon magenta
        .accent_glow    = RGBA(244, 114, 182, 160),
        .success        = RGB(6, 182, 212),    // Neon cyan
        .warning        = RGB(251, 191, 36),
        .danger         = RGB(244, 63, 94)
    },
    [THEME_LIGHT] = {
        .id             = THEME_LIGHT,
        .name           = "Claro Titán",
        .bg             = RGB(241, 243, 247),
        .card_bg_top    = RGBA(255, 255, 255, 250),
        .card_bg_bot    = RGBA(247, 249, 252, 250),
        .card_border    = RGBA(203, 213, 225, 180),
        .card_shadow    = RGBA(100, 116, 139, 45),
        .surface_inset  = RGBA(226, 232, 240, 180),
        .text_primary   = RGB(15, 23, 42),
        .text_secondary = RGB(71, 85, 105),
        .text_muted     = RGBA(100, 116, 139, 160),
        .accent         = RGB(37, 99, 235),    // Royal blue
        .accent_glow    = RGBA(96, 165, 250, 140),
        .success        = RGB(22, 163, 74),
        .warning        = RGB(217, 119, 6),
        .danger         = RGB(220, 38, 38)
    }
};

const Theme* theme_get(ThemeId id) {
    if (id < 0 || id >= THEME_COUNT) id = THEME_OBSIDIAN;
    return &G_THEMES[id];
}

ThemeId theme_get_current_id(void) {
    return g_current_theme;
}

const Theme* theme_get_current(void) {
    return &G_THEMES[g_current_theme];
}

void theme_set(ThemeId id) {
    if (id >= 0 && id < THEME_COUNT) {
        g_current_theme = id;
    }
}

void theme_cycle(void) {
    g_current_theme = (ThemeId)((g_current_theme + 1) % THEME_COUNT);
}

void theme_load_saved(const char* base_path) {
    char filepath[256];
    if (base_path && base_path[0]) {
        snprintf(filepath, sizeof(filepath), "%s/theme.dat", base_path);
    } else {
        snprintf(filepath, sizeof(filepath), "/data/data/com.example.nativec/files/theme.dat");
    }

    FILE* f = fopen(filepath, "rb");
    if (f) {
        int id = 0;
        if (fread(&id, sizeof(int), 1, f) == 1) {
            if (id >= 0 && id < THEME_COUNT) {
                g_current_theme = (ThemeId)id;
            }
        }
        fclose(f);
    }
}

void theme_save(const char* base_path) {
    char filepath[256];
    if (base_path && base_path[0]) {
        snprintf(filepath, sizeof(filepath), "%s/theme.dat", base_path);
    } else {
        snprintf(filepath, sizeof(filepath), "/data/data/com.example.nativec/files/theme.dat");
    }

    FILE* f = fopen(filepath, "wb");
    if (f) {
        int id = (int)g_current_theme;
        fwrite(&id, sizeof(int), 1, f);
        fclose(f);
    }
}
