#include "ui_echodesk.h"

#if defined(ECHODESK_OS)

#include "api_common.h"
#include "config.h"
#include "config_store.h"
#include "echodesk_control.h"
#include "echodesk_setup.h"
#include "echodesk_wifi_setup.h"
#include "echodesk_usage_state.h"
#include "serial_receiver.h"
#include "ui_common.h"
#include "ui_now_playing.h"
#include "ui_performance.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(font_standby_clock_76);

namespace {

enum EchoApp : uint8_t {
    ECHO_HOME = 0,
    ECHO_CODEX,
    ECHO_CLOCK,
    ECHO_PERFORMANCE,
    ECHO_NOW_PLAYING,
    ECHO_SETTINGS,
};

enum class SetupPage : uint8_t {
    HELLO,
    LANGUAGE,
    METHOD,
    DOWNLOAD,
    WIFI,
    IN_PROGRESS,
};

constexpr uint32_t COLOR_BLACK = 0x000000;
constexpr uint32_t COLOR_WHITE = 0xFFFFFF;
constexpr uint32_t ECHO_COLOR_PANEL = 0x0E0E0E;
constexpr uint32_t COLOR_PRESSED = 0x242424;
constexpr uint32_t COLOR_GREY = 0x8A8A8A;
constexpr uint32_t COLOR_DIM = 0x3C3C3C;
constexpr unsigned long NAV_GUARD_MS = 350;
constexpr unsigned long UI_REFRESH_MS = 1000;

lv_obj_t *home_screen = nullptr;
lv_obj_t *codex_screen = nullptr;
lv_obj_t *clock_screen = nullptr;
lv_obj_t *settings_screen = nullptr;
lv_obj_t *setup_screen = nullptr;
EchoApp current_app = ECHO_HOME;
unsigned long last_navigation_ms = 0;
unsigned long last_refresh_ms = 0;
unsigned long restart_at_ms = 0;
EchoApp pending_remote_app = ECHO_HOME;
bool remote_app_pending = false;
bool setup_state_change_pending = false;
SetupPage setup_page = SetupPage::HELLO;
unsigned long setup_hello_reveal_at = 0;
lv_obj_t *setup_hello_prompt = nullptr;
lv_obj_t *setup_hello_button = nullptr;
lv_obj_t *setup_wifi_status = nullptr;
lv_obj_t *setup_wifi_retry = nullptr;

struct UsageWidgets {
    lv_obj_t *container = nullptr;
    lv_obj_t *value = nullptr;
    lv_obj_t *bar = nullptr;
    lv_obj_t *reset = nullptr;
};

lv_obj_t *codex_status = nullptr;
lv_obj_t *codex_mode = nullptr;
lv_obj_t *codex_loading = nullptr;
lv_obj_t *codex_loading_label = nullptr;
UsageWidgets session_widgets;
UsageWidgets weekly_widgets;

lv_obj_t *clock_outline[8] = {};
lv_obj_t *clock_hour_cutout = nullptr;
lv_obj_t *clock_minute = nullptr;
lv_obj_t *clock_ampm = nullptr;
lv_obj_t *clock_loading = nullptr;
lv_obj_t *clock_waiting = nullptr;
char last_clock_hour[3] = "";
char last_clock_minute[3] = "";
char last_clock_ampm[3] = "";

lv_obj_t *brightness_slider = nullptr;
lv_obj_t *brightness_value = nullptr;
lv_obj_t *restart_modal = nullptr;
lv_obj_t *factory_reset_modal = nullptr;
bool theme_rebuild_pending = false;

lv_color_t color(uint32_t hex) {
    if (g_config.theme == THEME_LIGHT) {
        if (hex == COLOR_BLACK) return lv_color_hex(COLOR_WHITE);
        if (hex == COLOR_WHITE) return lv_color_hex(COLOR_BLACK);
        if (hex == ECHO_COLOR_PANEL) return lv_color_hex(0xF1F1F1);
        if (hex == COLOR_PRESSED) return lv_color_hex(0xD8D8D8);
        if (hex == COLOR_GREY) return lv_color_hex(0x555555);
        if (hex == COLOR_DIM) return lv_color_hex(0xB8B8B8);
    }
    return lv_color_hex(hex);
}

lv_obj_t *make_spinner(lv_obj_t *parent, int16_t size) {
    lv_obj_t *spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, size, size);
    lv_spinner_set_anim_params(spinner, 800, 260);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, color(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, color(COLOR_WHITE), LV_PART_INDICATOR);
    return spinner;
}

void style_screen(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, color(COLOR_BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void style_button(lv_obj_t *button, uint8_t radius = 10) {
    lv_obj_set_style_bg_color(button, color(ECHO_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, color(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, color(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, color(COLOR_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, color(COLOR_WHITE), LV_PART_MAIN | LV_STATE_PRESSED);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     lv_color_t text_color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    return label;
}

bool navigation_allowed();
void add_delete_tracking(lv_obj_t *screen, lv_obj_t **slot);

void setup_style_screen(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *setup_make_label(lv_obj_t *parent, const char *text,
                           const lv_font_t *font = &lv_font_montserrat_16) {
    lv_obj_t *label = make_label(parent, text, font, lv_color_white());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}

lv_obj_t *setup_make_button(lv_obj_t *parent, const char *text,
                            int16_t x, int16_t y, int16_t width) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 44);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xD8D8D8),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_14,
                                 lv_color_black());
    lv_obj_center(label);
    return button;
}

lv_obj_t *setup_rect(lv_obj_t *parent, int16_t width, int16_t height,
                     int16_t x, int16_t y) {
    lv_obj_t *rect = lv_obj_create(parent);
    lv_obj_remove_style_all(rect);
    lv_obj_set_size(rect, width, height);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_style_bg_color(rect, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    return rect;
}

lv_obj_t *setup_corner(lv_obj_t *parent, bool bottom_right,
                       int16_t x, int16_t y) {
    constexpr int16_t width = 72;
    constexpr int16_t height = 52;
    constexpr int16_t thickness = 3;
    lv_obj_t *corner = lv_obj_create(parent);
    lv_obj_remove_style_all(corner);
    lv_obj_set_size(corner, width, height);
    lv_obj_set_pos(corner, x, y);
    if (bottom_right) {
        setup_rect(corner, width, thickness, 0, height - thickness);
        setup_rect(corner, thickness, height, width - thickness, 0);
    } else {
        setup_rect(corner, width, thickness, 0, 0);
        setup_rect(corner, thickness, height, 0, 0);
    }
    return corner;
}

void setup_anim_set_x(void *object, int32_t value) {
    lv_obj_set_x(static_cast<lv_obj_t *>(object), value);
}

void setup_anim_set_y(void *object, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
}

void setup_animate_corner(lv_obj_t *corner, int16_t x, int16_t y,
                          int16_t delta_x, int16_t delta_y) {
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, corner);
    lv_anim_set_exec_cb(&animation, setup_anim_set_x);
    lv_anim_set_values(&animation, x, x + delta_x);
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_reverse_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, corner);
    lv_anim_set_exec_cb(&animation, setup_anim_set_y);
    lv_anim_set_values(&animation, y, y + delta_y);
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_reverse_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

lv_obj_t *create_setup_page(SetupPage page);

void load_setup_page(SetupPage page, bool animate = true) {
    lv_obj_t *old = lv_screen_active();
    lv_obj_t *next = create_setup_page(page);
    if (animate) {
        lv_screen_load_anim(next, LV_SCR_LOAD_ANIM_FADE_IN, 180, 0,
                            old && old != next);
    } else {
        lv_screen_load(next);
        if (old && old != next) lv_obj_delete(old);
    }
    lv_refr_now(nullptr);
}

void on_setup_begin(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    load_setup_page(SetupPage::LANGUAGE);
}

void on_setup_language_continue(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    g_config.language = LANG_EN;
    config_save(g_config);
    load_setup_page(SetupPage::METHOD);
}

void begin_setup_transport(uint8_t transport) {
    g_config.setup_transport = echodesk_setup_normalize_transport(transport);
    g_config.setup_state = ECHODESK_SETUP_WAITING_FOR_DESKTOP;
    g_config.setup_version = ECHODESK_SETUP_VERSION;
    config_save(g_config);
    serial_echodesk_state_changed();
}

void on_setup_usb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    echodesk_wifi_setup_stop();
    begin_setup_transport(ECHODESK_SETUP_TRANSPORT_USB);
    load_setup_page(SetupPage::DOWNLOAD);
}

void on_setup_wifi(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    begin_setup_transport(ECHODESK_SETUP_TRANSPORT_WIFI);
    echodesk_wifi_setup_request_start();
    load_setup_page(SetupPage::WIFI);
}

void on_setup_wifi_retry(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    echodesk_wifi_setup_request_start();
    load_setup_page(SetupPage::WIFI, false);
}

lv_obj_t *create_setup_page(SetupPage page) {
    setup_page = page;
    setup_hello_prompt = nullptr;
    setup_hello_button = nullptr;
    setup_wifi_status = nullptr;
    setup_wifi_retry = nullptr;
    lv_obj_t *screen = lv_obj_create(nullptr);
    setup_screen = screen;
    add_delete_tracking(screen, &setup_screen);
    setup_style_screen(screen);

    if (page == SetupPage::HELLO) {
        lv_obj_t *top = setup_corner(screen, false, 18, 16);
        lv_obj_t *bottom = setup_corner(screen, true, 230, 172);
        setup_animate_corner(top, 18, 16, 6, 5);
        setup_animate_corner(bottom, 230, 172, -6, -5);
        lv_obj_t *hello = setup_make_label(screen, "Hello.",
                                           &lv_font_montserrat_48);
        lv_obj_align(hello, LV_ALIGN_CENTER, 0, -23);
        setup_hello_prompt = setup_make_label(screen, "Let's begin.",
                                              &lv_font_montserrat_16);
        lv_obj_align(setup_hello_prompt, LV_ALIGN_CENTER, 0, 35);
        setup_hello_button = setup_make_button(screen, "LET'S BEGIN", 88, 178, 144);
        lv_obj_add_event_cb(setup_hello_button, on_setup_begin,
                            LV_EVENT_CLICKED, nullptr);
        lv_obj_add_flag(setup_hello_prompt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(setup_hello_button, LV_OBJ_FLAG_HIDDEN);
        setup_hello_reveal_at = millis() + 1100;
    } else if (page == SetupPage::LANGUAGE) {
        lv_obj_t *heading = setup_make_label(screen, "Choose your language.",
                                             &lv_font_montserrat_24);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 24);
        lv_obj_t *note = setup_make_label(screen,
            "More languages can be added in a future EchoDesk OS update.",
            &lv_font_montserrat_12);
        lv_obj_set_width(note, 280);
        lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_t *language = lv_obj_create(screen);
        lv_obj_set_size(language, 280, 56);
        lv_obj_set_pos(language, 20, 102);
        lv_obj_set_style_bg_color(language, lv_color_hex(0x111111), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(language, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(language, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(language, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(language, 12, LV_PART_MAIN);
        lv_obj_t *english = setup_make_label(language, "English    SELECTED",
                                              &lv_font_montserrat_16);
        lv_obj_center(english);
        lv_obj_t *button = setup_make_button(screen, "CONTINUE", 88, 180, 144);
        lv_obj_add_event_cb(button, on_setup_language_continue,
                            LV_EVENT_CLICKED, nullptr);
    } else if (page == SetupPage::METHOD) {
        lv_obj_t *heading = setup_make_label(screen, "How do you want to set up?",
                                             &lv_font_montserrat_20);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 25);
        lv_obj_t *body = setup_make_label(
            screen, "USB stays the default. Wi-Fi is opt-in and can be changed later.",
            &lv_font_montserrat_12);
        lv_obj_set_width(body, 290);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 58);
        lv_obj_t *usb = setup_make_button(screen, "SET UP WITH USB", 22, 110, 132);
        lv_obj_add_event_cb(usb, on_setup_usb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *wifi = setup_make_button(screen, "SET UP WITH WI-FI", 166, 110, 132);
        lv_obj_add_event_cb(wifi, on_setup_wifi, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *note = setup_make_label(
            screen, "USB usage data remains available in either mode.",
            &lv_font_montserrat_12);
        lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -28);
    } else if (page == SetupPage::DOWNLOAD) {
        lv_obj_t *icon = setup_make_label(screen, LV_SYMBOL_DOWNLOAD,
                                          &lv_font_montserrat_36);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 17);
        lv_obj_t *heading = setup_make_label(screen, "Get EchoDesk Desktop.",
                                             &lv_font_montserrat_20);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 62);
        lv_obj_t *body = setup_make_label(
            screen,
            "Download EchoDesk Desktop on your PC, then select\n'Set up a new device' to continue.",
            &lv_font_montserrat_12);
        lv_obj_set_width(body, 296);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 96);
        const char *url = ECHODESK_SETUP_URL;
        lv_obj_t *url_label = setup_make_label(
            screen, url[0] ? url : "DOWNLOAD URL NOT CONFIGURED",
            &lv_font_montserrat_12);
        lv_obj_set_width(url_label, 292);
        lv_label_set_long_mode(url_label, LV_LABEL_LONG_DOT);
        lv_obj_align(url_label, LV_ALIGN_BOTTOM_MID, 0, -30);
        lv_obj_t *waiting = make_spinner(screen, 22);
        lv_obj_align(waiting, LV_ALIGN_BOTTOM_MID, 0, -55);
    } else if (page == SetupPage::WIFI) {
        char details[128];
        snprintf(details, sizeof(details),
                 "1. On your PC, join: %s\n2. Wi-Fi password: %s\n3. In EchoDesk Desktop, select Find Wi-Fi devices.",
                 echodesk_wifi_setup_ap_ssid(),
                 echodesk_wifi_setup_pairing_code());
        lv_obj_t *heading = setup_make_label(screen, "Set up with Wi-Fi",
                                             &lv_font_montserrat_20);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 14);
        lv_obj_t *details_label = setup_make_label(screen, details,
                                                   &lv_font_montserrat_12);
        lv_obj_set_width(details_label, 300);
        lv_obj_set_style_text_align(details_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_pos(details_label, 10, 52);
        lv_obj_t *spinner = make_spinner(screen, 20);
        lv_obj_set_pos(spinner, 24, 185);
        setup_wifi_status = setup_make_label(screen, "Temporary setup network is starting...",
                                             &lv_font_montserrat_12);
        lv_obj_set_width(setup_wifi_status, 245);
        lv_obj_set_style_text_align(setup_wifi_status, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_pos(setup_wifi_status, 55, 187);
        setup_wifi_retry = setup_make_button(screen, "RETRY", 110, 181, 100);
        lv_obj_add_event_cb(setup_wifi_retry, on_setup_wifi_retry,
                            LV_EVENT_CLICKED, nullptr);
        lv_obj_add_flag(setup_wifi_retry, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_t *top = setup_corner(screen, false, 18, 16);
        lv_obj_t *bottom = setup_corner(screen, true, 230, 172);
        setup_animate_corner(top, 18, 16, 4, 4);
        setup_animate_corner(bottom, 230, 172, -4, -4);
        lv_obj_t *spinner = make_spinner(screen, 34);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -35);
        lv_obj_t *heading = setup_make_label(
            screen, "Connected to EchoDesk Desktop",
            &lv_font_montserrat_16);
        lv_obj_set_width(heading, 280);
        lv_obj_align(heading, LV_ALIGN_CENTER, 0, 14);
        lv_obj_t *body = setup_make_label(
            screen, "Preparing setup. Keep the USB cable connected.",
            &lv_font_montserrat_12);
        lv_obj_set_width(body, 280);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, 47);
    }
    return screen;
}

SetupPage setup_page_for_state(uint8_t state) {
    switch (echodesk_setup_normalize_state(state)) {
        case ECHODESK_SETUP_WAITING_FOR_DESKTOP:
            return echodesk_setup_normalize_transport(g_config.setup_transport) ==
                           ECHODESK_SETUP_TRANSPORT_WIFI
                       ? SetupPage::WIFI
                       : SetupPage::DOWNLOAD;
        case ECHODESK_SETUP_IN_PROGRESS: return SetupPage::IN_PROGRESS;
        default: return SetupPage::HELLO;
    }
}

void on_screen_deleted(lv_event_t *event) {
    lv_obj_t **slot = static_cast<lv_obj_t **>(lv_event_get_user_data(event));
    if (slot && *slot == lv_event_get_target(event)) {
        *slot = nullptr;
    }
}

void add_delete_tracking(lv_obj_t *screen, lv_obj_t **slot) {
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, slot);
}

bool navigation_allowed() {
    unsigned long now = millis();
    if (now - last_navigation_ms < NAV_GUARD_MS) return false;
    last_navigation_ms = now;
    return true;
}

void show_app(EchoApp app, bool animate = true);

void on_home_pressed(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    show_app(ECHO_HOME);
}

lv_obj_t *create_home_control(lv_obj_t *parent) {
    lv_obj_t *button = lv_button_create(parent);
    style_button(button, 8);
    lv_obj_set_size(button, 66, 28);
    lv_obj_set_pos(button, 8, 6);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_t *label = make_label(button, "<  HOME", &lv_font_montserrat_12, color(COLOR_WHITE));
    lv_obj_center(label);
    lv_obj_add_event_cb(button, on_home_pressed, LV_EVENT_CLICKED, nullptr);
    return button;
}

void add_header(lv_obj_t *screen, const char *title) {
    create_home_control(screen);
    lv_obj_t *heading = make_label(screen, title, &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 304, 1);
    lv_obj_set_pos(line, 8, 39);
    lv_obj_set_style_bg_color(line, color(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
}

void add_small_wordmark(lv_obj_t *parent) {
    static const int8_t offsets[][2] = {
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
        {1, 0}, {-1, 1}, {0, 1}, {1, 1},
    };
    for (const auto &offset : offsets) {
        lv_obj_t *label = make_label(parent, "ECHO", &lv_font_montserrat_20, color(COLOR_WHITE));
        lv_obj_set_style_text_letter_space(label, -1, LV_PART_MAIN);
        lv_obj_set_pos(label, 12 + offset[0], 8 + offset[1]);
    }
    lv_obj_t *cutout = make_label(parent, "ECHO", &lv_font_montserrat_20, color(COLOR_BLACK));
    lv_obj_set_style_text_letter_space(cutout, -1, LV_PART_MAIN);
    lv_obj_set_pos(cutout, 12, 8);
    lv_obj_t *desk = make_label(parent, "DESK", &lv_font_montserrat_20, color(COLOR_WHITE));
    lv_obj_set_style_text_letter_space(desk, 1, LV_PART_MAIN);
    lv_obj_set_pos(desk, 73, 8);
    lv_obj_t *version = make_label(parent, "OS  1.0", &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -12, 13);
}

void on_home_card(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    EchoApp app = static_cast<EchoApp>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    show_app(app);
}

void add_home_card(lv_obj_t *screen, int16_t x, int16_t y, int16_t width,
                   EchoApp app, const char *icon, const char *title,
                   const char *subtitle) {
    lv_obj_t *button = lv_button_create(screen);
    style_button(button, 12);
    lv_obj_set_size(button, width, 55);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(button, on_home_card, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(app)));

    lv_obj_t *icon_box = lv_obj_create(button);
    lv_obj_remove_style_all(icon_box);
    lv_obj_set_size(icon_box, 30, 30);
    lv_obj_set_pos(icon_box, 8, 7);
    lv_obj_set_style_border_color(icon_box, color(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(icon_box, 8, LV_PART_MAIN);
    lv_obj_t *icon_label = make_label(icon_box, icon, &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_center(icon_label);

    const lv_font_t *title_font = strlen(title) > 9 ? &lv_font_montserrat_12
                                                    : &lv_font_montserrat_14;
    lv_obj_t *title_label = make_label(button, title, title_font, color(COLOR_WHITE));
    lv_obj_set_pos(title_label, 45, 6);
    lv_obj_t *sub_label = make_label(button, subtitle, &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_set_width(sub_label, width - 18);
    lv_label_set_long_mode(sub_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(sub_label, 9, 36);
    lv_obj_t *arrow = make_label(button, ">", &lv_font_montserrat_16, color(COLOR_GREY));
    lv_obj_align(arrow, LV_ALIGN_TOP_RIGHT, -8, 11);
}

lv_obj_t *create_home_screen() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    home_screen = screen;
    add_delete_tracking(screen, &home_screen);
    style_screen(screen);
    add_small_wordmark(screen);
    add_home_card(screen, 8, 43, 146, ECHO_CODEX, "C", "CODEX", "Usage limits");
    add_home_card(screen, 166, 43, 146, ECHO_CLOCK, "12", "CLOCK", "Local time");
    add_home_card(screen, 8, 104, 146, ECHO_PERFORMANCE, "^", "PERFORMANCE", "PC activity");
    add_home_card(screen, 166, 104, 146, ECHO_NOW_PLAYING, LV_SYMBOL_AUDIO,
                  "NOW PLAYING", "Windows media");
    add_home_card(screen, 8, 165, 304, ECHO_SETTINGS, "*", "SETTINGS", "Display & restart");
    return screen;
}

UsageWidgets create_usage_card(lv_obj_t *screen, int16_t y, const char *title) {
    UsageWidgets widgets;
    lv_obj_t *panel = lv_obj_create(screen);
    widgets.container = panel;
    lv_obj_set_size(panel, 304, 83);
    lv_obj_set_pos(panel, 8, y);
    lv_obj_set_style_bg_color(panel, color(ECHO_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = make_label(panel, title, &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_set_pos(name, 12, 8);
    widgets.value = make_label(panel, "--", &lv_font_montserrat_36, color(COLOR_WHITE));
    lv_obj_align(widgets.value, LV_ALIGN_TOP_RIGHT, -12, 1);

    widgets.bar = lv_bar_create(panel);
    lv_obj_set_size(widgets.bar, 280, 8);
    lv_obj_set_pos(widgets.bar, 12, 45);
    lv_bar_set_range(widgets.bar, 0, 100);
    lv_obj_set_style_radius(widgets.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(widgets.bar, color(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widgets.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(widgets.bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(widgets.bar, color(COLOR_WHITE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(widgets.bar, LV_OPA_COVER, LV_PART_INDICATOR);

    widgets.reset = make_label(panel, "WAITING FOR DATA", &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_set_pos(widgets.reset, 12, 61);
    return widgets;
}

lv_obj_t *create_codex_screen() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    codex_screen = screen;
    add_delete_tracking(screen, &codex_screen);
    style_screen(screen);
    add_header(screen, "CODEX USAGE");

    codex_status = make_label(screen, "WAITING", &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_align(codex_status, LV_ALIGN_TOP_RIGHT, -10, 12);
    codex_mode = make_label(screen, "USED", &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_set_pos(codex_mode, 10, 43);
    session_widgets = create_usage_card(screen, 58, "5-HOUR SESSION");
    weekly_widgets = create_usage_card(screen, 149, "WEEKLY");
    codex_loading = make_spinner(screen, 38);
    lv_obj_align(codex_loading, LV_ALIGN_CENTER, 0, -5);
    codex_loading_label = make_label(screen, "WAITING FOR CODEX DATA",
                                    &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_set_width(codex_loading_label, 280);
    lv_label_set_long_mode(codex_loading_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(codex_loading_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(codex_loading_label, LV_ALIGN_CENTER, 0, 31);
    lv_obj_add_flag(codex_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(codex_loading_label, LV_OBJ_FLAG_HIDDEN);
    return screen;
}

int percent_from_fraction(float fraction) {
    if (!isfinite(fraction)) return 0;
    int value = static_cast<int>(fraction * 100.0f + 0.5f);
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    return value;
}

void update_usage_widgets(const UsageWidgets &widgets, float fraction,
                          time_t reset_epoch, bool inactive) {
    if (inactive) {
        lv_label_set_text(widgets.value, "--");
        lv_bar_set_value(widgets.bar, 0, LV_ANIM_OFF);
        lv_label_set_text(widgets.reset, "INACTIVE - NO CURRENT WINDOW");
        return;
    }

    int percentage = percent_from_fraction(fraction);
    char value[8];
    snprintf(value, sizeof(value), "%d%%", percentage);
    lv_label_set_text(widgets.value, value);
    lv_bar_set_value(widgets.bar, percentage, LV_ANIM_OFF);

    char countdown[32];
    format_reset_compact(reset_epoch, countdown, sizeof(countdown));
    char reset_text[48];
    if (reset_epoch > 0) {
        snprintf(reset_text, sizeof(reset_text), "RESET IN  %s", countdown);
    } else {
        strlcpy(reset_text, "RESET TIME UNAVAILABLE", sizeof(reset_text));
    }
    lv_label_set_text(widgets.reset, reset_text);
}

float configured_codex_fraction(float fraction, bool source_shows_remaining) {
    const bool target_shows_remaining =
        g_config.codex_percentage_mode == CODEX_PERCENTAGE_REMAINING;
    if (source_shows_remaining != target_shows_remaining && isfinite(fraction)) {
        return 1.0f - fraction;
    }
    return fraction;
}

void update_codex() {
    if (!codex_screen) return;

    MonitorState codex = {};
    bool found = serial_get_provider_state(PROVIDER_OPENAI, codex);
    const EchoDeskUsageStateInput state_input = {
        found,
        codex.usage.valid,
        codex.usage.notice_only,
        codex.is_fetching,
        static_cast<uint32_t>(millis()),
        static_cast<uint32_t>(codex.usage.last_fetch),
        codex.usage.error,
    };
    const EchoDeskUsageDisplayState display_state =
        echodesk_classify_usage_state(state_input);
    const bool usable = display_state == EchoDeskUsageDisplayState::LIVE;

    if (usable) {
        lv_label_set_text(codex_status, "CONNECTED");
        lv_obj_set_style_text_color(codex_status, color(COLOR_WHITE), LV_PART_MAIN);
    } else if (display_state == EchoDeskUsageDisplayState::STALE) {
        lv_label_set_text(codex_status, "STALE");
        lv_obj_set_style_text_color(codex_status, color(COLOR_GREY), LV_PART_MAIN);
    } else if (display_state == EchoDeskUsageDisplayState::AUTH_REQUIRED) {
        lv_label_set_text(codex_status, "SIGN IN");
        lv_obj_set_style_text_color(codex_status, color(COLOR_GREY), LV_PART_MAIN);
    } else if (display_state == EchoDeskUsageDisplayState::ATTENTION) {
        lv_label_set_text(codex_status, "ATTENTION");
        lv_obj_set_style_text_color(codex_status, color(COLOR_GREY), LV_PART_MAIN);
    } else if (display_state == EchoDeskUsageDisplayState::WAITING) {
        lv_label_set_text(codex_status, "CONNECTING");
        lv_obj_set_style_text_color(codex_status, color(COLOR_GREY), LV_PART_MAIN);
    } else {
        lv_label_set_text(codex_status, "NO DATA");
        lv_obj_set_style_text_color(codex_status, color(COLOR_GREY), LV_PART_MAIN);
    }

    if (!usable) {
        lv_label_set_text(codex_mode, "LIVE DATA UNAVAILABLE");
        const char *message = "OPEN ECHODESK DESKTOP TO ENABLE AI USAGE";
        bool show_spinner = true;
        switch (display_state) {
            case EchoDeskUsageDisplayState::WAITING:
                message = "WAITING FOR CODEX DATA";
                break;
            case EchoDeskUsageDisplayState::AUTH_REQUIRED:
                message = "SIGN IN TO CODEX IN ECHODESK DESKTOP";
                show_spinner = false;
                break;
            case EchoDeskUsageDisplayState::ATTENTION:
                message = codex.usage.error[0] ? codex.usage.error : "CODEX NEEDS ATTENTION";
                show_spinner = false;
                break;
            case EchoDeskUsageDisplayState::STALE:
                message = "RECONNECT ECHODESK DESKTOP";
                show_spinner = false;
                break;
            case EchoDeskUsageDisplayState::NEVER_CONNECTED:
                break;
            case EchoDeskUsageDisplayState::LIVE:
                break;
        }
        lv_label_set_text(codex_loading_label, message);
        lv_obj_add_flag(session_widgets.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(weekly_widgets.container, LV_OBJ_FLAG_HIDDEN);
        if (show_spinner) {
            lv_obj_clear_flag(codex_loading, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(codex_loading, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(codex_loading_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(codex_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(codex_loading_label, LV_OBJ_FLAG_HIDDEN);
    const bool show_remaining =
        g_config.codex_percentage_mode == CODEX_PERCENTAGE_REMAINING;
    lv_label_set_text(codex_mode, show_remaining ? "PERCENT LEFT" : "PERCENT USED");
    bool session_inactive = echodesk_usage_window_is_inactive(
        codex.usage.five_hour_reset_epoch, codex.usage.five_hour_window_minutes);
    bool weekly_inactive = echodesk_usage_window_is_inactive(
        codex.usage.seven_day_reset_epoch, codex.usage.seven_day_window_minutes);
    const bool weekly_only = g_config.codex_preferred_display == CODEX_DISPLAY_WEEKLY;
    const bool session_only =
        g_config.codex_preferred_display == CODEX_DISPLAY_SESSION && !session_inactive;
    if (session_inactive || weekly_only) {
        lv_obj_add_flag(session_widgets.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(weekly_widgets.container, 92);
    } else {
        lv_obj_clear_flag(session_widgets.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(session_widgets.container, session_only ? 92 : 58);
        lv_obj_set_y(weekly_widgets.container, 149);
        update_usage_widgets(session_widgets,
                             configured_codex_fraction(
                                 codex.usage.five_hour_utilization,
                                 codex.usage.shows_remaining),
                             codex.usage.five_hour_reset_epoch, false);
    }
    if (session_only) {
        lv_obj_add_flag(weekly_widgets.container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(weekly_widgets.container, LV_OBJ_FLAG_HIDDEN);
        update_usage_widgets(weekly_widgets,
                             configured_codex_fraction(
                                 codex.usage.seven_day_utilization,
                                 codex.usage.shows_remaining),
                             codex.usage.seven_day_reset_epoch, weekly_inactive);
    }
}

void set_clock_visible(bool visible) {
    for (lv_obj_t *label : clock_outline) {
        if (label) visible ? lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN)
                           : lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    if (clock_hour_cutout) visible ? lv_obj_clear_flag(clock_hour_cutout, LV_OBJ_FLAG_HIDDEN)
                                   : lv_obj_add_flag(clock_hour_cutout, LV_OBJ_FLAG_HIDDEN);
    if (clock_minute) visible ? lv_obj_clear_flag(clock_minute, LV_OBJ_FLAG_HIDDEN)
                              : lv_obj_add_flag(clock_minute, LV_OBJ_FLAG_HIDDEN);
    if (clock_ampm) visible ? lv_obj_clear_flag(clock_ampm, LV_OBJ_FLAG_HIDDEN)
                            : lv_obj_add_flag(clock_ampm, LV_OBJ_FLAG_HIDDEN);
    if (clock_loading) visible ? lv_obj_add_flag(clock_loading, LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_clear_flag(clock_loading, LV_OBJ_FLAG_HIDDEN);
    if (clock_waiting) visible ? lv_obj_add_flag(clock_waiting, LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_clear_flag(clock_waiting, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *create_clock_screen() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    clock_screen = screen;
    add_delete_tracking(screen, &clock_screen);
    style_screen(screen);
    const char *heading = g_config.timezone == ECHODESK_TIMEZONE_UTC ? "UTC" :
                          g_config.timezone == ECHODESK_TIMEZONE_SYDNEY ? "SYDNEY" :
                          "MELBOURNE";
    add_header(screen, heading);

    static const int8_t outline_offsets[][2] = {
        {-2, -2}, {0, -2}, {2, -2}, {-2, 0},
        {2, 0}, {-2, 2}, {0, 2}, {2, 2},
    };
    for (uint8_t i = 0; i < 8; ++i) {
        clock_outline[i] = make_label(screen, "12", &font_standby_clock_76, color(COLOR_WHITE));
        lv_obj_align(clock_outline[i], LV_ALIGN_CENTER,
                     outline_offsets[i][0], -39 + outline_offsets[i][1]);
    }
    clock_hour_cutout = make_label(screen, "12", &font_standby_clock_76, color(COLOR_BLACK));
    lv_obj_align(clock_hour_cutout, LV_ALIGN_CENTER, 0, -39);
    clock_minute = make_label(screen, "00", &font_standby_clock_76, color(COLOR_WHITE));
    lv_obj_align(clock_minute, LV_ALIGN_CENTER, 0, 38);
    clock_ampm = make_label(screen, "AM", &lv_font_montserrat_14, color(COLOR_GREY));
    lv_obj_align(clock_ampm, LV_ALIGN_RIGHT_MID, -22, -38);
    clock_loading = make_spinner(screen, 38);
    lv_obj_align(clock_loading, LV_ALIGN_CENTER, 0, -8);
    clock_waiting = make_label(screen, "WAITING FOR TIME", &lv_font_montserrat_16, color(COLOR_GREY));
    lv_obj_align(clock_waiting, LV_ALIGN_CENTER, 0, 28);
    last_clock_hour[0] = '\0';
    last_clock_minute[0] = '\0';
    last_clock_ampm[0] = '\0';
    return screen;
}

void update_clock() {
    if (!clock_screen) return;
    time_t now = time(nullptr);
    if (now <= CLOCK_VALID_EPOCH) {
        set_clock_visible(false);
        return;
    }

    struct tm local_time = {};
    localtime_r(&now, &local_time);
    char hour[3];
    char minute[3];
    char ampm[3];
    const bool format_24h = g_config.clock_format == CLOCK_FORMAT_24_HOUR;
    strftime(hour, sizeof(hour), format_24h ? "%H" : "%I", &local_time);
    strftime(minute, sizeof(minute), "%M", &local_time);
    if (format_24h) ampm[0] = '\0';
    else strftime(ampm, sizeof(ampm), "%p", &local_time);
    if (!format_24h && hour[0] == '0') {
        hour[0] = hour[1];
        hour[1] = '\0';
    }

    set_clock_visible(true);
    if (format_24h && clock_ampm) {
        lv_obj_add_flag(clock_ampm, LV_OBJ_FLAG_HIDDEN);
    }
    if (strcmp(hour, last_clock_hour) != 0) {
        static const int8_t outline_offsets[][2] = {
            {-2, -2}, {0, -2}, {2, -2}, {-2, 0},
            {2, 0}, {-2, 2}, {0, 2}, {2, 2},
        };
        for (uint8_t i = 0; i < 8; ++i) {
            lv_label_set_text(clock_outline[i], hour);
            // A 1-digit hour has a different auto-size than a 2-digit hour.
            // Re-align after the text change to keep both precisely centred.
            lv_obj_align(clock_outline[i], LV_ALIGN_CENTER,
                         outline_offsets[i][0], -39 + outline_offsets[i][1]);
        }
        lv_label_set_text(clock_hour_cutout, hour);
        lv_obj_align(clock_hour_cutout, LV_ALIGN_CENTER, 0, -39);
        strlcpy(last_clock_hour, hour, sizeof(last_clock_hour));
    }
    if (strcmp(minute, last_clock_minute) != 0) {
        lv_label_set_text(clock_minute, minute);
        strlcpy(last_clock_minute, minute, sizeof(last_clock_minute));
    }
    if (strcmp(ampm, last_clock_ampm) != 0) {
        lv_label_set_text(clock_ampm, ampm);
        strlcpy(last_clock_ampm, ampm, sizeof(last_clock_ampm));
    }
}

void update_brightness_label(uint8_t value) {
    if (!brightness_value) return;
    char text[8];
    snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(value));
    lv_label_set_text(brightness_value, text);
}

void on_brightness_change(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    int32_t value = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(event)));
    if (value < 10) value = 10;
    if (value > 100) value = 100;
    g_config.brightness_pct = static_cast<uint8_t>(value);
    backlight_apply_percent(g_config.brightness_pct);
    update_brightness_label(g_config.brightness_pct);
}

void on_brightness_commit(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        config_save(g_config);
        serial_echodesk_state_changed();
        Serial.printf("[EchoDesk] Brightness saved: %u%%\n", (unsigned)g_config.brightness_pct);
    }
}

void on_restart_cancel(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (restart_modal) {
        lv_obj_delete(restart_modal);
        restart_modal = nullptr;
    }
}

void on_restart_confirm(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || restart_at_ms != 0) return;
    restart_at_ms = millis() + 350;
    if (restart_modal) {
        lv_obj_clean(restart_modal);
        lv_obj_t *label = make_label(restart_modal, "RESTARTING...", &lv_font_montserrat_20, color(COLOR_WHITE));
        lv_obj_center(label);
    }
    Serial.println("[EchoDesk] Controlled restart requested from Settings");
}

void show_restart_dialog() {
    if (!settings_screen || restart_modal) return;
    restart_modal = lv_obj_create(settings_screen);
    lv_obj_set_size(restart_modal, 320, 240);
    lv_obj_set_pos(restart_modal, 0, 0);
    lv_obj_set_style_bg_color(restart_modal, color(COLOR_BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(restart_modal, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(restart_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(restart_modal, 0, LV_PART_MAIN);
    lv_obj_add_flag(restart_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(restart_modal);

    lv_obj_t *panel = lv_obj_create(restart_modal);
    lv_obj_set_size(panel, 280, 132);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, color(ECHO_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, color(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);

    lv_obj_t *question = make_label(panel, "RESTART ECHODESK?", &lv_font_montserrat_16, color(COLOR_WHITE));
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_t *cancel = lv_button_create(panel);
    style_button(cancel, 8);
    lv_obj_set_size(cancel, 112, 42);
    lv_obj_set_pos(cancel, 16, 72);
    lv_obj_t *cancel_label = make_label(cancel, "CANCEL", &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, on_restart_cancel, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *confirm = lv_button_create(panel);
    style_button(confirm, 8);
    lv_obj_set_size(confirm, 112, 42);
    lv_obj_set_pos(confirm, 152, 72);
    lv_obj_set_style_border_color(confirm, color(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_t *confirm_label = make_label(confirm, "RESTART", &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm, on_restart_confirm, LV_EVENT_CLICKED, nullptr);
}

void on_restart_pressed(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    show_restart_dialog();
}

void on_factory_reset_cancel(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (factory_reset_modal) {
        lv_obj_delete(factory_reset_modal);
        factory_reset_modal = nullptr;
    }
}

void on_factory_reset_confirm(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    config_factory_reset_setup(g_config);
    serial_echodesk_state_changed();
    ui_echodesk_setup_state_changed();
    Serial.println("[EchoDesk] Onboarding reset from Settings; preferences retained");
}

void show_factory_reset_dialog() {
    if (!settings_screen || factory_reset_modal) return;
    factory_reset_modal = lv_obj_create(settings_screen);
    lv_obj_set_size(factory_reset_modal, 320, 240);
    lv_obj_set_pos(factory_reset_modal, 0, 0);
    lv_obj_set_style_bg_color(factory_reset_modal, color(COLOR_BLACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(factory_reset_modal, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(factory_reset_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(factory_reset_modal, 0, LV_PART_MAIN);
    lv_obj_add_flag(factory_reset_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(factory_reset_modal);

    lv_obj_t *panel = lv_obj_create(factory_reset_modal);
    lv_obj_set_size(panel, 292, 162);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, color(ECHO_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, color(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);

    lv_obj_t *question = make_label(panel, "RESET SETUP?", &lv_font_montserrat_16,
                                    color(COLOR_WHITE));
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_t *detail = make_label(
        panel, "Returns to Hello.\nYour apps and preferences stay saved.",
        &lv_font_montserrat_12, color(COLOR_GREY));
    lv_obj_set_width(detail, 260);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *cancel = lv_button_create(panel);
    style_button(cancel, 8);
    lv_obj_set_size(cancel, 118, 42);
    lv_obj_set_pos(cancel, 14, 106);
    lv_obj_t *cancel_label = make_label(cancel, "CANCEL", &lv_font_montserrat_14,
                                        color(COLOR_WHITE));
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, on_factory_reset_cancel, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *confirm = lv_button_create(panel);
    style_button(confirm, 8);
    lv_obj_set_size(confirm, 132, 42);
    lv_obj_set_pos(confirm, 146, 106);
    lv_obj_set_style_border_color(confirm, color(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_t *confirm_label = make_label(confirm, "RESET SETUP",
                                         &lv_font_montserrat_12,
                                         color(COLOR_WHITE));
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm, on_factory_reset_confirm, LV_EVENT_CLICKED, nullptr);
}

void on_factory_reset_pressed(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    show_factory_reset_dialog();
}

void on_theme_pressed(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    g_config.theme = g_config.theme == THEME_LIGHT ? THEME_DARK : THEME_LIGHT;
    config_save(g_config);
    ui_apply_theme(g_config.theme);
    theme_rebuild_pending = true;
    serial_echodesk_state_changed();
    Serial.printf("[EchoDesk] Theme saved: %s\n",
                  g_config.theme == THEME_LIGHT ? "light" : "dark");
}

lv_obj_t *create_settings_screen() {
    restart_modal = nullptr;
    factory_reset_modal = nullptr;
    lv_obj_t *screen = lv_obj_create(nullptr);
    settings_screen = screen;
    add_delete_tracking(screen, &settings_screen);
    style_screen(screen);
    add_header(screen, "SETTINGS");

    lv_obj_t *brightness = make_label(screen, "BRIGHTNESS", &lv_font_montserrat_16, color(COLOR_WHITE));
    lv_obj_set_pos(brightness, 16, 51);
    brightness_value = make_label(screen, "80%", &lv_font_montserrat_16, color(COLOR_WHITE));
    lv_obj_align(brightness_value, LV_ALIGN_TOP_RIGHT, -16, 51);

    brightness_slider = lv_slider_create(screen);
    lv_obj_set_size(brightness_slider, 288, 18);
    lv_obj_set_pos(brightness_slider, 16, 78);
    lv_slider_set_range(brightness_slider, 10, 100);
    uint8_t initial = g_config.brightness_pct < 10 ? 10 : g_config.brightness_pct;
    if (initial != g_config.brightness_pct) {
        g_config.brightness_pct = initial;
        backlight_apply_percent(initial);
        config_save(g_config);
    }
    lv_slider_set_value(brightness_slider, initial, LV_ANIM_OFF);
    update_brightness_label(initial);
    lv_obj_set_ext_click_area(brightness_slider, 12);
    lv_obj_set_style_bg_color(brightness_slider, color(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, color(COLOR_WHITE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(brightness_slider, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, color(COLOR_WHITE), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, on_brightness_change, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(brightness_slider, on_brightness_commit, LV_EVENT_ALL, nullptr);

    lv_obj_t *theme = lv_button_create(screen);
    style_button(theme, 10);
    lv_obj_set_size(theme, 288, 46);
    lv_obj_set_pos(theme, 16, 113);
    lv_obj_t *theme_label = make_label(
        theme,
        g_config.theme == THEME_LIGHT ? "THEME     LIGHT" : "THEME     DARK",
        &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_center(theme_label);
    lv_obj_add_event_cb(theme, on_theme_pressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *restart = lv_button_create(screen);
    style_button(restart, 10);
    lv_obj_set_size(restart, 134, 50);
    lv_obj_set_pos(restart, 16, 174);
    lv_obj_t *restart_label = make_label(restart, "RESTART", &lv_font_montserrat_14, color(COLOR_WHITE));
    lv_obj_center(restart_label);
    lv_obj_add_event_cb(restart, on_restart_pressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *factory_reset = lv_button_create(screen);
    style_button(factory_reset, 10);
    lv_obj_set_size(factory_reset, 134, 50);
    lv_obj_set_pos(factory_reset, 170, 174);
    lv_obj_t *factory_reset_label = make_label(
        factory_reset, "FACTORY RESET", &lv_font_montserrat_12,
        color(COLOR_WHITE));
    lv_obj_center(factory_reset_label);
    lv_obj_add_event_cb(factory_reset, on_factory_reset_pressed,
                        LV_EVENT_CLICKED, nullptr);
    return screen;
}

void on_performance_home(void *) {
    show_app(ECHO_HOME);
}

void on_performance_back(void *) {
    show_app(ECHO_HOME);
}

void on_now_playing_home(void *) {
    show_app(ECHO_HOME);
}

void show_app(EchoApp app, bool animate) {
    if (app == current_app && lv_screen_active() != nullptr) return;
    if (current_app == ECHO_NOW_PLAYING && app != ECHO_NOW_PLAYING) {
        ui_now_playing_leave();
    }
    lv_obj_t *target = nullptr;
    switch (app) {
        case ECHO_HOME:
            if (!home_screen) create_home_screen();
            target = home_screen;
            break;
        case ECHO_CODEX:
            if (!codex_screen) create_codex_screen();
            target = codex_screen;
            update_codex();
            break;
        case ECHO_CLOCK:
            if (!clock_screen) create_clock_screen();
            target = clock_screen;
            update_clock();
            break;
        case ECHO_PERFORMANCE:
            ui_performance_apply_default_view();
            target = ui_performance_create();
            break;
        case ECHO_NOW_PLAYING:
            target = ui_now_playing_create();
            break;
        case ECHO_SETTINGS:
            if (!settings_screen) create_settings_screen();
            target = settings_screen;
            break;
    }

    bool delete_old = current_app != ECHO_HOME && app == ECHO_HOME;
    current_app = app;
    if (animate) {
        lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_FADE_IN, 140, 0, delete_old);
    } else {
        lv_obj_t *old = lv_screen_active();
        lv_screen_load(target);
        if (delete_old && old && old != target && old != home_screen) lv_obj_delete(old);
    }
    Serial.printf("[EchoDesk] App: %s\n",
                  app == ECHO_HOME ? "Home" : app == ECHO_CODEX ? "Codex" :
                  app == ECHO_CLOCK ? "Clock" : app == ECHO_PERFORMANCE ? "Performance" :
                  app == ECHO_NOW_PLAYING ? "Now Playing" :
                  "Settings");
    serial_echodesk_state_changed();
}

lv_obj_t *current_screen_pointer() {
    switch (current_app) {
        case ECHO_HOME: return home_screen;
        case ECHO_CODEX: return codex_screen;
        case ECHO_CLOCK: return clock_screen;
        case ECHO_PERFORMANCE: return ui_performance_screen();
        case ECHO_NOW_PLAYING: return ui_now_playing_screen();
        case ECHO_SETTINGS: return settings_screen;
    }
    return nullptr;
}

void clear_current_slot() {
    switch (current_app) {
        case ECHO_HOME: home_screen = nullptr; break;
        case ECHO_CODEX: codex_screen = nullptr; break;
        case ECHO_CLOCK: clock_screen = nullptr; break;
        case ECHO_PERFORMANCE: break;
        case ECHO_NOW_PLAYING: break;
        case ECHO_SETTINGS: settings_screen = nullptr; break;
    }
}

lv_obj_t *rebuild_current_screen() {
    switch (current_app) {
        case ECHO_HOME: return create_home_screen();
        case ECHO_CODEX: return create_codex_screen();
        case ECHO_CLOCK: return create_clock_screen();
        case ECHO_PERFORMANCE: return ui_performance_recreate();
        case ECHO_NOW_PLAYING: return ui_now_playing_recreate();
        case ECHO_SETTINGS: return create_settings_screen();
    }
    return create_home_screen();
}

} // namespace

void ui_echodesk_init() {
    serial_apply_configured_timezone();
    current_app = ECHO_HOME;
    ui_performance_set_navigation_callbacks(on_performance_home, on_performance_back);
    ui_now_playing_set_navigation_callback(on_now_playing_home);
    if (echodesk_setup_normalize_state(g_config.setup_state) !=
        ECHODESK_SETUP_COMPLETED) {
        load_setup_page(setup_page_for_state(g_config.setup_state));
        Serial.printf("[EchoDesk] Guided setup active: %s\n",
                      echodesk_setup_state_name(g_config.setup_state));
        return;
    }
    create_home_screen();
    EchoApp startup_app = ECHO_HOME;
    switch (g_config.default_app) {
        case ECHODESK_APP_CODEX: startup_app = ECHO_CODEX; break;
        case ECHODESK_APP_CLOCK: startup_app = ECHO_CLOCK; break;
        case ECHODESK_APP_PERFORMANCE: startup_app = ECHO_PERFORMANCE; break;
        case ECHODESK_APP_NOW_PLAYING: startup_app = ECHO_NOW_PLAYING; break;
        case ECHODESK_APP_SETTINGS: startup_app = ECHO_SETTINGS; break;
        default: break;
    }
    if (startup_app == ECHO_HOME) {
        lv_screen_load_anim(home_screen, LV_SCR_LOAD_ANIM_FADE_IN, 180, 0, true);
    } else {
        lv_obj_t *boot = lv_screen_active();
        show_app(startup_app, false);
        if (boot && boot != lv_screen_active()) lv_obj_delete(boot);
    }
    lv_refr_now(nullptr);
    Serial.println("[EchoDesk] OS 1.0 shell active (320x240 landscape)");
}

void ui_echodesk_tick() {
    unsigned long now = millis();
    if (setup_state_change_pending) {
        setup_state_change_pending = false;
        if (echodesk_setup_normalize_state(g_config.setup_state) ==
            ECHODESK_SETUP_COMPLETED) {
            lv_obj_t *old = lv_screen_active();
            current_app = ECHO_HOME;
            if (!home_screen) create_home_screen();
            lv_screen_load_anim(home_screen, LV_SCR_LOAD_ANIM_FADE_IN, 180, 0,
                                old && old != home_screen);
            setup_screen = nullptr;
            lv_refr_now(nullptr);
            Serial.println("[EchoDesk] Guided setup completed; Home active");
        } else {
            load_setup_page(setup_page_for_state(g_config.setup_state));
        }
    }

    if (echodesk_setup_normalize_state(g_config.setup_state) !=
        ECHODESK_SETUP_COMPLETED) {
        if (setup_page == SetupPage::HELLO && setup_hello_reveal_at != 0 &&
            static_cast<long>(now - setup_hello_reveal_at) >= 0) {
            setup_hello_reveal_at = 0;
            if (setup_hello_prompt) {
                lv_obj_clear_flag(setup_hello_prompt, LV_OBJ_FLAG_HIDDEN);
            }
            if (setup_hello_button) {
                lv_obj_clear_flag(setup_hello_button, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (setup_page == SetupPage::WIFI && setup_wifi_status) {
            const char *text = "Temporary setup network is starting...";
            bool retry = false;
            switch (echodesk_wifi_setup_status()) {
                case EchoDeskWifiSetupStatus::PROVISIONING:
                    text = "Waiting securely for EchoDesk Desktop...";
                    break;
                case EchoDeskWifiSetupStatus::CONNECTING:
                    text = "Wi-Fi saved. Connecting to your network...";
                    break;
                case EchoDeskWifiSetupStatus::CONNECTED:
                    text = "Connected. EchoDesk Desktop can now find this device.";
                    break;
                case EchoDeskWifiSetupStatus::EXPIRED:
                    text = "Setup network expired after 15 minutes.";
                    retry = true;
                    break;
                case EchoDeskWifiSetupStatus::ERROR:
                    text = "Could not start Wi-Fi setup.";
                    retry = true;
                    break;
                default:
                    break;
            }
            lv_label_set_text(setup_wifi_status, text);
            if (setup_wifi_retry) {
                if (retry) lv_obj_clear_flag(setup_wifi_retry, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(setup_wifi_retry, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (restart_at_ms != 0 && static_cast<long>(now - restart_at_ms) >= 0) {
            Serial.flush();
            ESP.restart();
        }
        return;
    }

    if (remote_app_pending) {
        remote_app_pending = false;
        show_app(pending_remote_app);
    }
    bool new_data = serial_has_new_data();
    if (current_app == ECHO_CODEX && (new_data || now - last_refresh_ms >= UI_REFRESH_MS)) {
        update_codex();
        last_refresh_ms = now;
    } else if (current_app == ECHO_CLOCK && now - last_refresh_ms >= UI_REFRESH_MS) {
        update_clock();
        last_refresh_ms = now;
    } else if (current_app == ECHO_PERFORMANCE) {
        ui_performance_tick();
    } else if (current_app == ECHO_NOW_PLAYING) {
        ui_now_playing_tick();
    } else if (current_app == ECHO_SETTINGS && brightness_slider &&
               !lv_obj_has_state(brightness_slider, LV_STATE_PRESSED)) {
        uint8_t value = g_config.brightness_pct < 10 ? 10 : g_config.brightness_pct;
        if (lv_slider_get_value(brightness_slider) != value) {
            lv_slider_set_value(brightness_slider, value, LV_ANIM_OFF);
            update_brightness_label(value);
        }
    }

    if (restart_at_ms != 0 && static_cast<long>(now - restart_at_ms) >= 0) {
        Serial.flush();
        ESP.restart();
    }
    if (theme_rebuild_pending) {
        theme_rebuild_pending = false;
        ui_echodesk_recreate();
    }
}

void ui_echodesk_recreate() {
    if (echodesk_setup_normalize_state(g_config.setup_state) !=
        ECHODESK_SETUP_COMPLETED) {
        load_setup_page(setup_page_for_state(g_config.setup_state), false);
        lv_refr_now(nullptr);
        Serial.println("[EchoDesk] Guided setup screen recreated");
        return;
    }
    if (current_app != ECHO_HOME && home_screen) {
        lv_obj_t *old_home = home_screen;
        home_screen = nullptr;
        create_home_screen();
        lv_obj_delete(old_home);
    }
    if (current_app == ECHO_PERFORMANCE) {
        ui_performance_recreate();
        lv_refr_now(nullptr);
        Serial.println("[EchoDesk] Performance app recreated after display/config change");
        return;
    }
    if (current_app == ECHO_NOW_PLAYING) {
        ui_now_playing_recreate();
        lv_refr_now(nullptr);
        Serial.println("[EchoDesk] Now Playing app recreated after display/config change");
        return;
    }
    lv_obj_t *old = current_screen_pointer();
    EchoApp rebuilding = current_app;
    clear_current_slot();
    lv_obj_t *replacement = rebuild_current_screen();
    lv_screen_load(replacement);
    if (old && old != replacement) lv_obj_delete(old);
    current_app = rebuilding;
    if (current_app == ECHO_CODEX) update_codex();
    if (current_app == ECHO_CLOCK) update_clock();
    lv_refr_now(nullptr);
    Serial.println("[EchoDesk] Current app recreated after display/config change");
}

bool ui_echodesk_request_open_app(const char *app) {
    if (!app || echodesk_setup_normalize_state(g_config.setup_state) !=
                    ECHODESK_SETUP_COMPLETED) return false;
    if (strcmp(app, "home") == 0) pending_remote_app = ECHO_HOME;
    else if (strcmp(app, "codex") == 0) pending_remote_app = ECHO_CODEX;
    else if (strcmp(app, "clock") == 0) pending_remote_app = ECHO_CLOCK;
    else if (strcmp(app, "performance") == 0) pending_remote_app = ECHO_PERFORMANCE;
    else if (strcmp(app, "nowPlaying") == 0) pending_remote_app = ECHO_NOW_PLAYING;
    else if (strcmp(app, "settings") == 0) pending_remote_app = ECHO_SETTINGS;
    else return false;
    remote_app_pending = true;
    return true;
}

const char *ui_echodesk_active_app_name() {
    switch (current_app) {
        case ECHO_CODEX: return "codex";
        case ECHO_CLOCK: return "clock";
        case ECHO_PERFORMANCE: return "performance";
        case ECHO_NOW_PLAYING: return "nowPlaying";
        case ECHO_SETTINGS: return "settings";
        default: return "home";
    }
}

void ui_echodesk_request_restart() {
    if (restart_at_ms == 0) restart_at_ms = millis() + 350;
}

void ui_echodesk_setup_state_changed() {
    setup_state_change_pending = true;
}

#else

void ui_echodesk_init() {}
void ui_echodesk_tick() {}
void ui_echodesk_recreate() {}
bool ui_echodesk_request_open_app(const char *) { return false; }
const char *ui_echodesk_active_app_name() { return "unsupported"; }
void ui_echodesk_request_restart() {}
void ui_echodesk_setup_state_changed() {}

#endif
