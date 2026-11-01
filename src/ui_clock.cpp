#include "ui_clock.h"

#if defined(ECHODESK_OS)

#include "config_store.h"

#include <Arduino.h>
#include <stdio.h>
#include <time.h>

namespace {

enum class ClockView : uint8_t {
    SELECTOR = 0,
    STOPWATCH,
    COUNTDOWN,
    FOCUS,
    COUNTDOWN_EDITOR,
    FOCUS_EDITOR,
    REMOTE_WAIT,
};

constexpr uint32_t REFRESH_INTERVAL_MS = 100;
constexpr uint32_t NAVIGATION_GUARD_MS = 250;

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *heading_label = nullptr;
lv_obj_t *back_button = nullptr;
lv_obj_t *primary_value = nullptr;
lv_obj_t *secondary_value = nullptr;
lv_obj_t *status_value = nullptr;
lv_obj_t *ring = nullptr;
lv_obj_t *primary_button_label = nullptr;
lv_obj_t *selector_time = nullptr;
lv_obj_t *selector_date = nullptr;
lv_obj_t *countdown_editor_value = nullptr;
lv_obj_t *focus_editor_values[3] = {};

ClockView current_view = ClockView::SELECTOR;
EchoDeskClockMode remote_wait_mode = EchoDeskClockMode::COUNTDOWN;
uint32_t remote_wait_revision = 0;
uint32_t known_revision = 0;
uint16_t editor_countdown_minutes = 10;
uint16_t editor_focus_minutes = 25;
uint16_t editor_break_minutes = 5;
uint8_t editor_rounds = 4;
uint32_t last_refresh_ms = 0;
uint32_t last_navigation_ms = 0;

UiClockNavigationCallback navigate_home = nullptr;
UiClockConfigurationRequestCallback request_configuration = nullptr;
UiClockNavigationCallback state_changed = nullptr;
void *callback_context = nullptr;

lv_color_t background_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xFFFFFF)
                                         : lv_color_hex(0x000000);
}

lv_color_t foreground_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x111111)
                                         : lv_color_hex(0xFFFFFF);
}

lv_color_t panel_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xF2F3F5)
                                         : lv_color_hex(0x111214);
}

lv_color_t pressed_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xDADDE1)
                                         : lv_color_hex(0x292B2F);
}

lv_color_t muted_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x5E636B)
                                         : lv_color_hex(0xA8ADB5);
}

lv_color_t divider_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xD1D5DB)
                                         : lv_color_hex(0x34373C);
}

lv_color_t accent_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x2563EB)
                                         : lv_color_hex(0x5B8CFF);
}

lv_color_t complete_color() { return lv_color_hex(0x20B26B); }

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    return label;
}

void style_button(lv_obj_t *button, uint8_t radius = 9) {
    lv_obj_set_style_bg_color(button, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, pressed_color(),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, foreground_color(),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, int16_t x, int16_t y,
                      int16_t width, int16_t height, lv_event_cb_t callback,
                      void *user_data = nullptr) {
    lv_obj_t *button = lv_button_create(parent);
    style_button(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_12,
                                 foreground_color());
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

bool navigation_allowed() {
    const uint32_t now = millis();
    if (now - last_navigation_ms < NAVIGATION_GUARD_MS) return false;
    last_navigation_ms = now;
    return true;
}

void clear_view_references() {
    primary_value = nullptr;
    secondary_value = nullptr;
    status_value = nullptr;
    ring = nullptr;
    primary_button_label = nullptr;
    selector_time = nullptr;
    selector_date = nullptr;
    countdown_editor_value = nullptr;
    for (auto &value : focus_editor_values) value = nullptr;
}

void on_screen_deleted(lv_event_t *event) {
    if (lv_event_get_target(event) != screen) return;
    screen = nullptr;
    content = nullptr;
    heading_label = nullptr;
    back_button = nullptr;
    clear_view_references();
}

void render_view();
void refresh_view(const EchoDeskClockSnapshot &snapshot);

void invoke_home(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (navigate_home) navigate_home(callback_context);
}

void invoke_back(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (current_view == ClockView::SELECTOR) {
        if (navigate_home) navigate_home(callback_context);
        return;
    }
    current_view = ClockView::SELECTOR;
    render_view();
}

void create_header() {
    make_button(screen, "HOME", 6, 5, 62, 30, invoke_home);
    back_button = make_button(screen, "BACK", 252, 5, 62, 30, invoke_back);
    heading_label = make_label(screen, "CLOCK", &lv_font_montserrat_14,
                               foreground_color());
    lv_obj_set_width(heading_label, 168);
    lv_obj_set_style_text_align(heading_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(heading_label, 76, 12);

    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 304, 1);
    lv_obj_set_pos(divider, 8, 39);
    lv_obj_set_style_bg_color(divider, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
}

void notify_state_changed() {
    if (state_changed) state_changed(callback_context);
}

bool apply(EchoDeskClockAction action, EchoDeskClockMode mode,
           uint32_t duration_seconds = 0, uint16_t focus_minutes = 0,
           uint16_t break_minutes = 0, uint8_t rounds = 0) {
    EchoDeskClockCommand command{};
    command.action = action;
    command.mode = mode;
    command.duration_seconds = duration_seconds;
    command.focus_minutes = focus_minutes;
    command.break_minutes = break_minutes;
    command.rounds = rounds;
    if (echodesk_clock_apply(command, millis()) != EchoDeskClockResult::OK) {
        return false;
    }
    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_capture(millis(), snapshot);
    known_revision = snapshot.revision;
    notify_state_changed();
    return true;
}

ClockView view_for_mode(EchoDeskClockMode mode) {
    switch (mode) {
        case EchoDeskClockMode::STOPWATCH: return ClockView::STOPWATCH;
        case EchoDeskClockMode::COUNTDOWN: return ClockView::COUNTDOWN;
        case EchoDeskClockMode::FOCUS: return ClockView::FOCUS;
        default: return ClockView::SELECTOR;
    }
}

void open_mode(EchoDeskClockMode mode) {
    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_capture(millis(), snapshot);

    if (mode == EchoDeskClockMode::STOPWATCH) {
        apply(EchoDeskClockAction::SELECT, mode);
        current_view = ClockView::STOPWATCH;
        render_view();
        return;
    }

    const EchoDeskClockStatus status = mode == EchoDeskClockMode::COUNTDOWN
        ? snapshot.countdown.status : snapshot.focus.status;
    if (status != EchoDeskClockStatus::IDLE) {
        apply(EchoDeskClockAction::SELECT, mode);
        current_view = view_for_mode(mode);
        render_view();
        return;
    }

    if (request_configuration && request_configuration(mode, callback_context)) {
        remote_wait_mode = mode;
        remote_wait_revision = snapshot.revision;
        current_view = ClockView::REMOTE_WAIT;
    } else if (mode == EchoDeskClockMode::COUNTDOWN) {
        editor_countdown_minutes = static_cast<uint16_t>(
            (snapshot.countdown.duration_seconds + 59U) / 60U);
        if (editor_countdown_minutes < 1) editor_countdown_minutes = 1;
        current_view = ClockView::COUNTDOWN_EDITOR;
    } else {
        editor_focus_minutes = snapshot.focus.focus_minutes;
        editor_break_minutes = snapshot.focus.break_minutes;
        editor_rounds = snapshot.focus.rounds;
        current_view = ClockView::FOCUS_EDITOR;
    }
    render_view();
}

void on_selector_mode(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    const auto mode = static_cast<EchoDeskClockMode>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    open_mode(mode);
}

void on_timer_primary(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_capture(millis(), snapshot);
    const EchoDeskClockMode mode = current_view == ClockView::STOPWATCH
        ? EchoDeskClockMode::STOPWATCH
        : current_view == ClockView::COUNTDOWN ? EchoDeskClockMode::COUNTDOWN
                                               : EchoDeskClockMode::FOCUS;
    const EchoDeskClockStatus status = mode == EchoDeskClockMode::STOPWATCH
        ? snapshot.stopwatch.status
        : mode == EchoDeskClockMode::COUNTDOWN ? snapshot.countdown.status
                                               : snapshot.focus.status;
    apply(status == EchoDeskClockStatus::RUNNING ? EchoDeskClockAction::PAUSE
                                                  : EchoDeskClockAction::START,
          mode);
    render_view();
}

void on_timer_reset(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    const EchoDeskClockMode mode = current_view == ClockView::STOPWATCH
        ? EchoDeskClockMode::STOPWATCH
        : current_view == ClockView::COUNTDOWN ? EchoDeskClockMode::COUNTDOWN
                                               : EchoDeskClockMode::FOCUS;
    apply(EchoDeskClockAction::RESET, mode);
    render_view();
}

void on_focus_skip(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    apply(EchoDeskClockAction::SKIP, EchoDeskClockMode::FOCUS);
    render_view();
}

void update_countdown_editor_label() {
    if (!countdown_editor_value) return;
    char text[16];
    const uint16_t hours = editor_countdown_minutes / 60;
    const uint16_t minutes = editor_countdown_minutes % 60;
    if (hours > 0) snprintf(text, sizeof(text), "%uh %02um", hours, minutes);
    else snprintf(text, sizeof(text), "%u min", minutes);
    lv_label_set_text(countdown_editor_value, text);
}

void on_countdown_adjust(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    const int delta = static_cast<int>(reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event)));
    int next = static_cast<int>(editor_countdown_minutes) + delta;
    if (next < 1) next = 1;
    if (next > 1440) next = 1440;
    editor_countdown_minutes = static_cast<uint16_t>(next);
    update_countdown_editor_label();
}

void on_countdown_start(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (!apply(EchoDeskClockAction::CONFIGURE, EchoDeskClockMode::COUNTDOWN,
               static_cast<uint32_t>(editor_countdown_minutes) * 60U)) return;
    apply(EchoDeskClockAction::START, EchoDeskClockMode::COUNTDOWN);
    current_view = ClockView::COUNTDOWN;
    render_view();
}

void update_focus_editor_labels() {
    char text[16];
    if (focus_editor_values[0]) {
        snprintf(text, sizeof(text), "%u min", editor_focus_minutes);
        lv_label_set_text(focus_editor_values[0], text);
    }
    if (focus_editor_values[1]) {
        snprintf(text, sizeof(text), "%u min", editor_break_minutes);
        lv_label_set_text(focus_editor_values[1], text);
    }
    if (focus_editor_values[2]) {
        snprintf(text, sizeof(text), "%u", editor_rounds);
        lv_label_set_text(focus_editor_values[2], text);
    }
}

void on_focus_adjust(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    const intptr_t packed = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    const uint8_t field = static_cast<uint8_t>((packed >> 8) & 0xFF);
    const int8_t delta = static_cast<int8_t>(packed & 0xFF);
    if (field == 0) {
        int value = static_cast<int>(editor_focus_minutes) + delta;
        if (value < 1) value = 1;
        if (value > 180) value = 180;
        editor_focus_minutes = static_cast<uint16_t>(value);
    } else if (field == 1) {
        int value = static_cast<int>(editor_break_minutes) + delta;
        if (value < 1) value = 1;
        if (value > 60) value = 60;
        editor_break_minutes = static_cast<uint16_t>(value);
    } else {
        int value = static_cast<int>(editor_rounds) + delta;
        if (value < 1) value = 1;
        if (value > 12) value = 12;
        editor_rounds = static_cast<uint8_t>(value);
    }
    update_focus_editor_labels();
}

void on_focus_start(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (!apply(EchoDeskClockAction::CONFIGURE, EchoDeskClockMode::FOCUS, 0,
               editor_focus_minutes, editor_break_minutes, editor_rounds)) return;
    apply(EchoDeskClockAction::START, EchoDeskClockMode::FOCUS);
    current_view = ClockView::FOCUS;
    render_view();
}

void create_selector() {
    lv_label_set_text(heading_label, "CLOCK");
    lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *time_panel = lv_obj_create(content);
    style_button(time_panel, 10);
    lv_obj_clear_flag(time_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(time_panel, 304, 70);
    lv_obj_set_pos(time_panel, 8, 7);
    selector_time = make_label(time_panel, "--:--", &lv_font_montserrat_36,
                               foreground_color());
    lv_obj_set_pos(selector_time, 14, 3);
    selector_date = make_label(time_panel, "WAITING FOR TIME",
                               &lv_font_montserrat_12, muted_color());
    lv_obj_set_pos(selector_date, 16, 48);
    lv_obj_t *hint = make_label(time_panel, "TOOLS", &lv_font_montserrat_12,
                                muted_color());
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -14, 0);

    struct Card { EchoDeskClockMode mode; const char *icon; const char *title; };
    static const Card cards[] = {
        {EchoDeskClockMode::STOPWATCH, "00", "STOPWATCH"},
        {EchoDeskClockMode::COUNTDOWN, "10", "COUNTDOWN"},
        {EchoDeskClockMode::FOCUS, "25", "FOCUS"},
    };
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_t *button = lv_button_create(content);
        style_button(button, 10);
        lv_obj_set_size(button, 96, 110);
        lv_obj_set_pos(button, 8 + i * 104, 84);
        lv_obj_add_event_cb(button, on_selector_mode, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(cards[i].mode)));
        lv_obj_t *icon = make_label(button, cards[i].icon, &lv_font_montserrat_24,
                                    accent_color());
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 14);
        lv_obj_t *title = make_label(button, cards[i].title,
                                     &lv_font_montserrat_12, foreground_color());
        lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -17);
        lv_obj_t *arrow = make_label(button, ">", &lv_font_montserrat_12,
                                     muted_color());
        lv_obj_align(arrow, LV_ALIGN_BOTTOM_MID, 0, -1);
    }
}

void create_stopwatch() {
    lv_label_set_text(heading_label, "STOPWATCH");
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    status_value = make_label(content, "READY", &lv_font_montserrat_12,
                              muted_color());
    lv_obj_align(status_value, LV_ALIGN_TOP_MID, 0, 23);
    primary_value = make_label(content, "00:00:00.0", &lv_font_montserrat_36,
                               foreground_color());
    lv_obj_set_width(primary_value, 304);
    lv_obj_set_style_text_align(primary_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(primary_value, 8, 60);
    lv_obj_t *primary = make_button(content, "START", 36, 137, 150, 48,
                                    on_timer_primary);
    primary_button_label = lv_obj_get_child(primary, 0);
    make_button(content, "RESET", 198, 137, 86, 48, on_timer_reset);
}

lv_obj_t *create_ring() {
    lv_obj_t *arc = lv_arc_create(content);
    lv_obj_set_size(arc, 148, 148);
    lv_obj_set_pos(arc, 10, 25);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, accent_color(), LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    return arc;
}

void create_countdown() {
    lv_label_set_text(heading_label, "COUNTDOWN");
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    ring = create_ring();
    primary_value = make_label(content, "10:00", &lv_font_montserrat_24,
                               foreground_color());
    lv_obj_set_width(primary_value, 126);
    lv_obj_set_style_text_align(primary_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(primary_value, 21, 80);
    status_value = make_label(content, "READY", &lv_font_montserrat_12,
                              muted_color());
    lv_obj_set_width(status_value, 126);
    lv_obj_set_style_text_align(status_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(status_value, 21, 112);
    lv_obj_t *primary = make_button(content, "START", 176, 43, 128, 47,
                                    on_timer_primary);
    primary_button_label = lv_obj_get_child(primary, 0);
    make_button(content, "RESET", 176, 101, 128, 42, on_timer_reset);
    make_button(content, "CHANGE", 176, 154, 128, 38,
                [](lv_event_t *event) {
                    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
                        !navigation_allowed()) return;
                    EchoDeskClockSnapshot snapshot{};
                    echodesk_clock_capture(millis(), snapshot);
                    if (request_configuration &&
                        request_configuration(EchoDeskClockMode::COUNTDOWN,
                                              callback_context)) {
                        remote_wait_mode = EchoDeskClockMode::COUNTDOWN;
                        remote_wait_revision = snapshot.revision;
                        current_view = ClockView::REMOTE_WAIT;
                        render_view();
                        return;
                    }
                    editor_countdown_minutes = static_cast<uint16_t>(
                        (snapshot.countdown.duration_seconds + 59U) / 60U);
                    current_view = ClockView::COUNTDOWN_EDITOR;
                    render_view();
                });
}

void create_focus() {
    lv_label_set_text(heading_label, "FOCUS TIMER");
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    ring = create_ring();
    primary_value = make_label(content, "25:00", &lv_font_montserrat_24,
                               foreground_color());
    lv_obj_set_width(primary_value, 126);
    lv_obj_set_style_text_align(primary_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(primary_value, 21, 72);
    status_value = make_label(content, "FOCUS", &lv_font_montserrat_12,
                              muted_color());
    lv_obj_set_width(status_value, 126);
    lv_obj_set_style_text_align(status_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(status_value, 21, 103);
    secondary_value = make_label(content, "ROUND 1 OF 4", &lv_font_montserrat_12,
                                 muted_color());
    lv_obj_set_width(secondary_value, 126);
    lv_obj_set_style_text_align(secondary_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(secondary_value, 21, 124);
    lv_obj_t *primary = make_button(content, "START", 176, 30, 128, 43,
                                    on_timer_primary);
    primary_button_label = lv_obj_get_child(primary, 0);
    make_button(content, "SKIP", 176, 82, 62, 42, on_focus_skip);
    make_button(content, "RESET", 242, 82, 62, 42, on_timer_reset);
    make_button(content, "CHANGE", 176, 134, 128, 42,
                [](lv_event_t *event) {
                    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
                        !navigation_allowed()) return;
                    EchoDeskClockSnapshot snapshot{};
                    echodesk_clock_capture(millis(), snapshot);
                    if (request_configuration &&
                        request_configuration(EchoDeskClockMode::FOCUS,
                                              callback_context)) {
                        remote_wait_mode = EchoDeskClockMode::FOCUS;
                        remote_wait_revision = snapshot.revision;
                        current_view = ClockView::REMOTE_WAIT;
                        render_view();
                        return;
                    }
                    editor_focus_minutes = snapshot.focus.focus_minutes;
                    editor_break_minutes = snapshot.focus.break_minutes;
                    editor_rounds = snapshot.focus.rounds;
                    current_view = ClockView::FOCUS_EDITOR;
                    render_view();
                });
}

void create_countdown_editor() {
    lv_label_set_text(heading_label, "SET COUNTDOWN");
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *caption = make_label(content, "DURATION", &lv_font_montserrat_12,
                                   muted_color());
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 19);
    countdown_editor_value = make_label(content, "10 min", &lv_font_montserrat_36,
                                        foreground_color());
    lv_obj_set_width(countdown_editor_value, 304);
    lv_obj_set_style_text_align(countdown_editor_value, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_pos(countdown_editor_value, 8, 47);
    make_button(content, "-5", 16, 103, 62, 41, on_countdown_adjust,
                reinterpret_cast<void *>(static_cast<intptr_t>(-5)));
    make_button(content, "-1", 84, 103, 62, 41, on_countdown_adjust,
                reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    make_button(content, "+1", 174, 103, 62, 41, on_countdown_adjust,
                reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    make_button(content, "+5", 242, 103, 62, 41, on_countdown_adjust,
                reinterpret_cast<void *>(static_cast<intptr_t>(5)));
    make_button(content, "START COUNTDOWN", 52, 155, 216, 42,
                on_countdown_start);
    update_countdown_editor_label();
}

intptr_t pack_focus_adjust(uint8_t field, int8_t delta) {
    return (static_cast<intptr_t>(field) << 8) |
           static_cast<uint8_t>(delta);
}

void create_focus_editor() {
    lv_label_set_text(heading_label, "SET FOCUS");
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    static const char *names[] = {"FOCUS", "BREAK", "ROUNDS"};
    for (uint8_t i = 0; i < 3; ++i) {
        const int16_t y = 10 + i * 51;
        lv_obj_t *name = make_label(content, names[i], &lv_font_montserrat_12,
                                    muted_color());
        lv_obj_set_pos(name, 15, y + 13);
        make_button(content, "-", 104, y, 42, 40, on_focus_adjust,
                    reinterpret_cast<void *>(pack_focus_adjust(i, -1)));
        focus_editor_values[i] = make_label(content, "--", &lv_font_montserrat_14,
                                            foreground_color());
        lv_obj_set_width(focus_editor_values[i], 78);
        lv_obj_set_style_text_align(focus_editor_values[i], LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN);
        lv_obj_set_pos(focus_editor_values[i], 151, y + 12);
        make_button(content, "+", 234, y, 42, 40, on_focus_adjust,
                    reinterpret_cast<void *>(pack_focus_adjust(i, 1)));
    }
    make_button(content, "START FOCUS", 68, 163, 184, 35, on_focus_start);
    update_focus_editor_labels();
}

void create_remote_wait() {
    lv_label_set_text(heading_label,
                      remote_wait_mode == EchoDeskClockMode::COUNTDOWN
                          ? "COUNTDOWN" : "FOCUS TIMER");
    lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *spinner = lv_spinner_create(content);
    lv_obj_set_size(spinner, 50, 50);
    lv_spinner_set_anim_params(spinner, 800, 260);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, accent_color(), LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_t *title = make_label(content, "FINISH ON DESKTOP",
                                 &lv_font_montserrat_16, foreground_color());
    lv_obj_set_width(title, 304);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(title, 8, 100);
    lv_obj_t *detail = make_label(
        content,
        remote_wait_mode == EchoDeskClockMode::COUNTDOWN
            ? "Choose a duration in EchoDesk Desktop."
            : "Choose focus, break and rounds in EchoDesk Desktop.",
        &lv_font_montserrat_12, muted_color());
    lv_obj_set_width(detail, 288);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(detail, 16, 132);
    make_button(content, "SET UP HERE", 88, 164, 144, 34,
                [](lv_event_t *event) {
                    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
                        !navigation_allowed()) return;
                    current_view = remote_wait_mode == EchoDeskClockMode::COUNTDOWN
                        ? ClockView::COUNTDOWN_EDITOR : ClockView::FOCUS_EDITOR;
                    render_view();
                });
}

void render_view() {
    if (!content) return;
    lv_obj_clean(content);
    clear_view_references();
    switch (current_view) {
        case ClockView::SELECTOR: create_selector(); break;
        case ClockView::STOPWATCH: create_stopwatch(); break;
        case ClockView::COUNTDOWN: create_countdown(); break;
        case ClockView::FOCUS: create_focus(); break;
        case ClockView::COUNTDOWN_EDITOR: create_countdown_editor(); break;
        case ClockView::FOCUS_EDITOR: create_focus_editor(); break;
        case ClockView::REMOTE_WAIT: create_remote_wait(); break;
    }
    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_capture(millis(), snapshot);
    known_revision = snapshot.revision;
    refresh_view(snapshot);
}

void format_elapsed(uint64_t elapsed_ms, char *out, size_t out_size) {
    const uint64_t hours = elapsed_ms / 3600000ULL;
    const uint8_t minutes = static_cast<uint8_t>((elapsed_ms / 60000ULL) % 60ULL);
    const uint8_t seconds = static_cast<uint8_t>((elapsed_ms / 1000ULL) % 60ULL);
    const uint8_t tenths = static_cast<uint8_t>((elapsed_ms / 100ULL) % 10ULL);
    snprintf(out, out_size, "%02llu:%02u:%02u.%u",
             static_cast<unsigned long long>(hours), minutes, seconds, tenths);
}

void format_remaining(uint32_t seconds, char *out, size_t out_size) {
    const uint32_t hours = seconds / 3600U;
    const uint8_t minutes = static_cast<uint8_t>((seconds / 60U) % 60U);
    const uint8_t remaining_seconds = static_cast<uint8_t>(seconds % 60U);
    if (hours > 0) {
        snprintf(out, out_size, "%lu:%02u:%02u",
                 static_cast<unsigned long>(hours), minutes, remaining_seconds);
    } else {
        snprintf(out, out_size, "%02u:%02u", minutes, remaining_seconds);
    }
}

void update_selector_time() {
    if (!selector_time || !selector_date) return;
    const time_t now = time(nullptr);
    if (now <= 1700000000) {
        lv_label_set_text(selector_time, "--:--");
        lv_label_set_text(selector_date, "WAITING FOR TIME");
        return;
    }
    struct tm local{};
    localtime_r(&now, &local);
    char time_text[12];
    char date_text[28];
    strftime(time_text, sizeof(time_text),
             g_config.clock_format == CLOCK_FORMAT_24_HOUR ? "%H:%M" : "%I:%M %p",
             &local);
    if (g_config.clock_format != CLOCK_FORMAT_24_HOUR && time_text[0] == '0') {
        for (uint8_t i = 0; time_text[i] != '\0'; ++i) time_text[i] = time_text[i + 1];
    }
    strftime(date_text, sizeof(date_text), "%a, %d %b", &local);
    lv_label_set_text(selector_time, time_text);
    lv_label_set_text(selector_date, date_text);
}

void refresh_view(const EchoDeskClockSnapshot &snapshot) {
    char text[40];
    if (current_view == ClockView::SELECTOR) {
        update_selector_time();
        return;
    }
    if (current_view == ClockView::STOPWATCH && primary_value) {
        format_elapsed(snapshot.stopwatch.elapsed_ms, text, sizeof(text));
        lv_label_set_text(primary_value, text);
        lv_label_set_text(status_value,
                          echodesk_clock_status_name(snapshot.stopwatch.status));
        lv_label_set_text(primary_button_label,
                          snapshot.stopwatch.status == EchoDeskClockStatus::RUNNING
                              ? "PAUSE" : "START");
        return;
    }
    if (current_view == ClockView::COUNTDOWN && primary_value) {
        format_remaining(snapshot.countdown.remaining_seconds, text, sizeof(text));
        lv_label_set_text(primary_value, text);
        lv_label_set_text(status_value,
                          echodesk_clock_status_name(snapshot.countdown.status));
        lv_arc_set_value(ring, snapshot.countdown.progress_per_mille);
        lv_obj_set_style_arc_color(
            ring, snapshot.countdown.status == EchoDeskClockStatus::COMPLETE
                      ? complete_color() : accent_color(), LV_PART_INDICATOR);
        lv_label_set_text(primary_button_label,
                          snapshot.countdown.status == EchoDeskClockStatus::RUNNING
                              ? "PAUSE" : snapshot.countdown.status == EchoDeskClockStatus::COMPLETE
                                  ? "RESTART" : "START");
        return;
    }
    if (current_view == ClockView::FOCUS && primary_value) {
        format_remaining(snapshot.focus.remaining_seconds, text, sizeof(text));
        lv_label_set_text(primary_value, text);
        snprintf(text, sizeof(text), "%s · %s",
                 echodesk_focus_phase_name(snapshot.focus.phase),
                 echodesk_clock_status_name(snapshot.focus.status));
        lv_label_set_text(status_value, text);
        snprintf(text, sizeof(text), "ROUND %u OF %u", snapshot.focus.current_round,
                 snapshot.focus.rounds);
        lv_label_set_text(secondary_value, text);
        lv_arc_set_value(ring, snapshot.focus.progress_per_mille);
        lv_obj_set_style_arc_color(
            ring, snapshot.focus.status == EchoDeskClockStatus::COMPLETE
                      ? complete_color() : accent_color(), LV_PART_INDICATOR);
        lv_label_set_text(primary_button_label,
                          snapshot.focus.status == EchoDeskClockStatus::RUNNING
                              ? "PAUSE" : snapshot.focus.status == EchoDeskClockStatus::COMPLETE
                                  ? "RESTART" : "START");
    }
}

} // namespace

void ui_clock_set_callbacks(UiClockNavigationCallback home_callback,
                            UiClockConfigurationRequestCallback config_callback,
                            UiClockNavigationCallback state_changed_callback,
                            void *context) {
    navigate_home = home_callback;
    request_configuration = config_callback;
    state_changed = state_changed_callback;
    callback_context = context;
}

lv_obj_t *ui_clock_create() {
    if (screen) return screen;
    screen = lv_obj_create(nullptr);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_set_style_bg_color(screen, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    create_header();
    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 320, 200);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    current_view = ClockView::SELECTOR;
    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_capture(millis(), snapshot);
    known_revision = snapshot.revision;
    render_view();
    last_refresh_ms = millis();
    return screen;
}

lv_obj_t *ui_clock_screen() { return screen; }

lv_obj_t *ui_clock_recreate() {
    const ClockView saved_view = current_view;
    lv_obj_t *old = screen;
    const bool active = old && lv_screen_active() == old;
    screen = nullptr;
    content = nullptr;
    lv_obj_t *replacement = ui_clock_create();
    current_view = saved_view;
    render_view();
    if (active) lv_screen_load(replacement);
    if (old && old != replacement) lv_obj_delete(old);
    return replacement;
}

void ui_clock_tick() {
    if (!screen || lv_screen_active() != screen) return;
    const uint32_t now = millis();
    if (now - last_refresh_ms < REFRESH_INTERVAL_MS) return;
    last_refresh_ms = now;

    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_capture(now, snapshot);
    if (snapshot.revision != known_revision) {
        if (current_view == ClockView::REMOTE_WAIT &&
            snapshot.revision != remote_wait_revision) {
            current_view = view_for_mode(remote_wait_mode);
            render_view();
            return;
        }
        if (snapshot.active_mode != EchoDeskClockMode::TIME &&
            current_view == ClockView::SELECTOR) {
            current_view = view_for_mode(snapshot.active_mode);
            render_view();
            return;
        }
        known_revision = snapshot.revision;
    }
    refresh_view(snapshot);
}

#else

void ui_clock_set_callbacks(UiClockNavigationCallback,
                            UiClockConfigurationRequestCallback,
                            UiClockNavigationCallback, void *) {}
lv_obj_t *ui_clock_create() { return nullptr; }
lv_obj_t *ui_clock_screen() { return nullptr; }
lv_obj_t *ui_clock_recreate() { return nullptr; }
void ui_clock_tick() {}

#endif // ECHODESK_OS
