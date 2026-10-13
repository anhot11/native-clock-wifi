#ifndef THEME_H
#define THEME_H

#include <stdint.h>
#include <stdbool.h>

// RGBA 8888 Little Endian
#define RGBA(r, g, b, a) (((uint32_t)(r) & 0xFF) | (((uint32_t)(g) & 0xFF) << 8) | (((uint32_t)(b) & 0xFF) << 16) | (((uint32_t)(a) & 0xFF) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 255)

typedef enum {
    THEME_OBSIDIAN = 0,   // Glassmorphic dark slate with sky cyan accent
    THEME_AMOLED   = 1,   // Pure pitch black #000000 with vivid emerald accent
    THEME_CYBER    = 2,   // Deep midnight violet with neon magenta / cyan glow
    THEME_LIGHT    = 3,   // Titanium light with deep navy contrast
    THEME_COUNT    = 4
} ThemeId;

typedef struct {
    ThemeId id;
    const char* name;
    
    uint32_t bg;             // Canvas background
    uint32_t card_bg_top;    // Card gradient top
    uint32_t card_bg_bot;    // Card gradient bottom
    uint32_t card_border;    // 1px anti-aliased border
    uint32_t card_shadow;    // Soft shadow color
    uint32_t surface_inset;  // Inset pill / progress track background
    
    uint32_t text_primary;   // High-contrast headline / number
    uint32_t text_secondary; // Subheading / active value
    uint32_t text_muted;     // Tiny uppercase labels (low opacity)
    
    uint32_t accent;         // Brand accent color
    uint32_t accent_glow;    // Glowing highlight
    uint32_t success;        // Semantic normal / optimal (green)
    uint32_t warning;        // Semantic warning (amber)
    uint32_t danger;         // Semantic critical / alarm (red)
} Theme;

const Theme* theme_get(ThemeId id);
ThemeId theme_get_current_id(void);
const Theme* theme_get_current(void);
void theme_set(ThemeId id);
void theme_cycle(void);
void theme_load_saved(const char* base_path);
void theme_save(const char* base_path);

#endif // THEME_H
