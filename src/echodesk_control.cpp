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

const char *echodesk_app_name(uint8_t app) {
    switch (app) {
        case ECHODESK_APP_CODEX: return "codex";
        case ECHODESK_APP_CLOCK: return "clock";
        case ECHODESK_APP_PERFORMANCE: return "performance";
        case ECHODESK_APP_NOW_PLAYING: return "nowPlaying";
        case ECHODESK_APP_SETTINGS: return "settings";
        case ECHODESK_APP_MY_DEVICE: return "myDevice";
        case ECHODESK_APP_BEDSIDE: return "bedside";
        default: return "home";
    }
}

const char *echodesk_clock_format_name(uint8_t format) {
    return format == CLOCK_FORMAT_24_HOUR ? "24h" : "12h";
}

const char *echodesk_timezone_name(uint8_t timezone) {
    switch (timezone) {
        case ECHODESK_TIMEZONE_SYDNEY: return "Australia/Sydney";
        case ECHODESK_TIMEZONE_UTC: return "UTC";
        default: return "Australia/Melbourne";
    }
}

const char *echodesk_codex_percentage_name(uint8_t mode) {
    return mode == CODEX_PERCENTAGE_REMAINING ? "remaining" : "used";
}

const char *echodesk_codex_display_name(uint8_t display) {
    switch (display) {
        case CODEX_DISPLAY_SESSION: return "session";
        case CODEX_DISPLAY_WEEKLY: return "weekly";
        default: return "both";
    }
}

const char *echodesk_performance_view_name(uint8_t view) {
    switch (view) {
        case PERFORMANCE_VIEW_CPU: return "cpu";
        case PERFORMANCE_VIEW_GPU: return "gpu";
        case PERFORMANCE_VIEW_MEMORY: return "memory";
        default: return "overview";
    }
}

bool echodesk_parse_app(const char *name, uint8_t &app) {
    if (!name) return false;
    if (strcmp(name, "home") == 0) app = ECHODESK_APP_HOME;
    else if (strcmp(name, "codex") == 0) app = ECHODESK_APP_CODEX;
    else if (strcmp(name, "clock") == 0) app = ECHODESK_APP_CLOCK;
    else if (strcmp(name, "performance") == 0) app = ECHODESK_APP_PERFORMANCE;
    else if (strcmp(name, "nowPlaying") == 0) app = ECHODESK_APP_NOW_PLAYING;
    else if (strcmp(name, "settings") == 0) app = ECHODESK_APP_SETTINGS;
    else if (strcmp(name, "myDevice") == 0) app = ECHODESK_APP_MY_DEVICE;
    else if (strcmp(name, "bedside") == 0) app = ECHODESK_APP_BEDSIDE;
    else return false;
    return true;
}

bool echodesk_parse_clock_format(const char *name, uint8_t &format) {
    if (!name) return false;
    if (strcmp(name, "12h") == 0) format = CLOCK_FORMAT_12_HOUR;
    else if (strcmp(name, "24h") == 0) format = CLOCK_FORMAT_24_HOUR;
    else return false;
    return true;
}

bool echodesk_parse_timezone(const char *name, uint8_t &timezone) {
    if (!name) return false;
    if (strcmp(name, "Australia/Melbourne") == 0) timezone = ECHODESK_TIMEZONE_MELBOURNE;
    else if (strcmp(name, "Australia/Sydney") == 0) timezone = ECHODESK_TIMEZONE_SYDNEY;
    else if (strcmp(name, "UTC") == 0) timezone = ECHODESK_TIMEZONE_UTC;
    else return false;
    return true;
}

bool echodesk_parse_codex_percentage(const char *name, uint8_t &mode) {
    if (!name) return false;
    if (strcmp(name, "used") == 0) mode = CODEX_PERCENTAGE_USED;
    else if (strcmp(name, "remaining") == 0) mode = CODEX_PERCENTAGE_REMAINING;
    else return false;
    return true;
}

bool echodesk_parse_codex_display(const char *name, uint8_t &display) {
    if (!name) return false;
    if (strcmp(name, "both") == 0) display = CODEX_DISPLAY_BOTH;
    else if (strcmp(name, "session") == 0) display = CODEX_DISPLAY_SESSION;
    else if (strcmp(name, "weekly") == 0) display = CODEX_DISPLAY_WEEKLY;
    else return false;
    return true;
}

bool echodesk_parse_performance_view(const char *name, uint8_t &view) {
    if (!name) return false;
    if (strcmp(name, "overview") == 0) view = PERFORMANCE_VIEW_OVERVIEW;
    else if (strcmp(name, "cpu") == 0) view = PERFORMANCE_VIEW_CPU;
    else if (strcmp(name, "gpu") == 0) view = PERFORMANCE_VIEW_GPU;
    else if (strcmp(name, "memory") == 0) view = PERFORMANCE_VIEW_MEMORY;
    else return false;
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
                     patch.has_now_playing_background_customized ||
                     patch.has_default_app || patch.has_clock_format ||
                     patch.has_timezone || patch.has_codex_percentage_mode ||
                     patch.has_codex_preferred_display ||
                     patch.has_performance_default_view;
    if (!any) return EchoDeskControlApplyResult::INVALID;
    if (!persist && (patch.has_theme || patch.has_now_playing_layout ||
                     patch.has_now_playing_background ||
                     patch.has_now_playing_background_customized ||
                     patch.has_default_app || patch.has_clock_format ||
                     patch.has_timezone || patch.has_codex_percentage_mode ||
                     patch.has_codex_preferred_display ||
                     patch.has_performance_default_view)) {
        return EchoDeskControlApplyResult::PREVIEW_NOT_SUPPORTED;
    }
    if ((patch.has_theme && patch.values.theme > THEME_LIGHT) ||
        (patch.has_brightness &&
         (patch.values.brightness < 10 || patch.values.brightness > 100)) ||
        (patch.has_now_playing_layout &&
         patch.values.now_playing_layout >= NOW_PLAYING_LAYOUT_COUNT) ||
        (patch.has_now_playing_background &&
         patch.values.now_playing_background >= NOW_PLAYING_BACKGROUND_COUNT) ||
        (patch.has_default_app && patch.values.default_app > ECHODESK_APP_BEDSIDE) ||
        (patch.has_clock_format && patch.values.clock_format > CLOCK_FORMAT_24_HOUR) ||
        (patch.has_timezone && patch.values.timezone > ECHODESK_TIMEZONE_UTC) ||
        (patch.has_codex_percentage_mode &&
         patch.values.codex_percentage_mode > CODEX_PERCENTAGE_REMAINING) ||
        (patch.has_codex_preferred_display &&
         patch.values.codex_preferred_display > CODEX_DISPLAY_WEEKLY) ||
        (patch.has_performance_default_view &&
         patch.values.performance_default_view > PERFORMANCE_VIEW_MEMORY)) {
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
    if (patch.has_default_app) next.default_app = patch.values.default_app;
    if (patch.has_clock_format) next.clock_format = patch.values.clock_format;
    if (patch.has_timezone) next.timezone = patch.values.timezone;
    if (patch.has_codex_percentage_mode) {
        next.codex_percentage_mode = patch.values.codex_percentage_mode;
    }
    if (patch.has_codex_preferred_display) {
        next.codex_preferred_display = patch.values.codex_preferred_display;
    }
    if (patch.has_performance_default_view) {
        next.performance_default_view = patch.values.performance_default_view;
    }
    next.now_playing_background = now_playing_resolve_background(
        next.now_playing_layout, next.now_playing_background,
        next.now_playing_background_customized);
    return EchoDeskControlApplyResult::OK;
}
