#include "ui_notification_stack.h"

#if defined(ECHODESK_OS)

#include "config_store.h"
#include "echodesk_notifications.h"

#include <Arduino.h>
#include <stdio.h>
#include <time.h>

namespace {

constexpr uint32_t REFRESH_INTERVAL_MS = 350;
constexpr uint32_t NAVIGATION_GUARD_MS = 250;

enum class NotificationView : uint8_t {
    LIST,
    DETAIL,
};

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *header_action = nullptr;
UiNotificationStackNavigationCallback home_callback = nullptr;
void *navigation_context = nullptr;
NotificationView current_view = NotificationView::LIST;
uint32_t selected_id = 0;
uint32_t known_revision = 0;
uint32_t last_refresh_ms = 0;
uint32_t last_navigation_ms = 0;

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

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    return label;
}

void style_button(lv_obj_t *button, uint8_t radius = 8) {
    lv_obj_set_style_bg_color(button, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, pressed_color(),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
}

bool navigation_allowed() {
    const uint32_t now = millis();
    if (now - last_navigation_ms < NAVIGATION_GUARD_MS) return false;
    last_navigation_ms = now;
    return true;
}

void render();

void invoke_home(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (current_view == NotificationView::DETAIL) {
        current_view = NotificationView::LIST;
        selected_id = 0;
        render();
        return;
    }
    if (home_callback) home_callback(navigation_context);
}

void on_clear(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    echodesk_notifications_clear();
    render();
}

void on_open_notification(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    selected_id = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    current_view = NotificationView::DETAIL;
    render();
}

void on_dismiss(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (selected_id != 0) echodesk_notifications_dismiss(selected_id);
    selected_id = 0;
    current_view = NotificationView::LIST;
    render();
}

void on_screen_deleted(lv_event_t *event) {
    if (lv_event_get_target(event) != screen) return;
    screen = nullptr;
    content = nullptr;
    header_action = nullptr;
}

void format_time(uint64_t epoch_seconds, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (epoch_seconds == 0) {
        snprintf(out, out_size, "RECENT");
        return;
    }
    const time_t value = static_cast<time_t>(epoch_seconds);
    struct tm local{};
    if (!localtime_r(&value, &local) ||
        strftime(out, out_size, "%H:%M", &local) == 0) {
        snprintf(out, out_size, "RECENT");
    }
}

void create_header() {
    lv_obj_t *back = lv_button_create(screen);
    style_button(back);
    lv_obj_set_size(back, 64, 30);
    lv_obj_set_pos(back, 6, 5);
    lv_obj_t *back_label = make_label(back, "HOME", &lv_font_montserrat_12,
                                      foreground_color());
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back, invoke_home, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *heading = make_label(screen, "NOTIFICATION STACK",
                                   &lv_font_montserrat_14, foreground_color());
    lv_obj_set_width(heading, 184);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(heading, 68, 12);

    header_action = lv_button_create(screen);
    style_button(header_action);
    lv_obj_set_size(header_action, 58, 30);
    lv_obj_set_pos(header_action, 256, 5);
    lv_obj_t *clear_label = make_label(header_action, "CLEAR",
                                       &lv_font_montserrat_12,
                                       foreground_color());
    lv_obj_center(clear_label);
    lv_obj_add_event_cb(header_action, on_clear, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 304, 1);
    lv_obj_set_pos(divider, 8, 39);
    lv_obj_set_style_bg_color(divider, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
}

void prepare_content(bool scrollable) {
    if (content) lv_obj_delete(content);
    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 320, 200);
    lv_obj_set_pos(content, 0, 40);
    if (scrollable) {
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(content, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_width(content, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(content, divider_color(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(content, LV_OPA_70, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(content, 2, LV_PART_SCROLLBAR);
    } else {
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void render_not_set_up() {
    prepare_content(false);
    lv_obj_add_flag(header_action, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *badge = lv_obj_create(content);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 50, 50);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_bg_color(badge, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(badge, 15, LV_PART_MAIN);
    lv_obj_t *icon = make_label(badge, LV_SYMBOL_BELL, &lv_font_montserrat_24,
                                foreground_color());
    lv_obj_center(icon);

    lv_obj_t *title = make_label(content, "NOT SET UP", &lv_font_montserrat_20,
                                 foreground_color());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_t *body = make_label(
        content,
        "Open EchoDesk Desktop\nOverview > Components\nNotification Stack > Set up notifications",
        &lv_font_montserrat_12, muted_color());
    lv_obj_set_width(body, 300);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(body, 5, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 107);
}

void render_empty() {
    prepare_content(false);
    lv_obj_add_flag(header_action, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *icon = make_label(content, LV_SYMBOL_OK, &lv_font_montserrat_36,
                                foreground_color());
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 33);
    lv_obj_t *title = make_label(content, "ALL CAUGHT UP",
                                 &lv_font_montserrat_20, foreground_color());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 83);
    lv_obj_t *body = make_label(
        content, "New notifications from your computer\nwill appear here.",
        &lv_font_montserrat_12, muted_color());
    lv_obj_set_width(body, 290);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(body, 4, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 120);
}

void render_list(const EchoDeskNotificationSnapshot &snapshot) {
    prepare_content(true);
    lv_obj_clear_flag(header_action, LV_OBJ_FLAG_HIDDEN);
    for (uint8_t i = 0; i < snapshot.count; ++i) {
        const EchoDeskNotificationItem &item = snapshot.items[i];
        const int16_t y = static_cast<int16_t>(6 + i * 73);
        lv_obj_t *card = lv_button_create(content);
        style_button(card, 10);
        lv_obj_set_size(card, 304, 65);
        lv_obj_set_pos(card, 8, y);
        lv_obj_add_event_cb(
            card, on_open_notification, LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<uintptr_t>(item.id)));

        lv_obj_t *source = make_label(card, item.source,
                                      &lv_font_montserrat_12, muted_color());
        lv_obj_set_width(source, 204);
        lv_label_set_long_mode(source, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(source, 11, 6);

        char timestamp[16];
        format_time(item.timestamp_epoch_seconds, timestamp, sizeof(timestamp));
        lv_obj_t *time = make_label(card, timestamp, &lv_font_montserrat_12,
                                    muted_color());
        lv_obj_set_width(time, 60);
        lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_pos(time, 231, 6);

        lv_obj_t *title = make_label(card, item.title,
                                     &lv_font_montserrat_14,
                                     foreground_color());
        lv_obj_set_width(title, 276);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(title, 11, 25);

        lv_obj_t *body = make_label(card, item.body[0] ? item.body : "Tap to open",
                                    &lv_font_montserrat_12, muted_color());
        lv_obj_set_width(body, 276);
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(body, 11, 45);
    }
    lv_obj_t *end_marker = lv_obj_create(content);
    lv_obj_remove_style_all(end_marker);
    lv_obj_set_size(end_marker, 1, 1);
    lv_obj_set_pos(end_marker, 0, static_cast<int16_t>(snapshot.count * 73 + 5));
}

const EchoDeskNotificationItem *find_selected(
    const EchoDeskNotificationSnapshot &snapshot) {
    for (uint8_t i = 0; i < snapshot.count; ++i) {
        if (snapshot.items[i].id == selected_id) return &snapshot.items[i];
    }
    return nullptr;
}

void render_detail(const EchoDeskNotificationSnapshot &snapshot) {
    const EchoDeskNotificationItem *item = find_selected(snapshot);
    if (!item) {
        current_view = NotificationView::LIST;
        selected_id = 0;
        if (snapshot.count == 0) render_empty();
        else render_list(snapshot);
        return;
    }
    prepare_content(true);
    lv_obj_add_flag(header_action, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back_label = lv_obj_get_child(lv_obj_get_child(screen, 0), 0);
    if (back_label) lv_label_set_text(back_label, "BACK");

    char timestamp[16];
    format_time(item->timestamp_epoch_seconds, timestamp, sizeof(timestamp));
    char eyebrow[64];
    snprintf(eyebrow, sizeof(eyebrow), "%s  ·  %s", item->source, timestamp);
    lv_obj_t *source = make_label(content, eyebrow, &lv_font_montserrat_12,
                                  muted_color());
    lv_obj_set_width(source, 294);
    lv_label_set_long_mode(source, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(source, 13, 11);

    lv_obj_t *title = make_label(content, item->title, &lv_font_montserrat_20,
                                 foreground_color());
    lv_obj_set_width(title, 294);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(title, 13, 36);
    lv_obj_update_layout(title);

    lv_obj_t *body = make_label(content,
                                item->body[0] ? item->body : "No message preview.",
                                &lv_font_montserrat_14, foreground_color());
    lv_obj_set_width(body, 294);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body, 4, LV_PART_MAIN);
    const int32_t body_y = lv_obj_get_y(title) + lv_obj_get_height(title) + 12;
    lv_obj_set_pos(body, 13, body_y);
    lv_obj_update_layout(body);

    lv_obj_t *dismiss = lv_button_create(content);
    style_button(dismiss, 9);
    lv_obj_set_size(dismiss, 294, 40);
    int32_t dismiss_y = lv_obj_get_y(body) + lv_obj_get_height(body) + 18;
    if (dismiss_y < 151) dismiss_y = 151;
    lv_obj_set_pos(dismiss, 13, dismiss_y);
    lv_obj_t *dismiss_label = make_label(dismiss, "DISMISS",
                                         &lv_font_montserrat_14,
                                         foreground_color());
    lv_obj_center(dismiss_label);
    lv_obj_add_event_cb(dismiss, on_dismiss, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *end_marker = lv_obj_create(content);
    lv_obj_remove_style_all(end_marker);
    lv_obj_set_size(end_marker, 1, 1);
    lv_obj_set_pos(end_marker, 0, dismiss_y + 48);
}

void render() {
    if (!screen) return;
    EchoDeskNotificationSnapshot snapshot{};
    echodesk_notifications_capture(snapshot);
    known_revision = snapshot.revision;

    lv_obj_t *home_button = lv_obj_get_child(screen, 0);
    if (home_button) {
        lv_obj_t *label = lv_obj_get_child(home_button, 0);
        if (label) lv_label_set_text(label,
            current_view == NotificationView::DETAIL ? "BACK" : "HOME");
    }

    if (!snapshot.enabled) {
        current_view = NotificationView::LIST;
        selected_id = 0;
        render_not_set_up();
    } else if (current_view == NotificationView::DETAIL) {
        render_detail(snapshot);
    } else if (snapshot.count == 0) {
        render_empty();
    } else {
        render_list(snapshot);
    }
}

} // namespace

void ui_notification_stack_set_navigation_callback(
    UiNotificationStackNavigationCallback callback, void *context) {
    home_callback = callback;
    navigation_context = context;
}

lv_obj_t *ui_notification_stack_create() {
    if (screen) return screen;
    screen = lv_obj_create(nullptr);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_set_style_bg_color(screen, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    create_header();
    render();
    last_refresh_ms = millis();
    return screen;
}

lv_obj_t *ui_notification_stack_screen() { return screen; }

lv_obj_t *ui_notification_stack_recreate() {
    const NotificationView saved_view = current_view;
    const uint32_t saved_id = selected_id;
    lv_obj_t *old = screen;
    const bool active = old && lv_screen_active() == old;
    screen = nullptr;
    content = nullptr;
    header_action = nullptr;
    lv_obj_t *replacement = ui_notification_stack_create();
    current_view = saved_view;
    selected_id = saved_id;
    render();
    if (active) lv_screen_load(replacement);
    if (old && old != replacement) lv_obj_delete(old);
    return replacement;
}

void ui_notification_stack_tick() {
    if (!screen || lv_screen_active() != screen) return;
    const uint32_t now = millis();
    if (now - last_refresh_ms < REFRESH_INTERVAL_MS) return;
    last_refresh_ms = now;
    EchoDeskNotificationSnapshot snapshot{};
    echodesk_notifications_capture(snapshot);
    if (snapshot.revision != known_revision) render();
}

#else

void ui_notification_stack_set_navigation_callback(
    UiNotificationStackNavigationCallback, void *) {}
lv_obj_t *ui_notification_stack_create() { return nullptr; }
lv_obj_t *ui_notification_stack_screen() { return nullptr; }
lv_obj_t *ui_notification_stack_recreate() { return nullptr; }
void ui_notification_stack_tick() {}

#endif // ECHODESK_OS
