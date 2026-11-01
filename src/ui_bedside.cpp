#include "ui_bedside.h"

#if defined(ECHODESK_OS)

#include "bedside_mode.h"
#include "config_store.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {

constexpr time_t VALID_CLOCK_EPOCH = 1700000000;
constexpr uint32_t REFRESH_INTERVAL_MS = 250;

lv_obj_t *screen = nullptr;
lv_obj_t *clock_label = nullptr;
lv_obj_t *date_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *alarm_card = nullptr;
lv_obj_t *alarm_value = nullptr;
lv_obj_t *alarm_note = nullptr;
lv_obj_t *weather_card = nullptr;
lv_obj_t *tap_layer = nullptr;
lv_obj_t *controls_overlay = nullptr;
lv_obj_t *alarm_overlay = nullptr;
lv_obj_t *alarm_title = nullptr;
lv_obj_t *cancel_button = nullptr;
UiBedsideNavigationCallback home_callback = nullptr;
void *navigation_context = nullptr;
uint32_t last_refresh_ms = 0;
uint32_t alarm_started_ms = 0;
uint8_t applied_brightness = 0xFF;
bool previous_alarm_ringing = false;
bool rendered_dark = false;
bool rendered_night_shift = false;
bool colors_initialized = false;

lv_color_t background(bool dark, bool warm) {
    if (dark) return warm ? lv_color_hex(0x120A03) : lv_color_hex(0x000000);
    return warm ? lv_color_hex(0xFFF0D2) : lv_color_hex(0xF7F8FA);
}

lv_color_t foreground(bool dark, bool warm) {
    if (dark) return warm ? lv_color_hex(0xFFD29A) : lv_color_hex(0xFFFFFF);
    return warm ? lv_color_hex(0x492600) : lv_color_hex(0x111216);
}

lv_color_t muted(bool dark, bool warm) {
    if (dark) return warm ? lv_color_hex(0xB27B48) : lv_color_hex(0x9CA3AF);
    return warm ? lv_color_hex(0x89613A) : lv_color_hex(0x606773);
}

lv_color_t panel(bool dark, bool warm) {
    if (dark) return warm ? lv_color_hex(0x241308) : lv_color_hex(0x111318);
    return warm ? lv_color_hex(0xF3DDB7) : lv_color_hex(0xE9ECF1);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

void apply_backlight(uint8_t brightness) {
    if (brightness == applied_brightness) return;
    applied_brightness = brightness;
    backlight_apply_percent(brightness);
}

void style_button(lv_obj_t *button, bool dark, bool warm) {
    lv_obj_set_style_bg_color(button, panel(dark, warm), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, muted(dark, warm), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, foreground(dark, warm), LV_PART_MAIN);
}

void apply_colors(bool dark, bool warm) {
    rendered_dark = dark;
    rendered_night_shift = warm;
    colors_initialized = true;
    const lv_color_t bg = background(dark, warm);
    const lv_color_t fg = foreground(dark, warm);
    const lv_color_t dim = muted(dark, warm);
    lv_obj_set_style_bg_color(screen, bg, LV_PART_MAIN);
    lv_obj_t *primary_labels[] = {clock_label, alarm_value};
    for (lv_obj_t *label : primary_labels) {
        if (label) lv_obj_set_style_text_color(label, fg, LV_PART_MAIN);
    }
    lv_obj_t *muted_labels[] = {date_label, status_label, alarm_note};
    for (lv_obj_t *label : muted_labels) {
        if (label) lv_obj_set_style_text_color(label, dim, LV_PART_MAIN);
    }
    lv_obj_t *cards[] = {alarm_card, weather_card};
    for (lv_obj_t *card : cards) {
        if (!card) continue;
        lv_obj_set_style_bg_color(card, panel(dark, warm), LV_PART_MAIN);
        lv_obj_set_style_border_color(card, muted(dark, warm), LV_PART_MAIN);
        lv_obj_set_style_text_color(card, fg, LV_PART_MAIN);
    }
    if (controls_overlay) {
        lv_obj_set_style_bg_color(controls_overlay, bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(controls_overlay, LV_OPA_90, LV_PART_MAIN);
        uint32_t count = lv_obj_get_child_count(controls_overlay);
        for (uint32_t i = 0; i < count; ++i) {
            lv_obj_t *child = lv_obj_get_child(controls_overlay, i);
            if (lv_obj_check_type(child, &lv_button_class)) style_button(child, dark, warm);
        }
    }
}

BedsideLocalTime read_local_time(struct tm *local_out = nullptr) {
    BedsideLocalTime result{};
    const time_t epoch = time(nullptr);
    if (epoch < VALID_CLOCK_EPOCH) return result;
    struct tm local{};
    if (!localtime_r(&epoch, &local)) return result;
    result.valid = true;
    result.epoch = static_cast<uint64_t>(epoch);
    result.date_key = (local.tm_year + 1900) * 10000 +
                      (local.tm_mon + 1) * 100 + local.tm_mday;
    result.minute_of_day = local.tm_hour * 60 + local.tm_min;
    result.second = local.tm_sec;
    result.weekday = local.tm_wday;
    if (local_out) *local_out = local;
    return result;
}

int32_t tomorrow_date_key(const struct tm &today) {
    struct tm tomorrow = today;
    tomorrow.tm_mday += 1;
    tomorrow.tm_isdst = -1;
    if (mktime(&tomorrow) < VALID_CLOCK_EPOCH) return 0;
    return (tomorrow.tm_year + 1900) * 10000 +
           (tomorrow.tm_mon + 1) * 100 + tomorrow.tm_mday;
}

void hide_controls() {
    if (controls_overlay) lv_obj_add_flag(controls_overlay, LV_OBJ_FLAG_HIDDEN);
    if (tap_layer) lv_obj_clear_flag(tap_layer, LV_OBJ_FLAG_HIDDEN);
}

void show_controls() {
    if (tap_layer) lv_obj_add_flag(tap_layer, LV_OBJ_FLAG_HIDDEN);
    if (controls_overlay) lv_obj_clear_flag(controls_overlay, LV_OBJ_FLAG_HIDDEN);
}

void on_tap(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (bedside_runtime().display_sleeping) {
        bedside_set_display_sleeping(false);
        applied_brightness = 0xFF;
        hide_controls();
        return;
    }
    show_controls();
}

void on_overlay_background(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_event_get_target(event) == controls_overlay) {
        hide_controls();
    }
}

void on_cancel_tomorrow(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    struct tm local{};
    const BedsideLocalTime now = read_local_time(&local);
    if (now.valid) bedside_cancel_tomorrow(tomorrow_date_key(local));
    hide_controls();
}

void on_display_off(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    bedside_set_display_sleeping(true);
    hide_controls();
    apply_backlight(0);
}

void on_exit(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ui_bedside_leave();
    if (home_callback) home_callback(navigation_context);
}

void on_stop_alarm(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    bedside_stop_alarm();
    previous_alarm_ringing = false;
    alarm_started_ms = 0;
    if (alarm_overlay) lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
    applied_brightness = 0xFF;
}

void on_screen_deleted(lv_event_t *event) {
    if (lv_event_get_target(event) != screen) return;
    screen = nullptr;
    clock_label = date_label = status_label = nullptr;
    alarm_card = alarm_value = alarm_note = weather_card = nullptr;
    tap_layer = controls_overlay = alarm_overlay = alarm_title = cancel_button = nullptr;
    colors_initialized = false;
}

lv_obj_t *create_action_button(lv_obj_t *parent, int16_t x, int16_t width,
                               const char *title, lv_event_cb_t callback) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 58);
    lv_obj_set_pos(button, x, 164);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    style_button(button, rendered_dark, rendered_night_shift);
    lv_obj_t *label = make_label(button, title, &lv_font_montserrat_12);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    return button;
}

void layout_widgets(const BedsideConfig &config) {
    const uint8_t count = static_cast<uint8_t>(config.widget_next_alarm) +
                          static_cast<uint8_t>(config.widget_weather);
    if (alarm_card) {
        if (!config.widget_next_alarm) lv_obj_add_flag(alarm_card, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_clear_flag(alarm_card, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(alarm_card, count == 1 ? 8 : 8, 133);
            lv_obj_set_width(alarm_card, count == 1 ? 304 : 148);
            if (alarm_note) lv_obj_set_width(alarm_note, count == 1 ? 284 : 128);
        }
    }
    if (weather_card) {
        if (!config.widget_weather) lv_obj_add_flag(weather_card, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_clear_flag(weather_card, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(weather_card, count == 1 ? 8 : 164, 133);
            lv_obj_set_width(weather_card, count == 1 ? 304 : 148);
        }
    }
}

void update_text(const BedsideLocalTime &now, const struct tm &local) {
    const BedsideConfig &config = bedside_get_config();
    lv_obj_set_style_text_font(clock_label,
                               config.clock_style == BedsideClockStyle::LARGE
                                   ? &lv_font_montserrat_48
                                   : &lv_font_montserrat_36,
                               LV_PART_MAIN);
    if (!now.valid) {
        lv_label_set_text(clock_label, "--:--");
        lv_label_set_text(date_label, "CONNECTING TO WI-FI TIME");
        lv_label_set_text(status_label, "Cold boot needs saved Wi-Fi and NTP");
        lv_label_set_text(alarm_value, "TIME NEEDED");
        lv_label_set_text(alarm_note, "Alarms resume after clock sync");
        return;
    }

    char clock[16];
    int hour = local.tm_hour;
    const char *suffix = "";
    if (config.clock_format == BedsideClockFormat::HOUR_12) {
        suffix = local.tm_hour >= 12 ? " PM" : " AM";
        hour %= 12;
        if (hour == 0) hour = 12;
    }
    if (config.show_seconds) {
        snprintf(clock, sizeof(clock), "%d:%02d:%02d%s", hour, local.tm_min,
                 local.tm_sec, suffix);
    } else {
        snprintf(clock, sizeof(clock), "%d:%02d%s", hour, local.tm_min, suffix);
    }
    lv_label_set_text(clock_label, clock);
    static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char date[32];
    snprintf(date, sizeof(date), "%s  %d %s", DAYS[local.tm_wday],
             local.tm_mday, MONTHS[local.tm_mon]);
    lv_label_set_text(date_label, date);
    lv_label_set_text(status_label, "Tap anywhere for controls");

    const BedsideNextAlarm &next = bedside_runtime().next_alarm;
    if (!next.valid) {
        lv_label_set_text(alarm_value, "NO ALARMS");
        lv_label_set_text(alarm_note, "Set one in EchoDesk Desktop");
    } else {
        int next_hour = next.minute_of_day / 60;
        const int next_minute = next.minute_of_day % 60;
        const char *next_suffix = "";
        if (config.clock_format == BedsideClockFormat::HOUR_12) {
            next_suffix = next_hour >= 12 ? " PM" : " AM";
            next_hour %= 12;
            if (next_hour == 0) next_hour = 12;
        }
        char value[24];
        snprintf(value, sizeof(value), "%d:%02d%s", next_hour, next_minute,
                 next_suffix);
        lv_label_set_text(alarm_value, value);
        char note[48];
        const char *day = next.days_away == 0 ? "Today" :
                          next.days_away == 1 ? "Tomorrow" : "Upcoming";
        snprintf(note, sizeof(note), "%s%s%s", day,
                 next.label[0] ? "  ·  " : "", next.label);
        lv_label_set_text(alarm_note, note);
    }
}

void update_alarm_overlay(uint32_t now_ms) {
    const BedsideRuntime &runtime = bedside_runtime();
    if (runtime.alarm_ringing && !previous_alarm_ringing) {
        alarm_started_ms = now_ms;
        previous_alarm_ringing = true;
        bedside_set_display_sleeping(false);
        lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
        char title[48] = "ALARM";
        for (uint8_t i = 0; i < bedside_get_config().alarm_count; ++i) {
            const BedsideAlarm &alarm = bedside_get_config().alarms[i];
            if (alarm.id == runtime.ringing_alarm_id && alarm.label[0]) {
                snprintf(title, sizeof(title), "ALARM\n%s", alarm.label);
                break;
            }
        }
        lv_label_set_text(alarm_title, title);
    }
    if (!runtime.alarm_ringing) {
        previous_alarm_ringing = false;
        if (alarm_overlay) lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const uint32_t elapsed = now_ms - alarm_started_ms;
    uint8_t brightness = 60;
    if (elapsed >= 120 && elapsed < 240) brightness = 100;
    else if (elapsed >= 360 && elapsed < 520) brightness = 100;
    apply_backlight(brightness);
}

} // namespace

void ui_bedside_set_navigation_callback(UiBedsideNavigationCallback callback,
                                        void *context) {
    home_callback = callback;
    navigation_context = context;
}

lv_obj_t *ui_bedside_create() {
    if (screen) return screen;
    screen = lv_obj_create(nullptr);
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, nullptr);

    clock_label = make_label(screen, "--:--", &lv_font_montserrat_48);
    lv_obj_set_width(clock_label, 304);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 18);
    date_label = make_label(screen, "CONNECTING TO WI-FI TIME", &lv_font_montserrat_14);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 79);
    status_label = make_label(screen, "Cold boot needs saved Wi-Fi and NTP",
                              &lv_font_montserrat_12);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 104);

    alarm_card = lv_obj_create(screen);
    lv_obj_set_size(alarm_card, 148, 96);
    lv_obj_set_style_radius(alarm_card, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(alarm_card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(alarm_card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(alarm_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *alarm_header = make_label(alarm_card, "NEXT ALARM", &lv_font_montserrat_12);
    lv_obj_align(alarm_header, LV_ALIGN_TOP_LEFT, 0, 0);
    alarm_value = make_label(alarm_card, "TIME NEEDED", &lv_font_montserrat_20);
    lv_obj_align(alarm_value, LV_ALIGN_TOP_LEFT, 0, 25);
    alarm_note = make_label(alarm_card, "Alarms resume after sync", &lv_font_montserrat_12);
    lv_obj_set_width(alarm_note, 280);
    lv_label_set_long_mode(alarm_note, LV_LABEL_LONG_DOT);
    lv_obj_align(alarm_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    weather_card = lv_obj_create(screen);
    lv_obj_set_size(weather_card, 148, 96);
    lv_obj_set_style_radius(weather_card, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(weather_card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(weather_card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(weather_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *weather_header = make_label(weather_card, "WEATHER", &lv_font_montserrat_12);
    lv_obj_align(weather_header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *weather_value = make_label(weather_card, "--°", &lv_font_montserrat_24);
    lv_obj_align(weather_value, LV_ALIGN_TOP_LEFT, 0, 23);
    lv_obj_t *weather_note = make_label(weather_card, "COMING SOON", &lv_font_montserrat_12);
    lv_obj_align(weather_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    tap_layer = lv_obj_create(screen);
    lv_obj_remove_style_all(tap_layer);
    lv_obj_set_size(tap_layer, 320, 240);
    lv_obj_add_flag(tap_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap_layer, on_tap, LV_EVENT_CLICKED, nullptr);

    controls_overlay = lv_obj_create(screen);
    lv_obj_set_size(controls_overlay, 320, 240);
    lv_obj_set_style_border_width(controls_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(controls_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(controls_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(controls_overlay, on_overlay_background, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *controls_title = make_label(controls_overlay, "BEDSIDE CONTROLS",
                                          &lv_font_montserrat_20);
    lv_obj_align(controls_title, LV_ALIGN_TOP_MID, 0, 27);
    lv_obj_t *controls_note = make_label(controls_overlay,
                                        "Ambient settings stay on this device",
                                        &lv_font_montserrat_12);
    lv_obj_align(controls_note, LV_ALIGN_TOP_MID, 0, 61);
    cancel_button = create_action_button(controls_overlay, 8, 98,
                                         "CANCEL\nTOMORROW", on_cancel_tomorrow);
    create_action_button(controls_overlay, 111, 98, "DISPLAY\nOFF", on_display_off);
    create_action_button(controls_overlay, 214, 98, "EXIT\nBEDSIDE", on_exit);
    lv_obj_add_flag(controls_overlay, LV_OBJ_FLAG_HIDDEN);

    alarm_overlay = lv_obj_create(screen);
    lv_obj_set_size(alarm_overlay, 320, 240);
    lv_obj_set_style_bg_color(alarm_overlay, lv_color_hex(0x140300), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alarm_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(alarm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(alarm_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    alarm_title = make_label(alarm_overlay, "ALARM", &lv_font_montserrat_36);
    lv_obj_set_width(alarm_title, 304);
    lv_obj_set_style_text_align(alarm_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(alarm_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(alarm_title, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_t *stop = lv_button_create(alarm_overlay);
    lv_obj_set_size(stop, 248, 64);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_bg_color(stop, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(stop, 16, LV_PART_MAIN);
    lv_obj_t *stop_label = make_label(stop, "STOP ALARM", &lv_font_montserrat_20);
    lv_obj_set_style_text_color(stop_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop, on_stop_alarm, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);

    layout_widgets(bedside_get_config());
    apply_colors(false, false);
    ui_bedside_tick();
    return screen;
}

lv_obj_t *ui_bedside_screen() { return screen; }

lv_obj_t *ui_bedside_recreate() {
    lv_obj_t *old = screen;
    screen = nullptr;
    clock_label = date_label = status_label = nullptr;
    alarm_card = alarm_value = alarm_note = weather_card = nullptr;
    tap_layer = controls_overlay = alarm_overlay = alarm_title = cancel_button = nullptr;
    colors_initialized = false;
    lv_obj_t *replacement = ui_bedside_create();
    if (old) {
        lv_screen_load(replacement);
        lv_obj_delete(old);
    }
    return replacement;
}

void ui_bedside_tick() {
    if (!screen) return;
    const uint32_t now_ms = millis();
    // Once ringing, drive the short 60/100/60/100 wake flash at loop cadence;
    // the rest of the ambient UI stays on its cheaper 250 ms cadence.
    if (bedside_runtime().alarm_ringing) update_alarm_overlay(now_ms);
    if (now_ms - last_refresh_ms < REFRESH_INTERVAL_MS) return;
    last_refresh_ms = now_ms;

    struct tm local{};
    const BedsideLocalTime now = read_local_time(&local);
    bedside_tick(now);
    const bool dark = now.valid ? bedside_dark_theme_active(now.minute_of_day) : true;
    const bool warm = now.valid ? bedside_night_shift_active(now.minute_of_day) : false;
    if (!colors_initialized || dark != rendered_dark || warm != rendered_night_shift) {
        apply_colors(dark, warm);
    }
    layout_widgets(bedside_get_config());
    update_text(now, local);

    const BedsideRuntime &runtime = bedside_runtime();
    if (cancel_button) {
        if (!now.valid || !runtime.next_alarm.valid) lv_obj_add_state(cancel_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(cancel_button, LV_STATE_DISABLED);
    }
    update_alarm_overlay(now_ms);
    if (runtime.alarm_ringing) return;
    if (runtime.display_sleeping) {
        apply_backlight(0);
    } else {
        apply_backlight(now.valid ? bedside_brightness_for_minute(now.minute_of_day)
                                  : bedside_get_config().manual_brightness);
    }
}

void ui_bedside_leave() {
    bedside_set_display_sleeping(false);
    bedside_stop_alarm();
    previous_alarm_ringing = false;
    alarm_started_ms = 0;
    apply_backlight(g_config.brightness_pct);
}

#else
void ui_bedside_set_navigation_callback(UiBedsideNavigationCallback, void *) {}
lv_obj_t *ui_bedside_create() { return nullptr; }
lv_obj_t *ui_bedside_screen() { return nullptr; }
lv_obj_t *ui_bedside_recreate() { return nullptr; }
void ui_bedside_tick() {}
void ui_bedside_leave() {}
#endif
