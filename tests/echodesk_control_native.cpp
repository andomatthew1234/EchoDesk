#include "../src/echodesk_control.h"
#include "../src/config.h"
#include "../src/now_playing_layout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    uint8_t value = 99;
    assert(echodesk_parse_layout("carPlay", value));
    assert(value == NOW_PLAYING_LAYOUT_CARPLAY);
    assert(strcmp(echodesk_layout_name(value), "carPlay") == 0);
    assert(!echodesk_parse_layout("CarPlay", value));
    assert(echodesk_parse_background("audioBars", value));
    assert(value == NOW_PLAYING_BACKGROUND_AUDIO_BARS);
    assert(strcmp(echodesk_background_name(value), "audioBars") == 0);

    EchoDeskControlSettings current{
        THEME_LIGHT, 80, NOW_PLAYING_LAYOUT_ALBUM_COVER,
        NOW_PLAYING_BACKGROUND_ALBUM_COVER, false};
    EchoDeskControlPatch patch{};
    patch.has_brightness = true;
    patch.values.brightness = 35;
    EchoDeskControlSettings next{};
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, false, next) ==
           EchoDeskControlApplyResult::OK);
    assert(next.brightness == 35);
    assert(echodesk_apply_settings_patch(current, patch, 6, 7, true, next) ==
           EchoDeskControlApplyResult::STALE_REVISION);

    patch = {};
    patch.has_now_playing_layout = true;
    patch.values.now_playing_layout = NOW_PLAYING_LAYOUT_ECHODESK;
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, false, next) ==
           EchoDeskControlApplyResult::PREVIEW_NOT_SUPPORTED);
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, true, next) ==
           EchoDeskControlApplyResult::OK);
    assert(next.now_playing_background == NOW_PLAYING_BACKGROUND_ECHODESK);
    assert(!next.now_playing_background_customized);

    patch = {};
    patch.has_now_playing_background = true;
    patch.values.now_playing_background = NOW_PLAYING_BACKGROUND_AUDIO_BARS;
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, true, next) ==
           EchoDeskControlApplyResult::OK);
    assert(next.now_playing_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS);
    assert(next.now_playing_background_customized);

    patch = {};
    patch.has_brightness = true;
    patch.values.brightness = 9;
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, true, next) ==
           EchoDeskControlApplyResult::INVALID);
    puts("echodesk_control_native: PASS");
    return 0;
}
