#include "ui_now_playing.h"

#if defined(ECHODESK_OS)

#include "config_store.h"
#include "music_data.h"
#include "now_playing_layout.h"
#include "serial_receiver.h"

#include <Arduino.h>
#include <misc/cache/instance/lv_image_cache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr uint32_t BLACK = 0x000000;
constexpr uint32_t WHITE = 0xFFFFFF;
constexpr uint32_t PANEL = 0x0E0E0E;
constexpr uint32_t PRESSED = 0x242424;
constexpr uint32_t GREY = 0x8A8A8A;
constexpr uint32_t DIM = 0x3C3C3C;
constexpr uint32_t METADATA_REFRESH_MS = 250;
constexpr uint32_t ANIMATION_REFRESH_MS = 40;
constexpr uint32_t VISUALIZER_SUBSCRIBE_MS = 2000;
constexpr uint8_t CORNER_COUNT = 8;

enum class PreferencePage : uint8_t { OVERVIEW = 0, LAYOUT, BACKGROUND };

struct UiNowPlayingState {
    lv_obj_t *screen;
    lv_obj_t *status_label;
    lv_obj_t *preferences_panel;
    lv_obj_t *background_image;
    lv_obj_t *art_image;
    lv_obj_t *art_placeholder;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *album_label;
    lv_obj_t *progress_bar;
    lv_obj_t *position_label;
    lv_obj_t *duration_label;
    lv_obj_t *previous_button;
    lv_obj_t *play_button;
    lv_obj_t *play_label;
    lv_obj_t *next_button;
    lv_obj_t **corner_segments;
    lv_obj_t **audio_bars;
    lv_obj_t **audio_peaks;
    uint8_t *audio_levels;
    uint8_t *audio_peak_levels;
    lv_image_dsc_t *art_descriptor;
    lv_image_dsc_t *background_descriptor;
    UiNowPlayingNavigationCallback home_callback;
    void *home_context;
    uint8_t current_layout;
    uint8_t current_background;
    uint8_t pending_layout;
    uint8_t pending_background;
    bool pending_background_customized;
    bool preference_change_pending;
    bool corners_playing;
    bool visualizer_playing;
    uint32_t last_refresh;
    uint32_t last_animation_refresh;
    uint32_t last_preference_touch;
    uint32_t last_visualizer_subscribe;
    uint32_t last_background_request;
    int8_t last_corner_offset;
    uint8_t last_corner_opacity;
    uint32_t displayed_art_revision;
    uint32_t displayed_background_revision;
    int8_t preference_page_pending;
};

UiNowPlayingState *ui = nullptr;

#define screen ui->screen
#define status_label ui->status_label
#define preferences_panel ui->preferences_panel
#define background_image ui->background_image
#define art_image ui->art_image
#define art_placeholder ui->art_placeholder
#define title_label ui->title_label
#define artist_label ui->artist_label
#define album_label ui->album_label
#define progress_bar ui->progress_bar
#define position_label ui->position_label
#define duration_label ui->duration_label
#define previous_button ui->previous_button
#define play_button ui->play_button
#define play_label ui->play_label
#define next_button ui->next_button
#define corner_segments ui->corner_segments
#define audio_bars ui->audio_bars
#define audio_peaks ui->audio_peaks
#define audio_levels ui->audio_levels
#define audio_peak_levels ui->audio_peak_levels
#define art_descriptor ui->art_descriptor
#define background_descriptor ui->background_descriptor
#define home_callback ui->home_callback
#define home_context ui->home_context
#define current_layout ui->current_layout
#define current_background ui->current_background
#define pending_layout ui->pending_layout
#define pending_background ui->pending_background
#define pending_background_customized ui->pending_background_customized
#define preference_change_pending ui->preference_change_pending
#define corners_playing ui->corners_playing
#define visualizer_playing ui->visualizer_playing
#define last_refresh ui->last_refresh
#define last_animation_refresh ui->last_animation_refresh
#define last_preference_touch ui->last_preference_touch
#define last_visualizer_subscribe ui->last_visualizer_subscribe
#define last_background_request ui->last_background_request
#define last_corner_offset ui->last_corner_offset
#define last_corner_opacity ui->last_corner_opacity
#define displayed_art_revision ui->displayed_art_revision
#define displayed_background_revision ui->displayed_background_revision
#define preference_page_pending ui->preference_page_pending

bool uses_theme_background() {
    return current_background != NOW_PLAYING_BACKGROUND_ALBUM_COVER;
}

void ensure_ui_storage() {
    if (!ui) {
        ui = static_cast<UiNowPlayingState *>(calloc(1, sizeof(UiNowPlayingState)));
        if (!ui) return;
        current_layout = pending_layout = NOW_PLAYING_LAYOUT_DEFAULT;
        current_background = pending_background = NOW_PLAYING_BACKGROUND_DEFAULT;
        last_corner_offset = -1;
        displayed_art_revision = displayed_background_revision = UINT32_MAX;
        preference_page_pending = -1;
    }
    if (!corner_segments) corner_segments = static_cast<lv_obj_t **>(calloc(CORNER_COUNT, sizeof(lv_obj_t *)));
    if (!audio_bars) audio_bars = static_cast<lv_obj_t **>(calloc(MUSIC_VISUALIZER_BANDS, sizeof(lv_obj_t *)));
    if (!audio_peaks) audio_peaks = static_cast<lv_obj_t **>(calloc(MUSIC_VISUALIZER_BANDS, sizeof(lv_obj_t *)));
    if (!audio_levels) audio_levels = static_cast<uint8_t *>(calloc(MUSIC_VISUALIZER_BANDS, 1));
    if (!audio_peak_levels) audio_peak_levels = static_cast<uint8_t *>(calloc(MUSIC_VISUALIZER_BANDS, 1));
    if (!art_descriptor) art_descriptor = static_cast<lv_image_dsc_t *>(calloc(1, sizeof(lv_image_dsc_t)));
    if (!background_descriptor) background_descriptor = static_cast<lv_image_dsc_t *>(calloc(1, sizeof(lv_image_dsc_t)));
}

lv_color_t themed_color(uint32_t value) {
    if (g_config.theme == THEME_LIGHT) {
        if (value == BLACK) return lv_color_hex(WHITE);
        if (value == WHITE) return lv_color_hex(BLACK);
        if (value == PANEL) return lv_color_hex(0xF1F1F1);
        if (value == PRESSED) return lv_color_hex(0xD8D8D8);
        if (value == GREY) return lv_color_hex(0x555555);
        if (value == DIM) return lv_color_hex(0xB8B8B8);
    }
    return lv_color_hex(value);
}

lv_color_t foreground_color(uint32_t value) {
    if (!uses_theme_background()) return lv_color_hex(value == GREY ? 0xB8B8B8 : value);
    return themed_color(value);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     uint32_t value = WHITE) {
    lv_obj_t *result = lv_label_create(parent);
    lv_label_set_text(result, text);
    lv_obj_set_style_text_font(result, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(result, foreground_color(value), LV_PART_MAIN);
    return result;
}

void set_rect(lv_obj_t *object, const NowPlayingRect &rect) {
    lv_obj_set_size(object, rect.width, rect.height);
    lv_obj_set_pos(object, rect.x, rect.y);
}

void style_button(lv_obj_t *button, uint8_t radius = 9) {
    lv_obj_set_style_bg_color(button, uses_theme_background() ? themed_color(PANEL) : lv_color_hex(0x090909), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, uses_theme_background() ? LV_OPA_COVER : LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, foreground_color(DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, foreground_color(PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, foreground_color(WHITE), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
}

void clear_widget_references() {
    status_label = preferences_panel = background_image = art_image = art_placeholder = nullptr;
    title_label = artist_label = album_label = progress_bar = nullptr;
    position_label = duration_label = previous_button = play_button = play_label = next_button = nullptr;
    if (corner_segments) memset(corner_segments, 0, CORNER_COUNT * sizeof(lv_obj_t *));
    if (audio_bars) memset(audio_bars, 0, MUSIC_VISUALIZER_BANDS * sizeof(lv_obj_t *));
    if (audio_peaks) memset(audio_peaks, 0, MUSIC_VISUALIZER_BANDS * sizeof(lv_obj_t *));
}

void send_visualizer_subscription(bool enabled) {
    Serial.printf("{\"type\":\"music_visualizer\",\"version\":1,\"enabled\":%s}\n",
                  enabled ? "true" : "false");
    last_visualizer_subscribe = millis();
}

void send_background_subscription(bool enabled) {
    Serial.printf("{\"type\":\"music_background_request\",\"version\":1,\"enabled\":%s}\n",
                  enabled ? "true" : "false");
    last_background_request = millis();
}

void on_screen_deleted(lv_event_t *event) {
    if (screen == lv_event_get_target(event)) {
        if (background_descriptor) lv_image_cache_drop(background_descriptor);
        music_data_release_retired_background();
        screen = nullptr;
        clear_widget_references();
    }
}

void on_home(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (home_callback) home_callback(home_context);
}

void send_control(const char *action) {
    MusicState state{};
    if (!music_data_get(state) || state.generation == 0) return;
    Serial.printf("{\"type\":\"music_control\",\"version\":1,\"generation\":%llu,\"action\":\"%s\"}\n",
                  static_cast<unsigned long long>(state.generation), action);
}

void on_control(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const char *action = static_cast<const char *>(lv_event_get_user_data(event));
    if (action) send_control(action);
}

void set_hidden(lv_obj_t *object, bool hidden) {
    if (!object) return;
    hidden ? lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void set_enabled(lv_obj_t *button, bool enabled) {
    if (!button) return;
    enabled ? lv_obj_remove_state(button, LV_STATE_DISABLED) : lv_obj_add_state(button, LV_STATE_DISABLED);
}

void set_bounded_text(lv_obj_t *object, const char *value, const char *fallback) {
    if (object) lv_label_set_text(object, value && value[0] ? value : fallback);
}

void format_time(uint32_t value_ms, char *out, size_t capacity) {
    const uint32_t total = value_ms / 1000;
    snprintf(out, capacity, "%lu:%02lu", (unsigned long)(total / 60), (unsigned long)(total % 60));
}

void set_image_opacity(void *object, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), value, LV_PART_MAIN);
}

void fade_in_background() {
    if (!background_image) return;
    lv_obj_set_style_opa(background_image, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, background_image);
    lv_anim_set_exec_cb(&animation, set_image_opacity);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 180);
    lv_anim_start(&animation);
}

void create_album_background() {
    background_image = lv_image_create(screen);
    lv_obj_set_pos(background_image, 0, 0);
    lv_obj_add_flag(background_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(background_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 320, 240);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
}

void position_audio_bar(uint8_t i) {
    if (!audio_bars || !audio_peaks || !audio_levels || !audio_peak_levels || !audio_bars[i]) return;
    constexpr int16_t max_height = 160;
    const int16_t x = 3 + static_cast<int16_t>(i) * 20;
    const int16_t height = 2 + static_cast<int16_t>(audio_levels[i]) * max_height / 255;
    const int16_t peak_y = 236 - static_cast<int16_t>(audio_peak_levels[i]) * max_height / 255;
    lv_obj_set_size(audio_bars[i], 14, height);
    lv_obj_set_pos(audio_bars[i], x, 238 - height);
    lv_obj_set_size(audio_peaks[i], 14, 2);
    lv_obj_set_pos(audio_peaks[i], x, peak_y);
}

void create_audio_bars() {
    if (!audio_bars || !audio_peaks || !audio_levels || !audio_peak_levels) return;
    memset(audio_levels, 0, MUSIC_VISUALIZER_BANDS);
    memset(audio_peak_levels, 0, MUSIC_VISUALIZER_BANDS);
    for (uint8_t i = 0; i < MUSIC_VISUALIZER_BANDS; ++i) {
        audio_bars[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(audio_bars[i]);
        lv_obj_set_style_bg_color(audio_bars[i], themed_color(WHITE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(audio_bars[i], LV_OPA_30, LV_PART_MAIN);
        lv_obj_clear_flag(audio_bars[i], LV_OBJ_FLAG_CLICKABLE);
        audio_peaks[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(audio_peaks[i]);
        lv_obj_set_style_bg_color(audio_peaks[i], themed_color(WHITE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(audio_peaks[i], LV_OPA_60, LV_PART_MAIN);
        lv_obj_clear_flag(audio_peaks[i], LV_OBJ_FLAG_CLICKABLE);
        position_audio_bar(i);
    }
}

void create_background() {
    if (current_background == NOW_PLAYING_BACKGROUND_ALBUM_COVER) create_album_background();
    else if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS) create_audio_bars();
}

void create_artwork(const NowPlayingLayoutGeometry &geometry) {
    lv_obj_t *holder = lv_obj_create(screen);
    set_rect(holder, geometry.artwork);
    lv_obj_set_style_bg_color(holder, uses_theme_background() ? themed_color(PANEL) : lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(holder, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(holder, foreground_color(DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(holder, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(holder, geometry.artwork_radius, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(holder, geometry.artwork_radius > 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(holder, 0, LV_PART_MAIN);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    art_placeholder = lv_obj_create(holder);
    lv_obj_remove_style_all(art_placeholder);
    lv_obj_set_size(art_placeholder, LV_PCT(100), LV_PCT(100));
    lv_obj_t *symbol = make_label(art_placeholder, LV_SYMBOL_AUDIO, &lv_font_montserrat_36, GREY);
    lv_obj_center(symbol);
    art_image = lv_image_create(holder);
    lv_obj_center(art_image);
    lv_image_set_scale(art_image, static_cast<uint32_t>(geometry.artwork.width) * 256U / 96U);
    lv_obj_add_flag(art_image, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *make_metadata_label(const NowPlayingRect &rect, const char *text,
                              const lv_font_t *font, uint32_t value) {
    lv_obj_t *result = make_label(screen, text, font, value);
    set_rect(result, rect);
    lv_label_set_long_mode(result, LV_LABEL_LONG_DOT);
    return result;
}

void create_progress(const NowPlayingLayoutGeometry &geometry) {
    progress_bar = lv_bar_create(screen);
    set_rect(progress_bar, geometry.progress);
    lv_bar_set_range(progress_bar, 0, 1000);
    lv_obj_set_style_radius(progress_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, foreground_color(DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar, foreground_color(WHITE), LV_PART_INDICATOR);
    if (current_layout == NOW_PLAYING_LAYOUT_ALBUM_COVER) return;
    position_label = make_label(screen, "0:00", &lv_font_montserrat_12, GREY);
    lv_obj_set_pos(position_label, geometry.progress.x, geometry.progress.y + 8);
    duration_label = make_label(screen, "--:--", &lv_font_montserrat_12, GREY);
    lv_obj_align(duration_label, LV_ALIGN_TOP_RIGHT, -(320 - geometry.progress.x - geometry.progress.width), geometry.progress.y + 8);
}

lv_obj_t *create_control(const NowPlayingRect &rect, const char *symbol,
                         const char *action, lv_obj_t **symbol_out = nullptr) {
    lv_obj_t *button = lv_button_create(screen);
    style_button(button, current_layout == NOW_PLAYING_LAYOUT_ECHODESK ? 0 : 9);
    set_rect(button, rect);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_t *label = make_label(button, symbol, &lv_font_montserrat_20);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, on_control, LV_EVENT_CLICKED, const_cast<char *>(action));
    if (symbol_out) *symbol_out = label;
    return button;
}

void create_corners(const NowPlayingLayoutGeometry &geometry) {
    if (!geometry.animated_corners || !corner_segments) return;
    for (uint8_t i = 0; i < CORNER_COUNT; ++i) {
        corner_segments[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(corner_segments[i]);
        lv_obj_set_style_bg_color(corner_segments[i], foreground_color(WHITE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(corner_segments[i], LV_OPA_COVER, LV_PART_MAIN);
    }
}

void position_corners(int8_t offset, uint8_t opacity) {
    if (!corner_segments || !corner_segments[0]) return;
    const auto &art = now_playing_layout_geometry(current_layout).artwork;
    const int16_t left = art.x - 5, top = art.y - 5;
    const int16_t right = art.x + art.width + 5, bottom = art.y + art.height + 5;
    constexpr int16_t length = 20, thickness = 2;
    const NowPlayingRect segments[CORNER_COUNT] = {
        {int16_t(left-offset),int16_t(top-offset),length,thickness},{int16_t(left-offset),int16_t(top-offset),thickness,length},
        {int16_t(right-length+offset),int16_t(top-offset),length,thickness},{int16_t(right-thickness+offset),int16_t(top-offset),thickness,length},
        {int16_t(left-offset),int16_t(bottom-thickness+offset),length,thickness},{int16_t(left-offset),int16_t(bottom-length+offset),thickness,length},
        {int16_t(right-length+offset),int16_t(bottom-thickness+offset),length,thickness},{int16_t(right-thickness+offset),int16_t(bottom-length+offset),thickness,length},
    };
    for (uint8_t i = 0; i < CORNER_COUNT; ++i) { set_rect(corner_segments[i], segments[i]); lv_obj_set_style_bg_opa(corner_segments[i], opacity, LV_PART_MAIN); }
    last_corner_offset = offset; last_corner_opacity = opacity;
}

void update_corners(uint32_t now) {
    if (!corner_segments || !corner_segments[0]) return;
    const int8_t offset = now_playing_corner_offset(now, corners_playing);
    uint8_t opacity = LV_OPA_90;
    if (corners_playing) {
        const uint16_t phase = now % 1600U;
        const uint16_t folded = phase <= 800U ? phase : 1600U - phase;
        opacity = static_cast<uint8_t>(180U + folded * 75U / 800U);
    }
    if (offset != last_corner_offset || opacity != last_corner_opacity) position_corners(offset, opacity);
}

bool preference_click_allowed() {
    const uint32_t now = millis();
    if (last_preference_touch && now - last_preference_touch < 220) return false;
    last_preference_touch = now; return true;
}

void close_preferences() {
    if (preferences_panel) { lv_obj_delete(preferences_panel); preferences_panel = nullptr; }
}

void queue_preference_change(uint8_t layout, uint8_t background, bool customized) {
    pending_layout = now_playing_layout_normalize(layout);
    pending_background = now_playing_resolve_background(pending_layout, background, customized);
    pending_background_customized = customized;
    preference_change_pending = true;
}

void on_layout_selected(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !preference_click_allowed()) return;
    const uintptr_t encoded = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (!encoded) return;
    const uint8_t selected = now_playing_layout_normalize(static_cast<uint8_t>(encoded - 1));
    if (selected == current_layout) { preference_page_pending = int8_t(PreferencePage::OVERVIEW); return; }
    queue_preference_change(selected, current_background, g_config.now_playing_background_customized);
}

void on_background_selected(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !preference_click_allowed()) return;
    const uintptr_t encoded = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (encoded) queue_preference_change(current_layout, static_cast<uint8_t>(encoded - 1), true);
}

void on_reset_defaults(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && preference_click_allowed())
        queue_preference_change(current_layout, now_playing_default_background(current_layout), false);
}

void on_preference_page(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && preference_click_allowed())
        preference_page_pending = static_cast<int8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
}

lv_obj_t *preference_button(const char *text, int16_t y, int16_t height,
                            lv_event_cb_t callback, uintptr_t data) {
    lv_obj_t *button = lv_button_create(preferences_panel);
    style_button(button, 8);
    lv_obj_set_size(button, 272, height); lv_obj_set_pos(button, 12, y);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_center(make_label(button, text, &lv_font_montserrat_14));
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, reinterpret_cast<void *>(data));
    return button;
}

void open_preferences(PreferencePage page) {
    close_preferences();
    preferences_panel = lv_obj_create(screen);
    lv_obj_set_size(preferences_panel, 296, 194); lv_obj_set_pos(preferences_panel, 12, 40);
    lv_obj_set_style_bg_color(preferences_panel, uses_theme_background() ? themed_color(PANEL) : lv_color_hex(PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(preferences_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(preferences_panel, foreground_color(DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(preferences_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(preferences_panel, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(preferences_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(preferences_panel, LV_OBJ_FLAG_SCROLLABLE);
    static const char *layouts[3] = {"ALBUM COVER", "CARPLAY INSPIRED", "ECHODESK STYLE"};
    static const char *backgrounds[3] = {"ALBUM COVER", "ECHODESK", "AUDIO BARS"};
    if (page == PreferencePage::OVERVIEW) {
        lv_obj_t *heading = make_label(preferences_panel, "CUSTOMISE NOW PLAYING", &lv_font_montserrat_14);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 8);
        char text[64];
        snprintf(text, sizeof(text), "LAYOUT  ·  %s", layouts[current_layout]);
        preference_button(text, 32, 44, on_preference_page, uintptr_t(PreferencePage::LAYOUT));
        snprintf(text, sizeof(text), "BACKGROUND  ·  %s", backgrounds[current_background]);
        preference_button(text, 82, 44, on_preference_page, uintptr_t(PreferencePage::BACKGROUND));
        preference_button("RESTORE LAYOUT DEFAULT", 132, 40, on_reset_defaults, 0);
        lv_obj_t *privacy = make_label(preferences_panel, "AUDIO BARS ANALYSE SYSTEM OUTPUT", &lv_font_montserrat_12, GREY);
        lv_obj_align(privacy, LV_ALIGN_BOTTOM_MID, 0, -3);
    } else {
        lv_obj_t *back = lv_button_create(preferences_panel);
        style_button(back, 7); lv_obj_set_size(back, 48, 28); lv_obj_set_pos(back, 8, 5);
        lv_obj_center(make_label(back, "<", &lv_font_montserrat_16));
        lv_obj_add_event_cb(back, on_preference_page, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(PreferencePage::OVERVIEW)));
        lv_obj_t *heading = make_label(preferences_panel, page == PreferencePage::LAYOUT ? "LAYOUT" : "BACKGROUND", &lv_font_montserrat_14);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 10);
        for (uint8_t i = 0; i < 3; ++i) {
            lv_obj_t *button = preference_button(page == PreferencePage::LAYOUT ? layouts[i] : backgrounds[i], 40 + i * 48, 42,
                page == PreferencePage::LAYOUT ? on_layout_selected : on_background_selected, i + 1);
            const bool selected = page == PreferencePage::LAYOUT ? i == current_layout : i == current_background;
            if (selected) { lv_obj_set_style_border_color(button, foreground_color(WHITE), LV_PART_MAIN); lv_obj_set_style_border_width(button, 2, LV_PART_MAIN); }
        }
    }
    lv_obj_move_foreground(preferences_panel);
}

void on_customize(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (preferences_panel) close_preferences(); else open_preferences(PreferencePage::OVERVIEW);
}

void create_header() {
    lv_obj_t *home = lv_button_create(screen);
    style_button(home, 8); lv_obj_set_size(home, 68, 36); lv_obj_set_pos(home, 4, 4);
    lv_obj_center(make_label(home, "< HOME", &lv_font_montserrat_12));
    lv_obj_add_event_cb(home, on_home, LV_EVENT_CLICKED, nullptr);
    status_label = make_label(screen, "OFFLINE", &lv_font_montserrat_12, GREY);
    lv_obj_set_size(status_label, 184, 18); lv_obj_set_pos(status_label, 68, 12);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_t *customize = lv_button_create(screen);
    style_button(customize, 8); lv_obj_set_size(customize, 46, 36); lv_obj_set_pos(customize, 270, 4);
    lv_obj_center(make_label(customize, LV_SYMBOL_SETTINGS, &lv_font_montserrat_16));
    lv_obj_add_event_cb(customize, on_customize, LV_EVENT_CLICKED, nullptr);
}

void refresh_art(const MusicState &state, bool empty) {
    const uint32_t revision = music_data_art_revision();
    const bool matching = !empty && state.art_id[0] && music_data_art_available() && strcmp(state.art_id, music_data_art_id()) == 0;
    if (!matching || !art_descriptor) { set_hidden(art_image, true); set_hidden(art_placeholder, false); return; }
    if (displayed_art_revision != revision) {
        lv_image_cache_drop(art_descriptor); memset(art_descriptor, 0, sizeof(*art_descriptor));
        art_descriptor->header.magic = LV_IMAGE_HEADER_MAGIC; art_descriptor->header.cf = LV_COLOR_FORMAT_RAW;
        art_descriptor->header.w = 96; art_descriptor->header.h = 96;
        art_descriptor->data_size = music_data_art_size(); art_descriptor->data = music_data_art_data();
        lv_image_set_src(art_image, art_descriptor); displayed_art_revision = revision;
    }
    set_hidden(art_placeholder, true); set_hidden(art_image, false);
}

void refresh_album_background(const MusicState &state, bool empty) {
    if (current_background != NOW_PLAYING_BACKGROUND_ALBUM_COVER || !background_image || !background_descriptor) return;
    const uint32_t revision = music_data_background_revision();
    const bool matching = !empty && state.art_id[0] && music_data_background_available() && strcmp(state.art_id, music_data_background_id()) == 0;
    if (!matching) {
        set_hidden(background_image, true);
        lv_image_cache_drop(background_descriptor);
        music_data_release_retired_background();
        return;
    }
    if (displayed_background_revision != revision) {
        lv_image_cache_drop(background_descriptor); memset(background_descriptor, 0, sizeof(*background_descriptor));
        background_descriptor->header.magic = LV_IMAGE_HEADER_MAGIC; background_descriptor->header.cf = LV_COLOR_FORMAT_RAW;
        background_descriptor->header.w = MUSIC_BACKGROUND_WIDTH; background_descriptor->header.h = MUSIC_BACKGROUND_HEIGHT;
        background_descriptor->data_size = music_data_background_size(); background_descriptor->data = music_data_background_data();
        lv_image_set_src(background_image, background_descriptor); displayed_background_revision = revision;
        music_data_release_retired_background();
        set_hidden(background_image, false); fade_in_background();
    } else set_hidden(background_image, false);
}

const char *visualizer_status_text(MusicVisualizerStatus status) {
    switch (status) {
        case MusicVisualizerStatus::VISUALIZER_LIVE: return nullptr;
        case MusicVisualizerStatus::VISUALIZER_DISABLED: return "AUDIO BARS OFF";
        case MusicVisualizerStatus::VISUALIZER_UNAVAILABLE: return "AUDIO BARS UNAVAILABLE";
        case MusicVisualizerStatus::VISUALIZER_STALE: return "AUDIO BARS STALE";
        default: return "AUDIO BARS CONNECTING";
    }
}

void update_audio_bars(uint32_t now) {
    if (current_background != NOW_PLAYING_BACKGROUND_AUDIO_BARS || !audio_bars || !audio_levels) return;
    MusicVisualizerState visualizer{};
    const MusicVisualizerStatus status = music_data_visualizer_status(now);
    const bool live = status == MusicVisualizerStatus::VISUALIZER_LIVE && music_data_get_visualizer(visualizer) && visualizer.available;
    for (uint8_t i = 0; i < MUSIC_VISUALIZER_BANDS; ++i) {
        const uint8_t target = live && visualizer_playing ? visualizer.bands[i] : 0;
        const uint8_t level = now_playing_smooth_audio_band(audio_levels[i], target);
        audio_levels[i] = level;
        audio_peak_levels[i] = now_playing_decay_audio_peak(audio_peak_levels[i], level);
        position_audio_bar(i);
    }
}

void refresh() {
    if (!screen) return;
    MusicState state{}; const uint32_t now = millis();
    const MusicConnectionState connection = music_data_connection_state(now);
    const bool available = music_data_get(state);
    const bool disconnected = connection == MusicConnectionState::DISCONNECTED || connection == MusicConnectionState::NEVER_RECEIVED;
    const bool empty = !available || (state.playback == MusicPlaybackState::STOPPED && !state.title[0]);
    char status[96];
    const char *state_text = disconnected ? "OFFLINE" : connection == MusicConnectionState::STALE ? "STALE" : empty ? "READY" :
        state.playback == MusicPlaybackState::PLAYING ? "PLAYING" : state.playback == MusicPlaybackState::PAUSED ? "PAUSED" : "STOPPED";
    if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS) {
        const char *visualizer_text = visualizer_status_text(music_data_visualizer_status(now));
        if (visualizer_text) state_text = visualizer_text;
    }
    if (!empty && state.application[0] && current_background != NOW_PLAYING_BACKGROUND_AUDIO_BARS)
        snprintf(status, sizeof(status), "%s  ·  %s", state_text, state.application);
    else strlcpy(status, state_text, sizeof(status));
    lv_label_set_text(status_label, status);
    if (empty) {
        set_bounded_text(title_label, "NOTHING PLAYING", "NOTHING PLAYING");
        set_bounded_text(artist_label, disconnected ? "Start EchoDesk on Windows" : "Start playback on your PC", "");
        set_bounded_text(album_label, "", ""); lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        if (position_label) lv_label_set_text(position_label, "0:00");
        if (duration_label) lv_label_set_text(duration_label, "--:--");
    } else {
        set_bounded_text(title_label, state.title, "Title unavailable"); set_bounded_text(artist_label, state.artist, "Artist unavailable");
        set_bounded_text(album_label, state.album, "Album unavailable");
        const uint32_t position = music_data_display_position_ms(state, now);
        lv_bar_set_value(progress_bar, now_playing_progress_per_mille(position, state.duration_ms), LV_ANIM_OFF);
        char time_text[16];
        if (position_label) { format_time(position, time_text, sizeof(time_text)); lv_label_set_text(position_label, time_text); }
        if (duration_label) { if (state.duration_ms) { format_time(state.duration_ms, time_text, sizeof(time_text)); lv_label_set_text(duration_label, time_text); } else lv_label_set_text(duration_label, "--:--"); }
    }
    lv_label_set_text(play_label, !empty && state.playback == MusicPlaybackState::PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    const bool controls_live = !empty && connection == MusicConnectionState::LIVE;
    set_enabled(previous_button, controls_live && state.can_previous); set_enabled(play_button, controls_live && state.can_play_pause); set_enabled(next_button, controls_live && state.can_next);
    corners_playing = controls_live && state.playback == MusicPlaybackState::PLAYING;
    visualizer_playing = corners_playing;
    refresh_art(state, empty); refresh_album_background(state, empty);
    if (!corners_playing) update_corners(now);
}

} // namespace

void ui_now_playing_set_navigation_callback(UiNowPlayingNavigationCallback callback, void *context) {
    ensure_ui_storage();
    if (!ui) return;
    home_callback = callback; home_context = context;
}

lv_obj_t *ui_now_playing_create() {
    ensure_ui_storage();
    if (!ui) return nullptr;
    if (screen) return screen;
    clear_widget_references();
    current_layout = now_playing_layout_normalize(g_config.now_playing_layout);
    current_background = now_playing_resolve_background(current_layout, g_config.now_playing_background, g_config.now_playing_background_customized);
    const auto &geometry = now_playing_layout_geometry(current_layout);
    screen = lv_obj_create(nullptr); lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_set_style_bg_color(screen, uses_theme_background() ? themed_color(BLACK) : lv_color_hex(BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN); lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN); lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    create_background(); create_artwork(geometry); create_corners(geometry);
    title_label = make_metadata_label(geometry.title, "NOTHING PLAYING", current_layout == NOW_PLAYING_LAYOUT_ALBUM_COVER ? &lv_font_montserrat_16 : &lv_font_montserrat_20, WHITE);
    artist_label = make_metadata_label(geometry.artist, "Start playback on your PC", &lv_font_montserrat_12, GREY);
    if (geometry.album.width) album_label = make_metadata_label(geometry.album, "", &lv_font_montserrat_12, GREY);
    if (current_layout == NOW_PLAYING_LAYOUT_ALBUM_COVER) { lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN); lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN); }
    create_progress(geometry);
    previous_button = create_control(geometry.previous, LV_SYMBOL_PREV, "previous");
    play_button = create_control(geometry.play_pause, LV_SYMBOL_PLAY, "toggle", &play_label);
    next_button = create_control(geometry.next, LV_SYMBOL_NEXT, "next"); create_header();
    displayed_art_revision = displayed_background_revision = UINT32_MAX;
    last_corner_offset = -1; last_corner_opacity = 0; position_corners(0, LV_OPA_90);
    if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS) send_visualizer_subscription(true);
    if (current_background == NOW_PLAYING_BACKGROUND_ALBUM_COVER) send_background_subscription(true);
    refresh(); return screen;
}

lv_obj_t *ui_now_playing_screen() { return ui ? screen : nullptr; }

lv_obj_t *ui_now_playing_recreate() {
    ensure_ui_storage();
    if (!ui) return nullptr;
    const uint8_t replacement_background = now_playing_resolve_background(
        g_config.now_playing_layout, g_config.now_playing_background,
        g_config.now_playing_background_customized);
    if (screen && current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS &&
        replacement_background != NOW_PLAYING_BACKGROUND_AUDIO_BARS) {
        send_visualizer_subscription(false);
    }
    if (screen && current_background == NOW_PLAYING_BACKGROUND_ALBUM_COVER &&
        replacement_background != NOW_PLAYING_BACKGROUND_ALBUM_COVER) {
        send_background_subscription(false);
    }
    lv_obj_t *old = screen; const bool active = old && lv_screen_active() == old;
    screen = nullptr; clear_widget_references(); lv_obj_t *replacement = ui_now_playing_create();
    if (active) lv_screen_load(replacement); if (old && old != replacement) lv_obj_delete(old); return replacement;
}

void ui_now_playing_tick() {
    if (!ui || !screen || lv_screen_active() != screen) return;
    const uint32_t now = millis();
    if (preference_page_pending >= 0) { const PreferencePage page = static_cast<PreferencePage>(preference_page_pending); preference_page_pending = -1; open_preferences(page); }
    if (preference_change_pending) {
        preference_change_pending = false; close_preferences();
        if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS && pending_background != NOW_PLAYING_BACKGROUND_AUDIO_BARS) send_visualizer_subscription(false);
        if (current_background == NOW_PLAYING_BACKGROUND_ALBUM_COVER && pending_background != NOW_PLAYING_BACKGROUND_ALBUM_COVER) send_background_subscription(false);
        g_config.now_playing_layout = now_playing_layout_normalize(pending_layout);
        g_config.now_playing_background = now_playing_resolve_background(g_config.now_playing_layout, pending_background, pending_background_customized);
        g_config.now_playing_background_customized = pending_background_customized;
        config_save(g_config);
        serial_echodesk_state_changed();
        Serial.printf("[EchoDesk] Now Playing layout/background saved: %u/%u (%s)\n", g_config.now_playing_layout, g_config.now_playing_background, g_config.now_playing_background_customized ? "custom" : "default");
        ui_now_playing_recreate(); return;
    }
    if (now - last_refresh >= METADATA_REFRESH_MS) { last_refresh = now; refresh(); }
    if (now - last_animation_refresh >= ANIMATION_REFRESH_MS) {
        last_animation_refresh = now;
        if (current_layout == NOW_PLAYING_LAYOUT_ECHODESK) update_corners(now);
        if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS) update_audio_bars(now);
    }
    if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS && now - last_visualizer_subscribe >= VISUALIZER_SUBSCRIBE_MS) send_visualizer_subscription(true);
    if (current_background == NOW_PLAYING_BACKGROUND_ALBUM_COVER && now - last_background_request >= VISUALIZER_SUBSCRIBE_MS) send_background_subscription(true);
}

void ui_now_playing_leave() {
    if (!ui || !screen) return;
    if (current_background == NOW_PLAYING_BACKGROUND_AUDIO_BARS) {
        send_visualizer_subscription(false);
    }
    if (current_background == NOW_PLAYING_BACKGROUND_ALBUM_COVER) {
        send_background_subscription(false);
    }
}

#else

void ui_now_playing_set_navigation_callback(UiNowPlayingNavigationCallback, void *) {}
lv_obj_t *ui_now_playing_create() { return nullptr; }
lv_obj_t *ui_now_playing_screen() { return nullptr; }
lv_obj_t *ui_now_playing_recreate() { return nullptr; }
void ui_now_playing_tick() {}
void ui_now_playing_leave() {}

#endif
