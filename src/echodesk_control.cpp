#include "echodesk_control.h"

#include "config.h"
#include "now_playing_layout.h"

#include <string.h>

const char *echodesk_layout_name(uint8_t layout) {
    switch (now_playing_layout_normalize(layout)) {
        case NOW_PLAYING_LAYOUT_CARPLAY: return "carPlay";
        case NOW_PLAYING_LAYOUT_ECHODESK: return "echoDesk";
        default: return "albumCover";
    }
}

const char *echodesk_background_name(uint8_t background) {
    switch (now_playing_background_normalize(background)) {
        case NOW_PLAYING_BACKGROUND_ECHODESK: return "echoDesk";
        case NOW_PLAYING_BACKGROUND_AUDIO_BARS: return "audioBars";
        default: return "blurredAlbumCover";
    }
}

bool echodesk_parse_layout(const char *name, uint8_t &layout) {
    if (!name) return false;
    if (strcmp(name, "albumCover") == 0) layout = NOW_PLAYING_LAYOUT_ALBUM_COVER;
    else if (strcmp(name, "carPlay") == 0) layout = NOW_PLAYING_LAYOUT_CARPLAY;
    else if (strcmp(name, "echoDesk") == 0) layout = NOW_PLAYING_LAYOUT_ECHODESK;
    else return false;
    return true;
}

bool echodesk_parse_background(const char *name, uint8_t &background) {
    if (!name) return false;
    if (strcmp(name, "blurredAlbumCover") == 0) {
        background = NOW_PLAYING_BACKGROUND_ALBUM_COVER;
    } else if (strcmp(name, "echoDesk") == 0) {
        background = NOW_PLAYING_BACKGROUND_ECHODESK;
    } else if (strcmp(name, "audioBars") == 0) {
        background = NOW_PLAYING_BACKGROUND_AUDIO_BARS;
    } else {
        return false;
    }
    return true;
}

EchoDeskControlApplyResult echodesk_apply_settings_patch(
    const EchoDeskControlSettings &current,
    const EchoDeskControlPatch &patch,
    uint32_t base_revision,
    uint32_t current_revision,
    bool persist,
    EchoDeskControlSettings &next) {
    if (base_revision != current_revision) {
        return EchoDeskControlApplyResult::STALE_REVISION;
    }
    const bool any = patch.has_theme || patch.has_brightness ||
                     patch.has_now_playing_layout ||
                     patch.has_now_playing_background ||
                     patch.has_now_playing_background_customized;
    if (!any) return EchoDeskControlApplyResult::INVALID;
    if (!persist && (patch.has_theme || patch.has_now_playing_layout ||
                     patch.has_now_playing_background ||
                     patch.has_now_playing_background_customized)) {
        return EchoDeskControlApplyResult::PREVIEW_NOT_SUPPORTED;
    }
    if ((patch.has_theme && patch.values.theme > THEME_LIGHT) ||
        (patch.has_brightness &&
         (patch.values.brightness < 10 || patch.values.brightness > 100)) ||
        (patch.has_now_playing_layout &&
         patch.values.now_playing_layout >= NOW_PLAYING_LAYOUT_COUNT) ||
        (patch.has_now_playing_background &&
         patch.values.now_playing_background >= NOW_PLAYING_BACKGROUND_COUNT)) {
        return EchoDeskControlApplyResult::INVALID;
    }

    next = current;
    if (patch.has_theme) next.theme = patch.values.theme;
    if (patch.has_brightness) next.brightness = patch.values.brightness;
    if (patch.has_now_playing_layout) {
        next.now_playing_layout = patch.values.now_playing_layout;
    }
    if (patch.has_now_playing_background) {
        next.now_playing_background = patch.values.now_playing_background;
        if (!patch.has_now_playing_background_customized) {
            next.now_playing_background_customized = true;
        }
    }
    if (patch.has_now_playing_background_customized) {
        next.now_playing_background_customized =
            patch.values.now_playing_background_customized;
    }
    next.now_playing_background = now_playing_resolve_background(
        next.now_playing_layout, next.now_playing_background,
        next.now_playing_background_customized);
    return EchoDeskControlApplyResult::OK;
}
