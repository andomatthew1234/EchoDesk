#ifndef ECHODESK_CONTROL_H
#define ECHODESK_CONTROL_H

#include <stdint.h>

constexpr uint8_t ECHODESK_CONTROL_VERSION = 1;

struct EchoDeskControlSettings {
    uint8_t theme;
    uint8_t brightness;
    uint8_t now_playing_layout;
    uint8_t now_playing_background;
    bool now_playing_background_customized;
    uint8_t default_app;
    uint8_t clock_format;
    uint8_t timezone;
    uint8_t codex_percentage_mode;
    uint8_t codex_preferred_display;
    uint8_t performance_default_view;
};

struct EchoDeskControlPatch {
    bool has_theme;
    bool has_brightness;
    bool has_now_playing_layout;
    bool has_now_playing_background;
    bool has_now_playing_background_customized;
    bool has_default_app;
    bool has_clock_format;
    bool has_timezone;
    bool has_codex_percentage_mode;
    bool has_codex_preferred_display;
    bool has_performance_default_view;
    EchoDeskControlSettings values;
};

enum class EchoDeskControlApplyResult : uint8_t {
    OK = 0,
    INVALID,
    STALE_REVISION,
    PREVIEW_NOT_SUPPORTED,
};

const char *echodesk_layout_name(uint8_t layout);
const char *echodesk_background_name(uint8_t background);
bool echodesk_parse_layout(const char *name, uint8_t &layout);
bool echodesk_parse_background(const char *name, uint8_t &background);
const char *echodesk_app_name(uint8_t app);
const char *echodesk_clock_format_name(uint8_t format);
const char *echodesk_timezone_name(uint8_t timezone);
const char *echodesk_codex_percentage_name(uint8_t mode);
const char *echodesk_codex_display_name(uint8_t display);
const char *echodesk_performance_view_name(uint8_t view);
bool echodesk_parse_app(const char *name, uint8_t &app);
bool echodesk_parse_clock_format(const char *name, uint8_t &format);
bool echodesk_parse_timezone(const char *name, uint8_t &timezone);
bool echodesk_parse_codex_percentage(const char *name, uint8_t &mode);
bool echodesk_parse_codex_display(const char *name, uint8_t &display);
bool echodesk_parse_performance_view(const char *name, uint8_t &view);

EchoDeskControlApplyResult echodesk_apply_settings_patch(
    const EchoDeskControlSettings &current,
    const EchoDeskControlPatch &patch,
    uint32_t base_revision,
    uint32_t current_revision,
    bool persist,
    EchoDeskControlSettings &next);

#endif // ECHODESK_CONTROL_H
