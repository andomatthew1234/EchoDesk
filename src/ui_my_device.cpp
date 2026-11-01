#include "ui_my_device.h"

#if defined(ECHODESK_OS)

#include "config_store.h"
#include "my_device_health.h"

#include <Arduino.h>
#include <stdio.h>

namespace {

constexpr uint32_t REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t NAVIGATION_GUARD_MS = 250;

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *hero_dot = nullptr;
lv_obj_t *hero_state = nullptr;
lv_obj_t *hero_detail = nullptr;
lv_obj_t *connection_values[2] = {};
lv_obj_t *connection_dots[2] = {};
lv_obj_t *source_values[3] = {};
lv_obj_t *source_dots[3] = {};
lv_obj_t *detail_values[7] = {};

UiMyDeviceNavigationCallback home_callback = nullptr;
void *navigation_context = nullptr;
uint32_t last_refresh_ms = 0;
uint32_t last_navigation_ms = 0;

lv_color_t background_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000);
}

lv_color_t foreground_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x111111) : lv_color_hex(0xFFFFFF);
}

lv_color_t panel_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xF2F3F5) : lv_color_hex(0x111214);
}

lv_color_t pressed_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xDADDE1) : lv_color_hex(0x292B2F);
}

lv_color_t muted_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x5E636B) : lv_color_hex(0xA8ADB5);
}

lv_color_t divider_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xD1D5DB) : lv_color_hex(0x34373C);
}

lv_color_t state_color(MyDeviceHealthState state) {
    switch (state) {
        case MyDeviceHealthState::LIVE: return lv_color_hex(0x20B26B);
        case MyDeviceHealthState::WAITING: return lv_color_hex(0xF2A93B);
        case MyDeviceHealthState::STALE: return lv_color_hex(0xE27A2D);
        case MyDeviceHealthState::OFFLINE: return lv_color_hex(0xE05252);
        default: return muted_color();
    }
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    return label;
}

void style_card(lv_obj_t *card) {
    lv_obj_set_style_bg_color(card, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_card(int16_t y, int16_t height, const char *title) {
    lv_obj_t *card = lv_obj_create(content);
    style_card(card);
    lv_obj_set_size(card, 304, height);
    lv_obj_set_pos(card, 8, y);
    lv_obj_t *heading = make_label(card, title, &lv_font_montserrat_12, muted_color());
    lv_obj_set_pos(heading, 12, 9);
    return card;
}

lv_obj_t *make_dot(lv_obj_t *parent, int16_t x, int16_t y) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, muted_color(), LV_PART_MAIN);
    return dot;
}

void set_dot_state(lv_obj_t *dot, MyDeviceHealthState state) {
    if (dot) lv_obj_set_style_bg_color(dot, state_color(state), LV_PART_MAIN);
}

void make_status_row(lv_obj_t *card, int16_t y, const char *name,
                     lv_obj_t **dot_out, lv_obj_t **value_out) {
    *dot_out = make_dot(card, 13, static_cast<int16_t>(y + 5));
    lv_obj_t *name_label = make_label(card, name, &lv_font_montserrat_12, foreground_color());
    lv_obj_set_pos(name_label, 29, y);
    *value_out = make_label(card, "WAITING", &lv_font_montserrat_12, muted_color());
    lv_obj_set_width(*value_out, 165);
    lv_label_set_long_mode(*value_out, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(*value_out, 126, y);
}

void make_detail_row(lv_obj_t *card, uint8_t index, int16_t y, const char *name) {
    lv_obj_t *name_label = make_label(card, name, &lv_font_montserrat_12, muted_color());
    lv_obj_set_pos(name_label, 13, y);
    detail_values[index] = make_label(card, "--", &lv_font_montserrat_12, foreground_color());
    lv_obj_set_width(detail_values[index], 183);
    lv_label_set_long_mode(detail_values[index], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(detail_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(detail_values[index], 108, y);
}

bool navigation_allowed() {
    const uint32_t now = millis();
    if (now - last_navigation_ms < NAVIGATION_GUARD_MS) return false;
    last_navigation_ms = now;
    return true;
}

void invoke_home(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (home_callback) home_callback(navigation_context);
}

void on_screen_deleted(lv_event_t *event) {
    if (lv_event_get_target(event) != screen) return;
    screen = nullptr;
    content = nullptr;
    hero_dot = nullptr;
    hero_state = nullptr;
    hero_detail = nullptr;
    for (uint8_t i = 0; i < 2; ++i) {
        connection_values[i] = nullptr;
        connection_dots[i] = nullptr;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        source_values[i] = nullptr;
        source_dots[i] = nullptr;
    }
    for (auto &value : detail_values) value = nullptr;
}

void create_header() {
    lv_obj_t *home = lv_button_create(screen);
    lv_obj_set_size(home, 64, 30);
    lv_obj_set_pos(home, 6, 5);
    lv_obj_set_style_bg_color(home, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(home, pressed_color(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(home, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(home, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(home, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(home, 7, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(home, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home, 0, LV_PART_MAIN);
    lv_obj_t *home_label = make_label(home, "HOME", &lv_font_montserrat_12, foreground_color());
    lv_obj_center(home_label);
    lv_obj_add_event_cb(home, invoke_home, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *heading = make_label(screen, "MY DEVICE", &lv_font_montserrat_14, foreground_color());
    lv_obj_set_width(heading, 180);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(heading, 70, 12);

    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 304, 1);
    lv_obj_set_pos(divider, 8, 39);
    lv_obj_set_style_bg_color(divider, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
}

void create_content() {
    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 320, 200);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hero = make_card(4, 48, "ECHODESK DESKTOP");
    hero_dot = make_dot(hero, 13, 28);
    hero_state = make_label(hero, "WAITING", &lv_font_montserrat_20, foreground_color());
    lv_obj_set_pos(hero_state, 30, 19);
    hero_detail = make_label(hero, "Waiting for a trusted host", &lv_font_montserrat_12, muted_color());
    lv_obj_set_width(hero_detail, 168);
    lv_label_set_long_mode(hero_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(hero_detail, 126, 26);

    lv_obj_t *connections = make_card(56, 42, "CONNECTIONS");
    for (uint8_t i = 0; i < 2; ++i) {
        const int16_t x = static_cast<int16_t>(13 + i * 148);
        connection_dots[i] = make_dot(connections, x, 27);
        lv_obj_t *name = make_label(connections,
            i == 0 ? "USB" : "WI-FI", &lv_font_montserrat_12,
            foreground_color());
        lv_obj_set_pos(name, x + 14, 20);
        connection_values[i] = make_label(connections, "WAITING",
                                           &lv_font_montserrat_12,
                                           muted_color());
        lv_obj_set_width(connection_values[i], 82);
        lv_label_set_long_mode(connection_values[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(connection_values[i], x + 52, 20);
    }

    lv_obj_t *sources = make_card(102, 42, "DATA SOURCES");
    static const char *source_names[] = {"CODEX", "MUSIC", "PERF"};
    for (uint8_t i = 0; i < 3; ++i) {
        const int16_t x = static_cast<int16_t>(13 + i * 97);
        source_dots[i] = make_dot(sources, x, 27);
        lv_obj_t *name = make_label(sources, source_names[i],
                                    &lv_font_montserrat_12,
                                    foreground_color());
        lv_obj_set_pos(name, x + 13, 20);
        source_values[i] = make_label(sources, "--", &lv_font_montserrat_12,
                                      muted_color());
        lv_obj_set_width(source_values[i], 34);
        lv_label_set_long_mode(source_values[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(source_values[i], LV_TEXT_ALIGN_RIGHT,
                                    LV_PART_MAIN);
        lv_obj_set_pos(source_values[i], x + 50, 20);
    }

    lv_obj_t *details = make_card(148, 48, "DEVICE DETAILS");
    static const char *detail_names[] = {
        "FW", "UP", "HEAP", "WIFI", "IP", "CORE", "DATA"};
    for (uint8_t i = 0; i < 7; ++i) {
        const uint8_t column = i % 4;
        const uint8_t row = i / 4;
        const int16_t x = static_cast<int16_t>(9 + column * 73);
        const int16_t y = static_cast<int16_t>(19 + row * 13);
        lv_obj_t *name = make_label(details, detail_names[i],
                                    &lv_font_montserrat_12, muted_color());
        lv_obj_set_pos(name, x, y);
        detail_values[i] = make_label(details, "--", &lv_font_montserrat_12,
                                      foreground_color());
        lv_obj_set_width(detail_values[i], 39);
        lv_label_set_long_mode(detail_values[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(detail_values[i], x + 31, y);
    }
}

void format_channel(const MyDeviceHealthChannel &channel, char *out, size_t out_size) {
    char age[24];
    my_device_health_format_age(channel, age, sizeof(age));
    if (age[0]) {
        snprintf(out, out_size, "%s · %s", my_device_health_state_label(channel.state), age);
    } else {
        snprintf(out, out_size, "%s", my_device_health_state_label(channel.state));
    }
}

void update_channel(lv_obj_t *dot, lv_obj_t *label,
                    const MyDeviceHealthChannel &channel) {
    if (!dot || !label) return;
    char text[48];
    format_channel(channel, text, sizeof(text));
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
                                channel.state == MyDeviceHealthState::LIVE
                                    ? foreground_color()
                                    : muted_color(),
                                LV_PART_MAIN);
    set_dot_state(dot, channel.state);
}

void refresh() {
    if (!screen) return;
    MyDeviceHealthSnapshot snapshot{};
    my_device_health_capture(millis(), snapshot);

    char age[24];
    char text[128];
    my_device_health_format_age(snapshot.desktop, age, sizeof(age));
    lv_label_set_text(hero_state, my_device_health_state_label(snapshot.desktop.state));
    lv_obj_set_style_text_color(hero_state, state_color(snapshot.desktop.state), LV_PART_MAIN);
    set_dot_state(hero_dot, snapshot.desktop.state);
    if (snapshot.desktop.state == MyDeviceHealthState::LIVE) {
        snprintf(text, sizeof(text), "%s link · %s", snapshot.serial_transport, age);
    } else if (snapshot.desktop.state == MyDeviceHealthState::WAITING) {
        snprintf(text, sizeof(text), "Waiting for EchoDesk Desktop · %s", snapshot.serial_transport);
    } else {
        snprintf(text, sizeof(text), "EchoDesk Desktop · %s", age);
    }
    lv_label_set_text(hero_detail, text);

    update_channel(connection_dots[0], connection_values[0], snapshot.desktop);
    set_dot_state(connection_dots[1], snapshot.wifi.state);
    if (!snapshot.wifi_configured) {
        lv_label_set_text(connection_values[1], "NOT SET UP");
    } else if (!snapshot.wifi_connected) {
        lv_label_set_text(connection_values[1], "OFFLINE");
    } else {
        lv_label_set_text(connection_values[1],
                          snapshot.time_synced ? "CONNECTED · TIME SYNCED"
                                               : "CONNECTED · CLOCK WAIT");
    }
    lv_obj_set_style_text_color(connection_values[1],
                                snapshot.wifi_connected ? foreground_color()
                                                        : muted_color(),
                                LV_PART_MAIN);
    update_channel(source_dots[0], source_values[0], snapshot.codex);
    update_channel(source_dots[1], source_values[1], snapshot.music);
    update_channel(source_dots[2], source_values[2], snapshot.performance);

    lv_label_set_text(detail_values[0], snapshot.firmware_version[0] ? snapshot.firmware_version : "--");

    my_device_health_format_uptime(snapshot.uptime_seconds, text, sizeof(text));
    lv_label_set_text(detail_values[1], text);

    snprintf(text, sizeof(text), "%lu KB", static_cast<unsigned long>(snapshot.free_heap_bytes / 1024U));
    lv_label_set_text(detail_values[2], text);

    if (!snapshot.wifi_configured) snprintf(text, sizeof(text), "NOT CONFIGURED");
    else snprintf(text, sizeof(text), "%s", snapshot.wifi_ssid[0] ? snapshot.wifi_ssid : "CONFIGURED");
    lv_label_set_text(detail_values[3], text);

    if (snapshot.wifi_connected) {
        snprintf(text, sizeof(text), "%s · %d dBm", snapshot.wifi_ip[0] ? snapshot.wifi_ip : "NO IP",
                 static_cast<int>(snapshot.wifi_rssi_dbm));
    } else {
        snprintf(text, sizeof(text), "%s", snapshot.time_synced ? "OFFLINE · TIME SYNCED" : "OFFLINE");
    }
    lv_label_set_text(detail_values[4], text);

    snprintf(text, sizeof(text), "S%u C%u SET%u W%u", snapshot.scene_protocol,
             snapshot.control_protocol, snapshot.setup_protocol, snapshot.wifi_setup_protocol);
    lv_label_set_text(detail_values[5], text);

    snprintf(text, sizeof(text), "UP%u PERF%u MUS%u", snapshot.update_protocol,
             snapshot.performance_protocol, snapshot.music_protocol);
    lv_label_set_text(detail_values[6], text);
}

} // namespace

void ui_my_device_set_navigation_callback(UiMyDeviceNavigationCallback callback,
                                          void *context) {
    home_callback = callback;
    navigation_context = context;
}

lv_obj_t *ui_my_device_create() {
    if (screen) return screen;
    screen = lv_obj_create(nullptr);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_set_style_bg_color(screen, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    create_header();
    create_content();
    refresh();
    last_refresh_ms = millis();
    return screen;
}

lv_obj_t *ui_my_device_screen() { return screen; }

lv_obj_t *ui_my_device_recreate() {
    lv_obj_t *old = screen;
    const bool active = old && lv_screen_active() == old;
    screen = nullptr;
    content = nullptr;
    lv_obj_t *replacement = ui_my_device_create();
    if (active) lv_screen_load(replacement);
    if (old && old != replacement) lv_obj_delete(old);
    return replacement;
}

void ui_my_device_tick() {
    if (!screen || lv_screen_active() != screen) return;
    const uint32_t now = millis();
    if (now - last_refresh_ms < REFRESH_INTERVAL_MS) return;
    last_refresh_ms = now;
    refresh();
}

#else

void ui_my_device_set_navigation_callback(UiMyDeviceNavigationCallback, void *) {}
lv_obj_t *ui_my_device_create() { return nullptr; }
lv_obj_t *ui_my_device_screen() { return nullptr; }
lv_obj_t *ui_my_device_recreate() { return nullptr; }
void ui_my_device_tick() {}

#endif
