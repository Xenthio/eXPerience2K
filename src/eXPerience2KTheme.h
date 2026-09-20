#ifndef EXPERIENCE2K_THEME_H
#define EXPERIENCE2K_THEME_H

/* Stable IDs are persisted in configuration and in each resource state row.
   Add presets here without changing the XP operating-system support profiles. */
typedef struct {
    const char *id;
    const char *label;
    const char *font;
    const WCHAR *wide_font;
    int legacy_palette;
    int low_color_icons;
} THEME_PRESET;

static const THEME_PRESET g_theme_presets[] = {
    {"windows-2000", "Windows 2000", "Tahoma", L"Tahoma", 0, 0},
    {"windows-98-nt5", "Windows 98 / NT 5.0 Beta (experimental)",
     "MS Sans Serif", L"MS Sans Serif", 1, 0}
};
/* Resource-only variant: the UI exposes this as a checkbox, not a third theme. */
static const THEME_PRESET g_theme_98_low_color = {
    "windows-98-nt5-low-color", "Windows 98 / NT 5.0 Beta (16-colour icons)",
    "MS Sans Serif", L"MS Sans Serif", 1, 1
};

static inline const char *theme_asset_id(const THEME_PRESET *theme)
{
    return theme->low_color_icons ? "windows-98-nt5" : theme->id;
}

static inline const THEME_PRESET *theme_resource_variant(const THEME_PRESET *theme, int low_color)
{
    return theme->legacy_palette && low_color ? &g_theme_98_low_color : theme;
}

#define THEME_PRESET_COUNT (sizeof(g_theme_presets) / sizeof(g_theme_presets[0]))

static inline const THEME_PRESET *theme_by_id(const char *id)
{
    size_t i;
    for (i = 0; i < THEME_PRESET_COUNT; ++i)
        if (strcmp(id, g_theme_presets[i].id) == 0) return &g_theme_presets[i];
    if (!strcmp(id, g_theme_98_low_color.id)) return &g_theme_98_low_color;
    return NULL;
}

/* Windows 98 SE Windows Standard scheme, extracted from the reference VM.
   Explicit names matter: ButtonLight is distinct from ButtonFace. See
   reference-assets/windows-98/colors.tsv and its provenance notes. */
static const struct { const char *name; const char *rgb; } g_windows_98_colors[] = {
    {"Scrollbar", "192 192 192"},
    {"Background", "0 128 128"},
    {"ActiveTitle", "0 0 128"},
    {"InactiveTitle", "128 128 128"},
    {"Menu", "192 192 192"},
    {"Window", "255 255 255"},
    {"WindowFrame", "0 0 0"},
    {"MenuText", "0 0 0"},
    {"WindowText", "0 0 0"},
    {"TitleText", "255 255 255"},
    {"ActiveBorder", "192 192 192"},
    {"InactiveBorder", "192 192 192"},
    {"AppWorkSpace", "128 128 128"},
    {"Hilight", "0 0 128"},
    {"HilightText", "255 255 255"},
    {"ButtonFace", "192 192 192"},
    {"ButtonShadow", "128 128 128"},
    {"GrayText", "128 128 128"},
    {"ButtonText", "0 0 0"},
    {"InactiveTitleText", "192 192 192"},
    {"ButtonHilight", "255 255 255"},
    {"ButtonDkShadow", "0 0 0"},
    {"ButtonLight", "223 223 223"},
    {"InfoText", "0 0 0"},
    {"InfoWindow", "255 255 225"},
    {"ButtonAlternateFace", "181 181 181"},
    {"HotTrackingColor", "0 0 255"},
    {"GradientActiveTitle", "0 0 128"},
    {"GradientInactiveTitle", "128 128 128"},
};

static inline const char *theme_color(const THEME_PRESET *theme,
                                      const char *name, const char *fallback)
{
    size_t i;
    if (!theme->legacy_palette) return fallback;
    for (i = 0; i < sizeof(g_windows_98_colors) / sizeof(g_windows_98_colors[0]); ++i)
        if (!strcmp(name, g_windows_98_colors[i].name)) return g_windows_98_colors[i].rgb;
    return fallback;
}
#endif
