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
};

struct EchoDeskControlPatch {
    bool has_theme;
    bool has_brightness;
    bool has_now_playing_layout;
    bool has_now_playing_background;
    bool has_now_playing_background_customized;
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

EchoDeskControlApplyResult echodesk_apply_settings_patch(
    const EchoDeskControlSettings &current,
    const EchoDeskControlPatch &patch,
    uint32_t base_revision,
    uint32_t current_revision,
    bool persist,
    EchoDeskControlSettings &next);

#endif // ECHODESK_CONTROL_H
