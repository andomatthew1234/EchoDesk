// Render genuine EchoDesk OS screens with controlled demonstration data.
// The UI itself comes from the production firmware sources linked by the build
// script; this file replaces hardware and serial services. The Clock capture
// intentionally shows the host time at render time.
#include "api_common.h"
#include "config.h"
#include "config_store.h"
#include "music_data.h"
#include "serial_receiver.h"
#include "ui_common.h"
#include "ui_echodesk.h"
#include "ui_performance.h"

#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

uint16_t SCREEN_WIDTH = 320;
uint16_t SCREEN_HEIGHT = 240;
AppConfig g_config = {};

namespace {

std::vector<uint16_t> framebuffer;
MusicState demo_music = {};

void flush_display(lv_display_t *display, const lv_area_t *area,
                   uint8_t *pixels) {
    const auto *source = reinterpret_cast<const uint16_t *>(pixels);
    const int stride = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            framebuffer[y * SCREEN_WIDTH + x] =
                source[(y - area->y1) * stride + x - area->x1];
        }
    }
    lv_display_flush_ready(display);
}

void configure_demo(const char *screen) {
    g_config.theme = THEME_DARK;
    g_config.brightness_pct = 80;
    g_config.clock_format = CLOCK_FORMAT_12_HOUR;
    g_config.timezone = ECHODESK_TIMEZONE_MELBOURNE;
    g_config.codex_percentage_mode = CODEX_PERCENTAGE_USED;
    g_config.codex_preferred_display = CODEX_DISPLAY_BOTH;
    g_config.performance_default_view = PERFORMANCE_VIEW_OVERVIEW;
    g_config.now_playing_layout = NOW_PLAYING_LAYOUT_CARPLAY;
    g_config.now_playing_background = NOW_PLAYING_BACKGROUND_ECHODESK;
    g_config.now_playing_background_customized = true;
    g_config.default_app = std::strcmp(screen, "codex") == 0
        ? ECHODESK_APP_CODEX
        : std::strcmp(screen, "clock") == 0
            ? ECHODESK_APP_CLOCK
            : ECHODESK_APP_NOW_PLAYING;

    demo_music.timestamp_epoch = static_cast<uint32_t>(std::time(nullptr));
    demo_music.received_ms = millis() - 100;
    demo_music.position_ms = 132000;
    demo_music.duration_ms = 248000;
    demo_music.generation = 42;
    demo_music.playback = MusicPlaybackState::PLAYING;
    demo_music.can_play_pause = true;
    demo_music.can_previous = true;
    demo_music.can_next = true;
    strlcpy(demo_music.title, "Midnight Circuit", sizeof(demo_music.title));
    strlcpy(demo_music.artist, "Echoes in Motion", sizeof(demo_music.artist));
    strlcpy(demo_music.album, "Night Signals", sizeof(demo_music.album));
    demo_music.application[0] = '\0';
}

bool write_ppm(const char *path) {
    FILE *output = std::fopen(path, "wb");
    if (!output) return false;
    std::fprintf(output, "P6\n%u %u\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (uint16_t pixel : framebuffer) {
        const unsigned char rgb[] = {
            static_cast<unsigned char>(((pixel >> 11) & 31) * 255 / 31),
            static_cast<unsigned char>(((pixel >> 5) & 63) * 255 / 63),
            static_cast<unsigned char>((pixel & 31) * 255 / 31),
        };
        std::fwrite(rgb, 1, sizeof(rgb), output);
    }
    return std::fclose(output) == 0;
}

} // namespace

void config_save(const AppConfig &) {}
void backlight_apply_percent(uint8_t) {}
void apply_orientation(uint8_t) {}
void ui_apply_theme(uint8_t) {}
void ui_styles_init() {}
void ui_styles_reset() {}

void format_reset_compact(time_t reset_epoch, char *buffer, size_t length) {
    const time_t remaining = reset_epoch > time(nullptr)
        ? reset_epoch - time(nullptr)
        : 0;
    const long days = static_cast<long>(remaining / 86400);
    const long hours = static_cast<long>((remaining % 86400) / 3600);
    const long minutes = static_cast<long>((remaining % 3600) / 60);
    if (days > 0) std::snprintf(buffer, length, "%ldd %ldh", days, hours);
    else if (hours > 0) std::snprintf(buffer, length, "%ldh %ldm", hours, minutes);
    else std::snprintf(buffer, length, "%ldm", minutes);
}

bool serial_get_provider_state(uint8_t provider, MonitorState &state) {
    if (provider != PROVIDER_OPENAI) return false;
    state = {};
    state.provider = PROVIDER_OPENAI;
    strlcpy(state.provider_label, "CODEX", sizeof(state.provider_label));
    state.token_valid = true;
    state.usage.valid = true;
    state.usage.last_fetch = millis() - 100;
    state.usage.five_hour_utilization = .32f;
    state.usage.seven_day_utilization = .67f;
    state.usage.five_hour_reset_epoch = time(nullptr) + 2 * 3600 + 14 * 60;
    state.usage.seven_day_reset_epoch = time(nullptr) + 3 * 86400 + 8 * 3600;
    state.usage.five_hour_window_minutes = 300;
    state.usage.seven_day_window_minutes = 10080;
    return true;
}
MonitorState serial_get_state() { MonitorState state = {}; return state; }
bool serial_has_recent_data() { return true; }
bool serial_has_recent_host_frame() { return true; }
bool serial_has_new_data() { return false; }
void serial_select_view(uint8_t) {}
void serial_next_view() {}
void serial_previous_view() {}
bool serial_is_clock_view() { return false; }
uint8_t serial_view_count() { return 1; }
bool serial_is_plugin_view() { return false; }
uint8_t serial_active_view() { return 0; }
const char *serial_get_display_time() { return "14:32"; }
void serial_apply_configured_timezone() {
    _putenv_s("TZ", "UTC");
    _tzset();
}
void serial_request_ui_rebuild() {}
bool serial_ui_rebuild_requested() { return false; }
void serial_ui_rebuild_clear() {}
void serial_echodesk_state_changed() {}
bool host_timezone_is_set() { return true; }

void ui_performance_set_navigation_callbacks(UiPerformanceNavigationCallback,
                                             UiPerformanceNavigationCallback,
                                             void *) {}
lv_obj_t *ui_performance_create() { return nullptr; }
void ui_performance_tick() {}
lv_obj_t *ui_performance_recreate() { return nullptr; }
lv_obj_t *ui_performance_screen() { return nullptr; }
void ui_performance_apply_default_view() {}

void music_data_init() {}
void music_data_mark_disconnected() {}
bool music_data_ingest(const MusicState &, uint32_t) { return true; }
bool music_data_get(MusicState &state) { state = demo_music; return true; }
MusicConnectionState music_data_connection_state(uint32_t) {
    return MusicConnectionState::LIVE;
}
uint32_t music_data_display_position_ms(const MusicState &state, uint32_t now) {
    return state.playback == MusicPlaybackState::PLAYING
        ? state.position_ms + (now - state.received_ms)
        : state.position_ms;
}
bool music_data_begin_art(const char *, size_t, uint16_t, uint32_t) { return false; }
bool music_data_append_art(const char *, uint16_t, size_t, const char *) { return false; }
bool music_data_art_available() { return false; }
const uint8_t *music_data_art_data() { return nullptr; }
size_t music_data_art_size() { return 0; }
const char *music_data_art_id() { return ""; }
uint32_t music_data_art_revision() { return 0; }
bool music_data_begin_background(const char *, size_t, uint16_t, uint32_t) { return false; }
bool music_data_append_background(const char *, uint16_t, size_t, const char *) { return false; }
bool music_data_background_available() { return false; }
const uint8_t *music_data_background_data() { return nullptr; }
size_t music_data_background_size() { return 0; }
const char *music_data_background_id() { return ""; }
uint32_t music_data_background_revision() { return 0; }
void music_data_release_retired_background() {}
bool music_data_ingest_visualizer(const MusicVisualizerState &, uint32_t) { return false; }
bool music_data_get_visualizer(MusicVisualizerState &) { return false; }
MusicVisualizerStatus music_data_visualizer_status(uint32_t) {
    return MusicVisualizerStatus::VISUALIZER_DISABLED;
}
size_t music_base64_decode(const char *, uint8_t *, size_t) { return 0; }
uint32_t music_crc32(const uint8_t *, size_t) { return 0; }

int main(int argc, char **argv) {
    if (argc != 3) return 2; // codex|clock|now-playing output.ppm
    const bool supported = std::strcmp(argv[1], "codex") == 0 ||
                           std::strcmp(argv[1], "clock") == 0 ||
                           std::strcmp(argv[1], "now-playing") == 0;
    if (!supported) return 2;

    configure_demo(argv[1]);
    lv_init();
    lv_display_t *display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    framebuffer.assign(SCREEN_WIDTH * SCREEN_HEIGHT, 0);
    std::vector<uint16_t> draw_buffer(SCREEN_WIDTH * 24);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           draw_buffer.size() * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);

    ui_echodesk_init();
    for (int i = 0; i < 12; ++i) {
        lv_tick_inc(33);
        lv_timer_handler();
    }
    lv_refr_now(display);
    return write_ppm(argv[2]) ? 0 : 1;
}
