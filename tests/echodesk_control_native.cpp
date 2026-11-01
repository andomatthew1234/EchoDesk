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
    assert(echodesk_parse_app("nowPlaying", value));
    assert(value == ECHODESK_APP_NOW_PLAYING);
    assert(strcmp(echodesk_app_name(value), "nowPlaying") == 0);
    assert(echodesk_parse_app("myDevice", value));
    assert(value == ECHODESK_APP_MY_DEVICE);
    assert(strcmp(echodesk_app_name(value), "myDevice") == 0);
    assert(echodesk_parse_app("bedside", value));
    assert(value == ECHODESK_APP_BEDSIDE);
    assert(strcmp(echodesk_app_name(value), "bedside") == 0);
    assert(echodesk_parse_clock_format("24h", value));
    assert(value == CLOCK_FORMAT_24_HOUR);
    assert(echodesk_parse_timezone("Australia/Sydney", value));
    assert(value == ECHODESK_TIMEZONE_SYDNEY);
    assert(!echodesk_parse_timezone("Australia/Brisbane", value));
    assert(echodesk_parse_codex_percentage("remaining", value));
    assert(value == CODEX_PERCENTAGE_REMAINING);
    assert(echodesk_parse_codex_display("weekly", value));
    assert(value == CODEX_DISPLAY_WEEKLY);
    assert(echodesk_parse_performance_view("memory", value));
    assert(value == PERFORMANCE_VIEW_MEMORY);

    EchoDeskControlSettings current{
        THEME_LIGHT, 80, NOW_PLAYING_LAYOUT_ALBUM_COVER,
        NOW_PLAYING_BACKGROUND_ALBUM_COVER, false,
        ECHODESK_APP_HOME, CLOCK_FORMAT_12_HOUR,
        ECHODESK_TIMEZONE_MELBOURNE, CODEX_PERCENTAGE_USED,
        CODEX_DISPLAY_BOTH, PERFORMANCE_VIEW_OVERVIEW};
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

    patch = {};
    patch.has_default_app = true;
    patch.values.default_app = ECHODESK_APP_BEDSIDE;
    patch.has_clock_format = true;
    patch.values.clock_format = CLOCK_FORMAT_24_HOUR;
    patch.has_timezone = true;
    patch.values.timezone = ECHODESK_TIMEZONE_UTC;
    patch.has_codex_percentage_mode = true;
    patch.values.codex_percentage_mode = CODEX_PERCENTAGE_REMAINING;
    patch.has_codex_preferred_display = true;
    patch.values.codex_preferred_display = CODEX_DISPLAY_SESSION;
    patch.has_performance_default_view = true;
    patch.values.performance_default_view = PERFORMANCE_VIEW_GPU;
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, true, next) ==
           EchoDeskControlApplyResult::OK);
    assert(next.default_app == ECHODESK_APP_BEDSIDE);
    assert(next.clock_format == CLOCK_FORMAT_24_HOUR);
    assert(next.timezone == ECHODESK_TIMEZONE_UTC);
    assert(next.codex_percentage_mode == CODEX_PERCENTAGE_REMAINING);
    assert(next.codex_preferred_display == CODEX_DISPLAY_SESSION);
    assert(next.performance_default_view == PERFORMANCE_VIEW_GPU);
    assert(echodesk_apply_settings_patch(current, patch, 7, 7, false, next) ==
           EchoDeskControlApplyResult::PREVIEW_NOT_SUPPORTED);
    puts("echodesk_control_native: PASS");
    return 0;
}
