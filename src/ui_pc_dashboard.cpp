#include "ui_pc_dashboard.h"

#if defined(ECHODESK_OS)

#include "config_store.h"
#include "pc_dashboard.h"
#include "performance_data.h"
#include "serial_receiver.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t NAVIGATION_GUARD_MS = 250;
constexpr uint8_t GRAPH_POINTS = 24;

enum class DashboardView : uint8_t { DASHBOARD, CUSTOMISE };

struct TileWidgets {
    lv_obj_t *value = nullptr;
    lv_obj_t *note = nullptr;
    lv_obj_t *chart = nullptr;
    lv_chart_series_t *series = nullptr;
    lv_obj_t *bar = nullptr;
};

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *heading = nullptr;
lv_obj_t *header_action = nullptr;
lv_obj_t *selection_count = nullptr;
lv_obj_t *picker_buttons[static_cast<uint8_t>(PcDashboardMetric::COUNT)] = {};
lv_obj_t *picker_badges[static_cast<uint8_t>(PcDashboardMetric::COUNT)] = {};
TileWidgets tiles[PC_DASHBOARD_MAX_WIDGETS]{};
UiPcDashboardNavigationCallback home_callback = nullptr;
void *navigation_context = nullptr;
DashboardView current_view = DashboardView::DASHBOARD;
PcDashboardConfig editing_config{};
uint32_t editing_revision = 0;
uint32_t known_revision = 0;
uint32_t last_refresh_ms = 0;
uint32_t last_navigation_ms = 0;
char transient_message[48] = {};

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
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x1463DF)
                                         : lv_color_hex(0x72A7FF);
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

const char *metric_title(PcDashboardMetric metric) {
    switch (metric) {
        case PcDashboardMetric::BATTERY: return "BATTERY";
        case PcDashboardMetric::MICROPHONE: return "MICROPHONE";
        case PcDashboardMetric::STORAGE: return "STORAGE";
        case PcDashboardMetric::RAM: return "RAM";
        case PcDashboardMetric::CPU: return "CPU";
        case PcDashboardMetric::GPU: return "GPU";
        case PcDashboardMetric::NETWORK_DOWN: return "DOWNLOAD";
        case PcDashboardMetric::NETWORK_UP: return "UPLOAD";
        case PcDashboardMetric::CPU_TEMPERATURE: return "CPU TEMP";
        case PcDashboardMetric::GPU_TEMPERATURE: return "GPU TEMP";
        case PcDashboardMetric::MOTHERBOARD_TEMPERATURE: return "BOARD TEMP";
        case PcDashboardMetric::COUNT: break;
    }
    return "METRIC";
}

bool is_host_metric(PcDashboardMetric metric) {
    return metric == PcDashboardMetric::BATTERY ||
           metric == PcDashboardMetric::MICROPHONE ||
           metric == PcDashboardMetric::STORAGE;
}

bool is_graph_metric(PcDashboardMetric metric) {
    return !is_host_metric(metric);
}

bool is_percentage_metric(PcDashboardMetric metric) {
    return metric == PcDashboardMetric::CPU || metric == PcDashboardMetric::GPU ||
           metric == PcDashboardMetric::RAM;
}

bool is_temperature_metric(PcDashboardMetric metric) {
    return metric == PcDashboardMetric::CPU_TEMPERATURE ||
           metric == PcDashboardMetric::GPU_TEMPERATURE ||
           metric == PcDashboardMetric::MOTHERBOARD_TEMPERATURE;
}

uint8_t selection_index(PcDashboardMetric metric) {
    for (uint8_t i = 0; i < editing_config.count; ++i) {
        if (editing_config.metrics[i] == metric) return i;
    }
    return 0xFF;
}

void clear_tile_references() {
    for (TileWidgets &tile : tiles) tile = TileWidgets{};
}

void render();

void on_home(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (current_view == DashboardView::CUSTOMISE) {
        current_view = DashboardView::DASHBOARD;
        transient_message[0] = '\0';
        render();
        return;
    }
    if (home_callback) home_callback(navigation_context);
}

void on_customise(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    PcDashboardSnapshot snapshot{};
    pc_dashboard_capture(millis(), snapshot);
    editing_config = snapshot.config;
    editing_revision = snapshot.revision;
    transient_message[0] = '\0';
    current_view = DashboardView::CUSTOMISE;
    render();
}

void on_picker_toggle(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    const PcDashboardMetric metric = static_cast<PcDashboardMetric>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    const uint8_t existing = selection_index(metric);
    transient_message[0] = '\0';
    if (existing != 0xFF) {
        if (editing_config.count == PC_DASHBOARD_MIN_WIDGETS) {
            snprintf(transient_message, sizeof(transient_message),
                     "Keep at least one metric");
        } else {
            for (uint8_t i = existing; i + 1 < editing_config.count; ++i) {
                editing_config.metrics[i] = editing_config.metrics[i + 1];
            }
            --editing_config.count;
        }
    } else if (editing_config.count >= PC_DASHBOARD_MAX_WIDGETS) {
        snprintf(transient_message, sizeof(transient_message),
                 "Maximum four metrics");
    } else {
        editing_config.metrics[editing_config.count++] = metric;
    }
    render();
}

void on_picker_save(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (editing_revision != pc_dashboard_revision() ||
        !serial_echodesk_pc_dashboard_apply_local_config(editing_config)) {
        snprintf(transient_message, sizeof(transient_message), "%s",
                 editing_revision != pc_dashboard_revision()
                     ? "Dashboard changed; reopen editor"
                     : "Could not save selection");
        render();
        return;
    }
    current_view = DashboardView::DASHBOARD;
    transient_message[0] = '\0';
    render();
}

void on_screen_deleted(lv_event_t *event) {
    if (lv_event_get_target(event) != screen) return;
    screen = nullptr;
    content = nullptr;
    heading = nullptr;
    header_action = nullptr;
    selection_count = nullptr;
    memset(picker_buttons, 0, sizeof(picker_buttons));
    memset(picker_badges, 0, sizeof(picker_badges));
    clear_tile_references();
}

void create_header() {
    lv_obj_t *home = lv_button_create(screen);
    style_button(home);
    lv_obj_set_size(home, 64, 30);
    lv_obj_set_pos(home, 6, 5);
    lv_obj_t *home_label = make_label(home, "HOME", &lv_font_montserrat_12,
                                      foreground_color());
    lv_obj_center(home_label);
    lv_obj_add_event_cb(home, on_home, LV_EVENT_CLICKED, nullptr);

    heading = make_label(screen, "PC DASHBOARD", &lv_font_montserrat_14,
                         foreground_color());
    lv_obj_set_width(heading, 180);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(heading, 70, 12);

    header_action = lv_button_create(screen);
    style_button(header_action);
    lv_obj_set_size(header_action, 64, 30);
    lv_obj_set_pos(header_action, 250, 5);
    lv_obj_t *action_label = make_label(header_action, "EDIT",
                                        &lv_font_montserrat_12,
                                        foreground_color());
    lv_obj_center(action_label);
    lv_obj_add_event_cb(header_action, on_customise, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 304, 1);
    lv_obj_set_pos(divider, 8, 39);
    lv_obj_set_style_bg_color(divider, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
}

void prepare_content(bool scrollable = false) {
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
    } else {
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    }
    clear_tile_references();
}

void tile_geometry(uint8_t count, uint8_t index, int16_t &x, int16_t &y,
                   int16_t &width, int16_t &height) {
    if (count == 1) {
        x = 8; y = 7; width = 304; height = 184;
    } else if (count == 2) {
        x = index == 0 ? 8 : 164; y = 7; width = 148; height = 184;
    } else {
        x = (index % 2) == 0 ? 8 : 164;
        y = index < 2 ? 7 : 101;
        width = 148; height = 88;
    }
}

void create_tile(uint8_t index, uint8_t count, PcDashboardMetric metric) {
    int16_t x, y, width, height;
    tile_geometry(count, index, x, y, width, height);
    lv_obj_t *tile = lv_obj_create(content);
    lv_obj_set_size(tile, width, height);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_bg_color(tile, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = make_label(tile, metric_title(metric),
                                 &lv_font_montserrat_12, muted_color());
    lv_obj_set_pos(title, 10, 7);
    tiles[index].value = make_label(tile, "--", count <= 2 ? &lv_font_montserrat_24
                                                            : &lv_font_montserrat_20,
                                    foreground_color());
    lv_obj_set_pos(tiles[index].value, 10, count <= 2 ? 30 : 27);
    lv_obj_set_width(tiles[index].value, width - 20);
    lv_label_set_long_mode(tiles[index].value, LV_LABEL_LONG_DOT);
    tiles[index].note = make_label(tile, "WAITING", &lv_font_montserrat_12,
                                   muted_color());
    lv_obj_set_width(tiles[index].note, width - 20);
    lv_label_set_long_mode(tiles[index].note, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(tiles[index].note, 10, count <= 2 ? height - 22 : height - 17);

    if (is_graph_metric(metric)) {
        tiles[index].chart = lv_chart_create(tile);
        lv_obj_set_size(tiles[index].chart, width - 20,
                        count <= 2 ? height - 91 : 15);
        lv_obj_set_pos(tiles[index].chart, 10, count <= 2 ? 63 : 51);
        lv_chart_set_type(tiles[index].chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(tiles[index].chart, GRAPH_POINTS);
        lv_chart_set_div_line_count(tiles[index].chart, 0, 0);
        lv_obj_set_style_bg_opa(tiles[index].chart, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(tiles[index].chart, 0, LV_PART_MAIN);
        lv_obj_set_style_line_width(tiles[index].chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(tiles[index].chart, 0, 0, LV_PART_INDICATOR);
        tiles[index].series = lv_chart_add_series(tiles[index].chart,
                                                   accent_color(),
                                                   LV_CHART_AXIS_PRIMARY_Y);
    } else if (metric != PcDashboardMetric::MICROPHONE) {
        tiles[index].bar = lv_bar_create(tile);
        lv_obj_set_size(tiles[index].bar, width - 20, 7);
        lv_obj_set_pos(tiles[index].bar, 10, count <= 2 ? 72 : 57);
        lv_bar_set_range(tiles[index].bar, 0, 100);
        lv_obj_set_style_radius(tiles[index].bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(tiles[index].bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(tiles[index].bar, divider_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(tiles[index].bar, accent_color(), LV_PART_INDICATOR);
    }
}

const char *host_status_text(PcDashboardConnectionState state) {
    switch (state) {
        case PcDashboardConnectionState::LIVE: return "LIVE";
        case PcDashboardConnectionState::STALE: return "STALE";
        case PcDashboardConnectionState::DISCONNECTED: return "PC OFFLINE";
        case PcDashboardConnectionState::NEVER_RECEIVED: return "WAITING FOR PC";
    }
    return "WAITING";
}

const char *performance_status_text(PerformanceConnectionState state) {
    switch (state) {
        case PerformanceConnectionState::LIVE: return "LIVE";
        case PerformanceConnectionState::STALE: return "STALE";
        case PerformanceConnectionState::DISCONNECTED: return "PC OFFLINE";
        case PerformanceConnectionState::NEVER_RECEIVED: return "WAITING FOR PC";
    }
    return "WAITING";
}

void format_bytes(uint64_t bytes, char *out, size_t size) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    uint8_t unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    if (value >= 100.0 || unit == 0) snprintf(out, size, "%.0f %s", value, units[unit]);
    else snprintf(out, size, "%.1f %s", value, units[unit]);
}

bool performance_value(const PerformanceSample &sample, PcDashboardMetric metric,
                       float &value) {
    switch (metric) {
        case PcDashboardMetric::CPU:
            if (!(sample.metric_valid_mask & PERF_METRIC_CPU)) return false;
            value = sample.cpu_pct; return true;
        case PcDashboardMetric::GPU:
            if (!(sample.metric_valid_mask & PERF_METRIC_GPU)) return false;
            value = sample.gpu_pct; return true;
        case PcDashboardMetric::RAM:
            if (!(sample.metric_valid_mask & PERF_METRIC_RAM)) return false;
            value = sample.ram_pct; return true;
        case PcDashboardMetric::NETWORK_DOWN:
            if (!(sample.metric_valid_mask & PERF_METRIC_NETWORK_DOWN)) return false;
            value = static_cast<float>(sample.network_down_bps); return true;
        case PcDashboardMetric::NETWORK_UP:
            if (!(sample.metric_valid_mask & PERF_METRIC_NETWORK_UP)) return false;
            value = static_cast<float>(sample.network_up_bps); return true;
        case PcDashboardMetric::CPU_TEMPERATURE:
            if (!(sample.temperature_valid_mask & PERF_TEMP_CPU)) return false;
            value = sample.cpu_temp_c; return true;
        case PcDashboardMetric::GPU_TEMPERATURE:
            if (!(sample.temperature_valid_mask & PERF_TEMP_GPU)) return false;
            value = sample.gpu_temp_c; return true;
        case PcDashboardMetric::MOTHERBOARD_TEMPERATURE:
            if (!(sample.temperature_valid_mask & PERF_TEMP_MOTHERBOARD)) return false;
            value = sample.motherboard_temp_c; return true;
        default: return false;
    }
}

void update_graph(uint8_t index, PcDashboardMetric metric) {
    TileWidgets &tile = tiles[index];
    if (!tile.chart || !tile.series) return;
    int32_t maximum = 100;
    if (is_temperature_metric(metric)) maximum = 120;
    if (metric == PcDashboardMetric::NETWORK_DOWN ||
        metric == PcDashboardMetric::NETWORK_UP) {
        uint64_t peak = 1;
        const size_t count = performance_data_history_count();
        for (size_t i = 0; i < count; ++i) {
            PerformanceSample sample{};
            float value = 0;
            if (performance_data_get_history(i, sample) &&
                performance_value(sample, metric, value) &&
                static_cast<uint64_t>(value) > peak) peak = static_cast<uint64_t>(value);
        }
        maximum = static_cast<int32_t>(peak > 2000000000ULL ? 2000000000ULL : peak);
    }
    lv_chart_set_range(tile.chart, LV_CHART_AXIS_PRIMARY_Y, 0, maximum);
    for (uint8_t p = 0; p < GRAPH_POINTS; ++p) {
        lv_chart_set_value_by_id(tile.chart, tile.series, p, LV_CHART_POINT_NONE);
    }
    const size_t count = performance_data_history_count();
    const size_t shown = count < GRAPH_POINTS ? count : GRAPH_POINTS;
    const size_t source_offset = count - shown;
    const size_t point_offset = GRAPH_POINTS - shown;
    for (size_t i = 0; i < shown; ++i) {
        PerformanceSample sample{};
        float value = 0;
        if (!performance_data_get_history(source_offset + i, sample) ||
            !performance_value(sample, metric, value)) continue;
        int32_t point = static_cast<int32_t>(value);
        if (point < 0) point = 0;
        if (point > maximum) point = maximum;
        lv_chart_set_value_by_id(tile.chart, tile.series,
                                 point_offset + i, point);
    }
    lv_chart_refresh(tile.chart);
}

void update_tile(uint8_t index, PcDashboardMetric metric,
                 const PcDashboardSnapshot &snapshot) {
    TileWidgets &tile = tiles[index];
    if (!tile.value || !tile.note) return;
    char value_text[48] = "--";
    const char *note = is_host_metric(metric)
        ? host_status_text(snapshot.host_state)
        : performance_status_text(snapshot.performance_state);
    int32_t bar_value = 0;
    bool available = false;

    if (metric == PcDashboardMetric::BATTERY && snapshot.has_host_sample &&
        (snapshot.host.valid_mask & PC_DASH_HOST_BATTERY)) {
        snprintf(value_text, sizeof(value_text), "%.0f%%",
                 snapshot.host.battery_pct);
        note = snapshot.host.charging ? "CHARGING" : note;
        bar_value = static_cast<int32_t>(snapshot.host.battery_pct);
        available = true;
    } else if (metric == PcDashboardMetric::MICROPHONE && snapshot.has_host_sample &&
               (snapshot.host.valid_mask & PC_DASH_HOST_MICROPHONE)) {
        snprintf(value_text, sizeof(value_text), "%s",
                 snapshot.host.microphone_in_use ? "IN USE" : "OFF");
        note = snapshot.host.microphone_in_use ? "MIC ACTIVE" : note;
        available = true;
    } else if (metric == PcDashboardMetric::STORAGE && snapshot.has_host_sample &&
               (snapshot.host.valid_mask & PC_DASH_HOST_STORAGE)) {
        const double pct = snapshot.host.storage_total_bytes == 0 ? 0.0 :
            (100.0 * static_cast<double>(snapshot.host.storage_used_bytes) /
             static_cast<double>(snapshot.host.storage_total_bytes));
        snprintf(value_text, sizeof(value_text), "%.0f%%", pct);
        char used[20], total[20];
        format_bytes(snapshot.host.storage_used_bytes, used, sizeof(used));
        format_bytes(snapshot.host.storage_total_bytes, total, sizeof(total));
        static char storage_note[44];
        snprintf(storage_note, sizeof(storage_note), "%s / %s", used, total);
        note = storage_note;
        bar_value = static_cast<int32_t>(pct);
        available = true;
    } else if (snapshot.has_performance_sample) {
        float value = 0;
        if (performance_value(snapshot.performance, metric, value)) {
            available = true;
            if (is_percentage_metric(metric)) {
                snprintf(value_text, sizeof(value_text), "%.0f%%", value);
                bar_value = static_cast<int32_t>(value);
            } else if (is_temperature_metric(metric)) {
                snprintf(value_text, sizeof(value_text), "%.0f C", value);
            } else {
                format_bytes(static_cast<uint64_t>(value), value_text,
                             sizeof(value_text));
                strncat(value_text, "/s",
                        sizeof(value_text) - strlen(value_text) - 1);
            }
        }
    }

    lv_label_set_text(tile.value, value_text);
    lv_label_set_text(tile.note, available ? note : "NOT AVAILABLE");
    lv_obj_set_style_text_color(tile.value,
        available ? foreground_color() : muted_color(), LV_PART_MAIN);
    if (tile.bar) lv_bar_set_value(tile.bar, bar_value, LV_ANIM_OFF);
    if (tile.chart) update_graph(index, metric);
}

void refresh_dashboard() {
    if (!screen || current_view != DashboardView::DASHBOARD) return;
    PcDashboardSnapshot snapshot{};
    pc_dashboard_capture(millis(), snapshot);
    known_revision = snapshot.revision;
    for (uint8_t i = 0; i < snapshot.config.count; ++i) {
        update_tile(i, snapshot.config.metrics[i], snapshot);
    }
}

void render_dashboard() {
    prepare_content(false);
    lv_obj_clear_flag(header_action, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *label = lv_obj_get_child(header_action, 0);
    if (label) lv_label_set_text(label, "EDIT");
    PcDashboardSnapshot snapshot{};
    pc_dashboard_capture(millis(), snapshot);
    known_revision = snapshot.revision;
    for (uint8_t i = 0; i < snapshot.config.count; ++i) {
        create_tile(i, snapshot.config.count, snapshot.config.metrics[i]);
    }
    refresh_dashboard();
}

void render_picker() {
    prepare_content(true);
    lv_obj_add_flag(header_action, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(heading, "CUSTOMISE");

    selection_count = make_label(content, "", &lv_font_montserrat_12,
                                 transient_message[0] ? lv_color_hex(0xE25555)
                                                      : muted_color());
    char count_text[64];
    if (transient_message[0]) snprintf(count_text, sizeof(count_text), "%s", transient_message);
    else snprintf(count_text, sizeof(count_text), "%u OF 4 SELECTED",
                  static_cast<unsigned>(editing_config.count));
    lv_label_set_text(selection_count, count_text);
    lv_obj_set_pos(selection_count, 10, 7);

    const uint8_t metric_count = static_cast<uint8_t>(PcDashboardMetric::COUNT);
    for (uint8_t raw = 0; raw < metric_count; ++raw) {
        const PcDashboardMetric metric = static_cast<PcDashboardMetric>(raw);
        const int16_t y = static_cast<int16_t>(29 + raw * 42);
        lv_obj_t *button = lv_button_create(content);
        picker_buttons[raw] = button;
        style_button(button, 8);
        lv_obj_set_size(button, 304, 36);
        lv_obj_set_pos(button, 8, y);
        lv_obj_add_event_cb(button, on_picker_toggle, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(metric)));
        lv_obj_t *name = make_label(button, metric_title(metric),
                                    &lv_font_montserrat_14, foreground_color());
        lv_obj_set_pos(name, 12, 10);
        const uint8_t order = selection_index(metric);
        lv_obj_t *badge = lv_obj_create(button);
        picker_badges[raw] = badge;
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 26, 24);
        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -7, 0);
        lv_obj_set_style_bg_color(badge,
            order == 0xFF ? divider_color() : accent_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 12, LV_PART_MAIN);
        char badge_text[4];
        snprintf(badge_text, sizeof(badge_text), order == 0xFF ? "+" : "%u",
                 static_cast<unsigned>(order + 1));
        lv_obj_t *badge_label = make_label(badge, badge_text,
                                           &lv_font_montserrat_12,
                                           order == 0xFF ? foreground_color()
                                                         : background_color());
        lv_obj_center(badge_label);
    }

    const int16_t action_y = static_cast<int16_t>(29 + metric_count * 42 + 7);
    lv_obj_t *cancel = lv_button_create(content);
    style_button(cancel, 9);
    lv_obj_set_size(cancel, 146, 42);
    lv_obj_set_pos(cancel, 8, action_y);
    lv_obj_t *cancel_label = make_label(cancel, "CANCEL",
                                        &lv_font_montserrat_14,
                                        foreground_color());
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, on_home, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *save = lv_button_create(content);
    style_button(save, 9);
    lv_obj_set_size(save, 146, 42);
    lv_obj_set_pos(save, 166, action_y);
    lv_obj_set_style_bg_color(save, accent_color(), LV_PART_MAIN);
    lv_obj_t *save_label = make_label(save, "SAVE",
                                      &lv_font_montserrat_14,
                                      background_color());
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save, on_picker_save, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *end = lv_obj_create(content);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, 0, action_y + 50);
}

void render() {
    if (!screen) return;
    lv_label_set_text(heading,
        current_view == DashboardView::CUSTOMISE ? "CUSTOMISE" : "PC DASHBOARD");
    lv_obj_t *home = lv_obj_get_child(screen, 0);
    if (home) {
        lv_obj_t *label = lv_obj_get_child(home, 0);
        if (label) lv_label_set_text(label,
            current_view == DashboardView::CUSTOMISE ? "BACK" : "HOME");
    }
    if (current_view == DashboardView::CUSTOMISE) render_picker();
    else render_dashboard();
}

} // namespace

void ui_pc_dashboard_set_navigation_callback(
    UiPcDashboardNavigationCallback callback, void *context) {
    home_callback = callback;
    navigation_context = context;
}

lv_obj_t *ui_pc_dashboard_create() {
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

lv_obj_t *ui_pc_dashboard_screen() { return screen; }

lv_obj_t *ui_pc_dashboard_recreate() {
    const DashboardView saved_view = current_view;
    const PcDashboardConfig saved_editing = editing_config;
    const uint32_t saved_revision = editing_revision;
    lv_obj_t *old = screen;
    const bool active = old && lv_screen_active() == old;
    screen = nullptr;
    content = nullptr;
    heading = nullptr;
    header_action = nullptr;
    lv_obj_t *replacement = ui_pc_dashboard_create();
    current_view = saved_view;
    editing_config = saved_editing;
    editing_revision = saved_revision;
    render();
    if (active) lv_screen_load(replacement);
    if (old && old != replacement) lv_obj_delete(old);
    return replacement;
}

void ui_pc_dashboard_tick() {
    if (!screen || lv_screen_active() != screen) return;
    const uint32_t now = millis();
    if (now - last_refresh_ms < REFRESH_INTERVAL_MS) return;
    last_refresh_ms = now;
    if (current_view == DashboardView::CUSTOMISE) return;
    PcDashboardSnapshot snapshot{};
    pc_dashboard_capture(now, snapshot);
    if (snapshot.revision != known_revision) render_dashboard();
    else refresh_dashboard();
}

#else

void ui_pc_dashboard_set_navigation_callback(
    UiPcDashboardNavigationCallback, void *) {}
lv_obj_t *ui_pc_dashboard_create() { return nullptr; }
lv_obj_t *ui_pc_dashboard_screen() { return nullptr; }
lv_obj_t *ui_pc_dashboard_recreate() { return nullptr; }
void ui_pc_dashboard_tick() {}

#endif // ECHODESK_OS
