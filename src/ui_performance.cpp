#include "ui_performance.h"

#if defined(ECHODESK_OS)

#include "config_store.h"
#include "performance_data.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

enum class PerformanceView : uint8_t {
    SELECTOR = 0,
    ALL_USAGE = 1,
    CPU_USAGE,
    GPU_USAGE,
    MEMORY_USAGE,
    NETWORK,
    ALL_TEMPERATURES,
    CPU_TEMPERATURE,
    GPU_TEMPERATURE,
    MOTHERBOARD_TEMPERATURE,
    THERMAL_PRESSURE,
};

enum class SelectorTab : uint8_t {
    USAGE,
    TEMPERATURE,
};

constexpr uint8_t FIRST_VIEW = static_cast<uint8_t>(PerformanceView::ALL_USAGE);
constexpr uint8_t LAST_VIEW = static_cast<uint8_t>(PerformanceView::THERMAL_PRESSURE);
constexpr uint32_t REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t NAVIGATION_GUARD_MS = 250;

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *heading_label = nullptr;
lv_obj_t *freshness_label = nullptr;
lv_obj_t *primary_value_label = nullptr;
lv_obj_t *secondary_value_label = nullptr;
lv_obj_t *chart = nullptr;
lv_chart_series_t *chart_primary = nullptr;
lv_chart_series_t *chart_secondary = nullptr;
lv_obj_t *tile_values[4] = {};
lv_obj_t *tile_notes[4] = {};
lv_obj_t *tile_charts[4] = {};
lv_chart_series_t *tile_primary[4] = {};
lv_chart_series_t *tile_secondary[4] = {};

PerformanceView current_view = PerformanceView::ALL_USAGE;
PerformanceView last_metric_view = PerformanceView::ALL_USAGE;
SelectorTab selector_tab = SelectorTab::USAGE;
UiPerformanceNavigationCallback home_callback = nullptr;
UiPerformanceNavigationCallback back_callback = nullptr;
void *navigation_context = nullptr;
uint32_t last_refresh_ms = 0;
uint32_t last_navigation_ms = 0;
float smoothed_pressure = 0.0f;
ThermalLevel displayed_pressure_level = ThermalLevel::NORMAL;
bool pressure_initialized = false;
uint32_t pressure_sample_ms = 0;

lv_color_t background_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000);
}

lv_color_t foreground_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
}

lv_color_t panel_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xF0F0F0) : lv_color_hex(0x101010);
}

lv_color_t pressed_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xD8D8D8) : lv_color_hex(0x282828);
}

lv_color_t muted_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0x555555) : lv_color_hex(0xA0A0A0);
}

lv_color_t divider_color() {
    return g_config.theme == THEME_LIGHT ? lv_color_hex(0xBEBEBE) : lv_color_hex(0x3C3C3C);
}

bool navigation_allowed() {
    const uint32_t now = millis();
    if (now - last_navigation_ms < NAVIGATION_GUARD_MS) return false;
    last_navigation_ms = now;
    return true;
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
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, pressed_color(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, foreground_color(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
}

const char *view_title(PerformanceView view) {
    switch (view) {
        case PerformanceView::ALL_USAGE: return "ALL USAGE";
        case PerformanceView::CPU_USAGE: return "CPU USAGE";
        case PerformanceView::GPU_USAGE: return "GPU USAGE";
        case PerformanceView::MEMORY_USAGE: return "MEMORY";
        case PerformanceView::NETWORK: return "NETWORK";
        case PerformanceView::ALL_TEMPERATURES: return "ALL TEMPS";
        case PerformanceView::CPU_TEMPERATURE: return "CPU TEMP";
        case PerformanceView::GPU_TEMPERATURE: return "GPU TEMP";
        case PerformanceView::MOTHERBOARD_TEMPERATURE: return "BOARD TEMP";
        case PerformanceView::THERMAL_PRESSURE: return "PRESSURE";
        case PerformanceView::SELECTOR: return "PERFORMANCE";
    }
    return "PERFORMANCE";
}

bool is_temperature_view(PerformanceView view) {
    return view >= PerformanceView::ALL_TEMPERATURES;
}

PerformanceView configured_default_view() {
    switch (g_config.performance_default_view) {
        case PERFORMANCE_VIEW_CPU: return PerformanceView::CPU_USAGE;
        case PERFORMANCE_VIEW_GPU: return PerformanceView::GPU_USAGE;
        case PERFORMANCE_VIEW_MEMORY: return PerformanceView::MEMORY_USAGE;
        default: return PerformanceView::ALL_USAGE;
    }
}

void clear_widget_references() {
    freshness_label = nullptr;
    primary_value_label = nullptr;
    secondary_value_label = nullptr;
    chart = nullptr;
    chart_primary = nullptr;
    chart_secondary = nullptr;
    for (uint8_t i = 0; i < 4; ++i) {
        tile_values[i] = nullptr;
        tile_notes[i] = nullptr;
        tile_charts[i] = nullptr;
        tile_primary[i] = nullptr;
        tile_secondary[i] = nullptr;
    }
}

void on_screen_deleted(lv_event_t *event) {
    if (lv_event_get_target(event) == screen) {
        screen = nullptr;
        content = nullptr;
        heading_label = nullptr;
        clear_widget_references();
    }
}

void render_current_view();
void refresh_current_view();

void invoke_home(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (home_callback) home_callback(navigation_context);
}

void invoke_back(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    if (current_view != PerformanceView::SELECTOR) {
        selector_tab = is_temperature_view(current_view) ? SelectorTab::TEMPERATURE : SelectorTab::USAGE;
        current_view = PerformanceView::SELECTOR;
        render_current_view();
        return;
    }
    if (back_callback) back_callback(navigation_context);
}

lv_obj_t *make_header_button(const char *text, int16_t x, lv_event_cb_t callback) {
    lv_obj_t *button = lv_button_create(screen);
    style_button(button, 7);
    lv_obj_set_size(button, 62, 30);
    lv_obj_set_pos(button, x, 5);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_12, foreground_color());
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    return button;
}

void create_header() {
    make_header_button("HOME", 6, invoke_home);
    make_header_button("BACK", 252, invoke_back);
    heading_label = make_label(screen, "PERFORMANCE", &lv_font_montserrat_14, foreground_color());
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

void create_content() {
    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 320, 200);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
}

void make_freshness_label() {
    freshness_label = make_label(content, "NO DATA", &lv_font_montserrat_12, muted_color());
    lv_obj_align(freshness_label, LV_ALIGN_TOP_RIGHT, -9, 6);
}

const char *connection_text(PerformanceConnectionState state) {
    switch (state) {
        case PerformanceConnectionState::LIVE: return "LIVE";
        case PerformanceConnectionState::STALE: return "STALE";
        case PerformanceConnectionState::DISCONNECTED: return "OFFLINE";
        case PerformanceConnectionState::NEVER_RECEIVED: return "NO DATA";
    }
    return "NO DATA";
}

void update_freshness() {
    if (!freshness_label) return;
    const PerformanceConnectionState state = performance_data_connection_state(millis());
    lv_label_set_text(freshness_label, connection_text(state));
    lv_obj_set_style_text_color(freshness_label,
                                state == PerformanceConnectionState::LIVE ? foreground_color() : muted_color(),
                                LV_PART_MAIN);
}

void on_selector_tab(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    selector_tab = static_cast<SelectorTab>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    render_current_view();
}

void on_view_selected(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !navigation_allowed()) return;
    const PerformanceView selected = static_cast<PerformanceView>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    const uint8_t raw = static_cast<uint8_t>(selected);
    if (raw < FIRST_VIEW || raw > LAST_VIEW) return;
    current_view = selected;
    last_metric_view = selected;
    render_current_view();
}

void make_tab(const char *text, SelectorTab tab, int16_t x) {
    lv_obj_t *button = lv_button_create(content);
    style_button(button, 8);
    lv_obj_set_size(button, 146, 34);
    lv_obj_set_pos(button, x, 7);
    if (selector_tab == tab) {
        lv_obj_set_style_bg_color(button, foreground_color(), LV_PART_MAIN);
        lv_obj_set_style_border_color(button, foreground_color(), LV_PART_MAIN);
    }
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_12,
                                 selector_tab == tab ? background_color() : foreground_color());
    lv_obj_center(label);
    lv_obj_add_event_cb(button, on_selector_tab, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(tab)));
}

void make_view_button(PerformanceView view, int16_t x, int16_t y) {
    lv_obj_t *button = lv_button_create(content);
    style_button(button, 9);
    lv_obj_set_size(button, 146, 43);
    lv_obj_set_pos(button, x, y);
    if (view == last_metric_view) {
        lv_obj_set_style_border_color(button, foreground_color(), LV_PART_MAIN);
    }
    lv_obj_t *label = make_label(button, view_title(view), &lv_font_montserrat_12, foreground_color());
    lv_obj_center(label);
    lv_obj_add_event_cb(button, on_view_selected, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(view)));
}

void render_selector() {
    make_tab("USAGE", SelectorTab::USAGE, 8);
    make_tab("TEMPERATURE", SelectorTab::TEMPERATURE, 166);

    if (selector_tab == SelectorTab::USAGE) {
        make_view_button(PerformanceView::ALL_USAGE, 8, 49);
        make_view_button(PerformanceView::CPU_USAGE, 166, 49);
        make_view_button(PerformanceView::GPU_USAGE, 8, 98);
        make_view_button(PerformanceView::MEMORY_USAGE, 166, 98);
        make_view_button(PerformanceView::NETWORK, 8, 147);
    } else {
        make_view_button(PerformanceView::ALL_TEMPERATURES, 8, 49);
        make_view_button(PerformanceView::CPU_TEMPERATURE, 166, 49);
        make_view_button(PerformanceView::GPU_TEMPERATURE, 8, 98);
        make_view_button(PerformanceView::MOTHERBOARD_TEMPERATURE, 166, 98);
        make_view_button(PerformanceView::THERMAL_PRESSURE, 8, 147);
    }
}

void create_dashboard_tile(uint8_t index, int16_t x, int16_t y, const char *title) {
    lv_obj_t *tile = lv_obj_create(content);
    lv_obj_set_size(tile, 148, 82);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_bg_color(tile, panel_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = make_label(tile, title, &lv_font_montserrat_12, muted_color());
    lv_obj_set_pos(name, 7, 4);
    tile_notes[index] = make_label(tile, "WAITING", &lv_font_montserrat_12, muted_color());
    lv_obj_align(tile_notes[index], LV_ALIGN_TOP_RIGHT, -7, 4);
    tile_values[index] = make_label(tile, "--", &lv_font_montserrat_16, foreground_color());
    lv_obj_set_pos(tile_values[index], 7, 22);

    tile_charts[index] = lv_chart_create(tile);
    lv_obj_set_size(tile_charts[index], 134, 34);
    lv_obj_set_pos(tile_charts[index], 7, 43);
    lv_obj_set_style_bg_color(tile_charts[index], background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile_charts[index], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile_charts[index], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tile_charts[index], 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(tile_charts[index], divider_color(), LV_PART_MAIN);
    lv_obj_set_style_line_width(tile_charts[index], 1, LV_PART_MAIN);
    lv_obj_set_style_size(tile_charts[index], 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(tile_charts[index], 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(tile_charts[index], 2, LV_PART_MAIN);
    lv_chart_set_type(tile_charts[index], LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(tile_charts[index], PERFORMANCE_HISTORY_CAPACITY);
    lv_chart_set_div_line_count(tile_charts[index], 2, 3);
    tile_primary[index] = lv_chart_add_series(tile_charts[index], foreground_color(),
                                               LV_CHART_AXIS_PRIMARY_Y);
}

void render_usage_dashboard() {
    create_dashboard_tile(0, 8, 8, "CPU");
    create_dashboard_tile(1, 164, 8, "GPU");
    create_dashboard_tile(2, 8, 98, "MEMORY");
    create_dashboard_tile(3, 164, 98, "NETWORK");
    tile_secondary[3] = lv_chart_add_series(tile_charts[3], muted_color(),
                                             LV_CHART_AXIS_PRIMARY_Y);
}

void render_temperature_dashboard() {
    create_dashboard_tile(0, 8, 8, "CPU");
    create_dashboard_tile(1, 164, 8, "GPU");
    create_dashboard_tile(2, 8, 98, "BOARD");
    create_dashboard_tile(3, 164, 98, "PRESSURE");
}

void style_chart(lv_obj_t *widget) {
    lv_obj_set_style_bg_color(widget, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(widget, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(widget, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(widget, 7, LV_PART_MAIN);
    lv_obj_set_style_line_color(widget, divider_color(), LV_PART_MAIN);
    lv_obj_set_style_line_width(widget, 1, LV_PART_MAIN);
    lv_obj_set_style_size(widget, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(widget, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(widget, 5, LV_PART_MAIN);
    lv_chart_set_type(widget, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(widget, PERFORMANCE_HISTORY_CAPACITY);
    lv_chart_set_div_line_count(widget, 3, 4);
    lv_chart_set_update_mode(widget, LV_CHART_UPDATE_MODE_SHIFT);
}

void render_graph(bool dual_series) {
    make_freshness_label();
    primary_value_label = make_label(content, "--", &lv_font_montserrat_24, foreground_color());
    lv_obj_set_pos(primary_value_label, 9, 3);
    secondary_value_label = make_label(content, "", &lv_font_montserrat_12, muted_color());
    lv_obj_set_pos(secondary_value_label, 9, 31);

    chart = lv_chart_create(content);
    lv_obj_set_size(chart, 304, 143);
    lv_obj_set_pos(chart, 8, 51);
    style_chart(chart);
    chart_primary = lv_chart_add_series(chart, foreground_color(), LV_CHART_AXIS_PRIMARY_Y);
    if (dual_series) {
        chart_secondary = lv_chart_add_series(chart, muted_color(), LV_CHART_AXIS_PRIMARY_Y);
    }
}

void render_pressure() {
    make_freshness_label();
    primary_value_label = make_label(content, "NO DATA", &lv_font_montserrat_36, foreground_color());
    lv_obj_set_width(primary_value_label, 302);
    lv_obj_set_style_text_align(primary_value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(primary_value_label, 9, 36);
    secondary_value_label = make_label(content, "WAITING FOR TEMPERATURE DATA",
                                       &lv_font_montserrat_14, muted_color());
    lv_obj_set_width(secondary_value_label, 302);
    lv_obj_set_style_text_align(secondary_value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(secondary_value_label, 9, 85);

    chart = lv_chart_create(content);
    lv_obj_set_size(chart, 304, 75);
    lv_obj_set_pos(chart, 8, 119);
    style_chart(chart);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart, 4, 4);
    chart_primary = lv_chart_add_series(chart, foreground_color(), LV_CHART_AXIS_PRIMARY_Y);
}

void render_current_view() {
    if (!screen) return;
    if (content) lv_obj_delete(content);
    clear_widget_references();
    create_content();
    lv_label_set_text(heading_label, view_title(current_view));

    switch (current_view) {
        case PerformanceView::SELECTOR:
            render_selector();
            break;
        case PerformanceView::ALL_USAGE:
            render_usage_dashboard();
            break;
        case PerformanceView::ALL_TEMPERATURES:
            render_temperature_dashboard();
            break;
        case PerformanceView::NETWORK:
            render_graph(true);
            break;
        case PerformanceView::THERMAL_PRESSURE:
            render_pressure();
            break;
        default:
            render_graph(false);
            break;
    }
    last_refresh_ms = 0;
    refresh_current_view();
}

void set_percent_text(lv_obj_t *label, float value) {
    if (!label) return;
    char text[16];
    if (isfinite(value)) snprintf(text, sizeof(text), "%.0f%%", value);
    else snprintf(text, sizeof(text), "N/A");
    lv_label_set_text(label, text);
}

void set_temperature_text(lv_obj_t *label, float value, bool valid) {
    if (!label) return;
    char text[16];
    if (valid && isfinite(value)) snprintf(text, sizeof(text), "%.0f\xC2\xB0" "C", value);
    else snprintf(text, sizeof(text), "N/A");
    lv_label_set_text(label, text);
}

void format_network_rate(uint64_t bytes_per_second, char *buffer, size_t length) {
    static const char *units[] = {"bps", "Kbps", "Mbps", "Gbps"};
    float value = static_cast<float>(bytes_per_second) * 8.0f;
    uint8_t unit = 0;
    while (value >= 1000.0f && unit < 3) {
        value /= 1000.0f;
        ++unit;
    }
    if (value >= 100.0f || unit == 0) snprintf(buffer, length, "%.0f %s", value, units[unit]);
    else if (value >= 10.0f) snprintf(buffer, length, "%.1f %s", value, units[unit]);
    else snprintf(buffer, length, "%.2f %s", value, units[unit]);
}

void format_network_rate_compact(uint64_t bytes_per_second, char *buffer, size_t length) {
    const float bits = static_cast<float>(bytes_per_second) * 8.0f;
    if (bits >= 1000000000.0f) snprintf(buffer, length, "%.1fG", bits / 1000000000.0f);
    else if (bits >= 1000000.0f) snprintf(buffer, length, "%.1fM", bits / 1000000.0f);
    else if (bits >= 1000.0f) snprintf(buffer, length, "%.0fK", bits / 1000.0f);
    else snprintf(buffer, length, "%.0f", bits);
}

ThermalLevel level_for_temperature(float temperature, uint8_t component) {
    if (!isfinite(temperature)) return ThermalLevel::NORMAL;
    const ThermalThresholds thresholds = performance_thermal_thresholds(component);
    if (temperature >= thresholds.critical_c) return ThermalLevel::CRITICAL;
    if (temperature >= thresholds.hot_c) return ThermalLevel::HOT;
    if (temperature >= thresholds.warm_c) return ThermalLevel::WARM;
    return ThermalLevel::NORMAL;
}

const char *pressure_text(ThermalLevel level) {
    switch (level) {
        case ThermalLevel::NORMAL: return "GOOD";
        case ThermalLevel::WARM: return "MODERATE";
        case ThermalLevel::HOT:
        case ThermalLevel::CRITICAL: return "HIGH";
    }
    return "UNKNOWN";
}

lv_color_t pressure_color(ThermalLevel level) {
    switch (level) {
        case ThermalLevel::NORMAL: return lv_color_hex(0x31D158);
        case ThermalLevel::WARM: return lv_color_hex(0xFFD60A);
        case ThermalLevel::HOT:
        case ThermalLevel::CRITICAL: return lv_color_hex(0xFF453A);
    }
    return foreground_color();
}

uint8_t pressure_component(const PerformanceSample &sample) {
    uint8_t component = 0;
    float highest = -1.0f;
    const uint8_t components[] = {PERF_TEMP_CPU, PERF_TEMP_GPU, PERF_TEMP_MOTHERBOARD};
    const float values[] = {sample.cpu_temp_c, sample.gpu_temp_c, sample.motherboard_temp_c};
    for (uint8_t i = 0; i < 3; ++i) {
        if (!(sample.temperature_valid_mask & components[i])) continue;
        const float pressure = performance_thermal_pressure(
            values[i], performance_thermal_thresholds(components[i]));
        if (pressure > highest) {
            highest = pressure;
            component = components[i];
        }
    }
    return component;
}

const char *pressure_component_name(uint8_t component) {
    switch (component) {
        case PERF_TEMP_CPU: return "CPU";
        case PERF_TEMP_GPU: return "GPU";
        case PERF_TEMP_MOTHERBOARD: return "MOTHERBOARD";
        default: return "NO SENSOR";
    }
}

void update_pressure_state(const PerformanceSample &sample) {
    if (sample.temperature_valid_mask == 0 || sample.received_ms == pressure_sample_ms) return;
    const float raw = performance_highest_thermal_pressure(sample);
    smoothed_pressure = pressure_initialized
        ? performance_smooth_pressure(smoothed_pressure, raw)
        : raw;
    displayed_pressure_level = performance_thermal_level_with_hysteresis(
        smoothed_pressure, displayed_pressure_level);
    pressure_initialized = true;
    pressure_sample_ms = sample.received_ms;
}

bool has_metric(const PerformanceSample &sample, PerformanceView view) {
    switch (view) {
        case PerformanceView::CPU_USAGE:
            return (sample.metric_valid_mask & PERF_METRIC_CPU) != 0;
        case PerformanceView::GPU_USAGE:
            return (sample.metric_valid_mask & PERF_METRIC_GPU) != 0;
        case PerformanceView::MEMORY_USAGE:
            return (sample.metric_valid_mask & PERF_METRIC_RAM) != 0;
        default:
            return false;
    }
}

bool has_temperature(const PerformanceSample &sample, PerformanceView view) {
    switch (view) {
        case PerformanceView::CPU_TEMPERATURE:
            return (sample.temperature_valid_mask & PERF_TEMP_CPU) != 0;
        case PerformanceView::GPU_TEMPERATURE:
            return (sample.temperature_valid_mask & PERF_TEMP_GPU) != 0;
        case PerformanceView::MOTHERBOARD_TEMPERATURE:
            return (sample.temperature_valid_mask & PERF_TEMP_MOTHERBOARD) != 0;
        default:
            return false;
    }
}

float sample_value(const PerformanceSample &sample, PerformanceView view) {
    switch (view) {
        case PerformanceView::CPU_USAGE: return sample.cpu_pct;
        case PerformanceView::GPU_USAGE: return sample.gpu_pct;
        case PerformanceView::MEMORY_USAGE: return sample.ram_pct;
        case PerformanceView::CPU_TEMPERATURE: return sample.cpu_temp_c;
        case PerformanceView::GPU_TEMPERATURE: return sample.gpu_temp_c;
        case PerformanceView::MOTHERBOARD_TEMPERATURE: return sample.motherboard_temp_c;
        default: return NAN;
    }
}

void set_tile_note(uint8_t index, const char *text) {
    if (tile_notes[index]) lv_label_set_text(tile_notes[index], text);
}

void clear_tile_chart(uint8_t index) {
    if (!tile_charts[index] || !tile_primary[index]) return;
    for (uint32_t point = 0; point < PERFORMANCE_HISTORY_CAPACITY; ++point) {
        lv_chart_set_value_by_id(tile_charts[index], tile_primary[index], point,
                                 LV_CHART_POINT_NONE);
        if (tile_secondary[index]) {
            lv_chart_set_value_by_id(tile_charts[index], tile_secondary[index], point,
                                     LV_CHART_POINT_NONE);
        }
    }
}

bool sample_has_tile_metric(const PerformanceSample &sample, uint8_t index,
                            bool temperatures) {
    if (temperatures) {
        const uint8_t bits[] = {PERF_TEMP_CPU, PERF_TEMP_GPU, PERF_TEMP_MOTHERBOARD};
        return index < 3 ? (sample.temperature_valid_mask & bits[index]) != 0
                         : sample.temperature_valid_mask != 0;
    }
    const uint8_t bits[] = {PERF_METRIC_CPU, PERF_METRIC_GPU, PERF_METRIC_RAM};
    return index < 3 ? (sample.metric_valid_mask & bits[index]) != 0
                     : (sample.metric_valid_mask &
                        (PERF_METRIC_NETWORK_DOWN | PERF_METRIC_NETWORK_UP)) != 0;
}

float sample_tile_metric(const PerformanceSample &sample, uint8_t index,
                         bool temperatures) {
    if (temperatures) {
        switch (index) {
            case 0: return sample.cpu_temp_c;
            case 1: return sample.gpu_temp_c;
            case 2: return sample.motherboard_temp_c;
            default: return performance_highest_thermal_pressure(sample) * 100.0f;
        }
    }
    switch (index) {
        case 0: return sample.cpu_pct;
        case 1: return sample.gpu_pct;
        case 2: return sample.ram_pct;
        default: return NAN;
    }
}

uint64_t network_history_ceiling();

void update_dashboard_charts(bool temperatures) {
    const size_t count = performance_data_history_count();
    const size_t offset = count < PERFORMANCE_HISTORY_CAPACITY
        ? PERFORMANCE_HISTORY_CAPACITY - count : 0;
    const uint64_t network_ceiling = temperatures ? 1 : network_history_ceiling();

    for (uint8_t tile = 0; tile < 4; ++tile) {
        clear_tile_chart(tile);
        if (temperatures) {
            lv_chart_set_range(tile_charts[tile], LV_CHART_AXIS_PRIMARY_Y,
                               tile >= 2 ? 0 : 20, tile == 3 ? 100 : 110);
        } else {
            lv_chart_set_range(tile_charts[tile], LV_CHART_AXIS_PRIMARY_Y, 0,
                               tile == 3 ? 1000 : 100);
        }
    }

    PerformanceSample sample = {};
    for (size_t i = 0; i < count && i < PERFORMANCE_HISTORY_CAPACITY; ++i) {
        if (!performance_data_get_history(i, sample)) continue;
        for (uint8_t tile = 0; tile < 4; ++tile) {
            if (!sample_has_tile_metric(sample, tile, temperatures)) continue;
            if (!temperatures && tile == 3) {
                if (sample.metric_valid_mask & PERF_METRIC_NETWORK_DOWN) {
                    const int32_t down = static_cast<int32_t>(
                        (static_cast<double>(sample.network_down_bps) /
                         static_cast<double>(network_ceiling)) * 1000.0);
                    lv_chart_set_value_by_id(tile_charts[tile], tile_primary[tile],
                                             offset + i, down);
                }
                if ((sample.metric_valid_mask & PERF_METRIC_NETWORK_UP) && tile_secondary[tile]) {
                    const int32_t up = static_cast<int32_t>(
                        (static_cast<double>(sample.network_up_bps) /
                         static_cast<double>(network_ceiling)) * 1000.0);
                    lv_chart_set_value_by_id(tile_charts[tile], tile_secondary[tile],
                                             offset + i, up);
                }
                continue;
            }
            const float value = sample_tile_metric(sample, tile, temperatures);
            if (isfinite(value)) {
                lv_chart_set_value_by_id(tile_charts[tile], tile_primary[tile],
                                         offset + i, static_cast<int32_t>(lroundf(value)));
            }
        }
    }
    for (uint8_t tile = 0; tile < 4; ++tile) lv_chart_refresh(tile_charts[tile]);
}

void update_usage_dashboard(const PerformanceSample &latest, bool available) {
    if (!available) {
        const char *state = connection_text(performance_data_connection_state(millis()));
        for (uint8_t i = 0; i < 4; ++i) {
            lv_label_set_text(tile_values[i], "--");
            set_tile_note(i, state);
        }
        update_dashboard_charts(false);
        return;
    }
    set_percent_text(tile_values[0], has_metric(latest, PerformanceView::CPU_USAGE) ? latest.cpu_pct : NAN);
    set_percent_text(tile_values[1], has_metric(latest, PerformanceView::GPU_USAGE) ? latest.gpu_pct : NAN);
    set_percent_text(tile_values[2], has_metric(latest, PerformanceView::MEMORY_USAGE) ? latest.ram_pct : NAN);
    set_tile_note(0, has_metric(latest, PerformanceView::CPU_USAGE) ? "CURRENT" : "N/A");
    set_tile_note(1, has_metric(latest, PerformanceView::GPU_USAGE) ? "CURRENT" : "N/A");
    set_tile_note(2, has_metric(latest, PerformanceView::MEMORY_USAGE) ? "CURRENT" : "N/A");

    char down[20], up[20], combined[34];
    const bool down_valid = (latest.metric_valid_mask & PERF_METRIC_NETWORK_DOWN) != 0;
    const bool up_valid = (latest.metric_valid_mask & PERF_METRIC_NETWORK_UP) != 0;
    if (down_valid) format_network_rate_compact(latest.network_down_bps, down, sizeof(down));
    else snprintf(down, sizeof(down), "N/A");
    if (up_valid) format_network_rate_compact(latest.network_up_bps, up, sizeof(up));
    else snprintf(up, sizeof(up), "N/A");
    snprintf(combined, sizeof(combined), "D %s  U %s", down, up);
    lv_label_set_text(tile_values[3], combined);
    lv_obj_set_style_text_font(tile_values[3], &lv_font_montserrat_12, LV_PART_MAIN);
    set_tile_note(3, "BITS/S");
    update_dashboard_charts(false);
}

void update_temperature_dashboard(const PerformanceSample &latest, bool available) {
    if (!available) {
        const char *state = connection_text(performance_data_connection_state(millis()));
        for (uint8_t i = 0; i < 4; ++i) {
            lv_label_set_text(tile_values[i], "--");
            set_tile_note(i, state);
        }
        if (tile_values[3]) lv_obj_set_style_text_color(tile_values[3], foreground_color(), LV_PART_MAIN);
        update_dashboard_charts(true);
        return;
    }
    const bool cpu_valid = (latest.temperature_valid_mask & PERF_TEMP_CPU) != 0;
    const bool gpu_valid = (latest.temperature_valid_mask & PERF_TEMP_GPU) != 0;
    const bool board_valid = (latest.temperature_valid_mask & PERF_TEMP_MOTHERBOARD) != 0;
    set_temperature_text(tile_values[0], latest.cpu_temp_c, cpu_valid);
    set_temperature_text(tile_values[1], latest.gpu_temp_c, gpu_valid);
    set_temperature_text(tile_values[2], latest.motherboard_temp_c, board_valid);
    set_tile_note(0, cpu_valid ? pressure_text(level_for_temperature(latest.cpu_temp_c, PERF_TEMP_CPU)) : "NO SENSOR");
    set_tile_note(1, gpu_valid ? pressure_text(level_for_temperature(latest.gpu_temp_c, PERF_TEMP_GPU)) : "NO SENSOR");
    set_tile_note(2, board_valid ? pressure_text(level_for_temperature(latest.motherboard_temp_c, PERF_TEMP_MOTHERBOARD)) : "NO SENSOR");
    if (latest.temperature_valid_mask == 0) {
        lv_label_set_text(tile_values[3], "--");
        lv_obj_set_style_text_color(tile_values[3], foreground_color(), LV_PART_MAIN);
        set_tile_note(3, "NO SENSORS");
    } else {
        const float pressure_value = performance_highest_thermal_pressure(latest);
        (void)pressure_value;
        update_pressure_state(latest);
        const ThermalLevel pressure = displayed_pressure_level;
        lv_label_set_text(tile_values[3], pressure_text(pressure));
        lv_obj_set_style_text_color(tile_values[3], pressure_color(pressure), LV_PART_MAIN);
        char pressure_note[20];
        const uint8_t component = pressure_component(latest);
        const char *short_component = component == PERF_TEMP_MOTHERBOARD ? "MB" :
                                      pressure_component_name(component);
        snprintf(pressure_note, sizeof(pressure_note), "%s %.0f%%",
                 short_component, smoothed_pressure * 100.0f);
        set_tile_note(3, pressure_note);
    }
    update_dashboard_charts(true);
}

uint64_t network_history_ceiling() {
    uint64_t maximum = 1;
    const size_t count = performance_data_history_count();
    PerformanceSample sample = {};
    for (size_t i = 0; i < count; ++i) {
        if (!performance_data_get_history(i, sample)) continue;
        if (sample.network_down_bps > maximum) maximum = sample.network_down_bps;
        if (sample.network_up_bps > maximum) maximum = sample.network_up_bps;
    }
    uint64_t ceiling = 1000;
    while (ceiling < maximum && ceiling <= UINT64_MAX / 10u) ceiling *= 10u;
    return ceiling;
}

void fill_chart_missing() {
    if (!chart || !chart_primary) return;
    for (uint32_t i = 0; i < PERFORMANCE_HISTORY_CAPACITY; ++i) {
        lv_chart_set_value_by_id(chart, chart_primary, i, LV_CHART_POINT_NONE);
        if (chart_secondary) lv_chart_set_value_by_id(chart, chart_secondary, i, LV_CHART_POINT_NONE);
    }
}

void update_metric_graph(const PerformanceSample &latest, bool available) {
    update_freshness();
    const bool temperature = is_temperature_view(current_view);
    const bool valid = available && (temperature ? has_temperature(latest, current_view)
                                                 : has_metric(latest, current_view));
    const float value = valid ? sample_value(latest, current_view) : NAN;
    if (temperature) set_temperature_text(primary_value_label, value, valid);
    else set_percent_text(primary_value_label, value);
    if (valid && current_view == PerformanceView::MEMORY_USAGE && latest.ram_total_bytes > 0) {
        char memory[40];
        const double used_gib = static_cast<double>(latest.ram_used_bytes) / 1073741824.0;
        const double total_gib = static_cast<double>(latest.ram_total_bytes) / 1073741824.0;
        snprintf(memory, sizeof(memory), "%.1f / %.1f GiB  /  LAST 60", used_gib, total_gib);
        lv_label_set_text(secondary_value_label, memory);
    } else {
        lv_label_set_text(secondary_value_label,
                          valid ? (temperature ? "LAST 60 SAMPLES  /  CELSIUS" : "LAST 60 SAMPLES  /  PERCENT")
                                : (available ? "SENSOR UNAVAILABLE" : "WAITING FOR PC DATA"));
    }

    const bool board_temperature = current_view == PerformanceView::MOTHERBOARD_TEMPERATURE;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                       temperature && !board_temperature ? 20 : 0,
                       temperature && !board_temperature ? 110 : 100);
    fill_chart_missing();
    const size_t count = performance_data_history_count();
    const size_t offset = count < PERFORMANCE_HISTORY_CAPACITY ? PERFORMANCE_HISTORY_CAPACITY - count : 0;
    PerformanceSample sample = {};
    for (size_t i = 0; i < count && i < PERFORMANCE_HISTORY_CAPACITY; ++i) {
        if (!performance_data_get_history(i, sample)) continue;
        if (temperature ? !has_temperature(sample, current_view)
                        : !has_metric(sample, current_view)) continue;
        const float sample_metric = sample_value(sample, current_view);
        if (!isfinite(sample_metric)) continue;
        lv_chart_set_value_by_id(chart, chart_primary, offset + i,
                                 static_cast<int32_t>(lroundf(sample_metric)));
    }
    lv_chart_refresh(chart);
}

void update_network_graph(const PerformanceSample &latest, bool available) {
    update_freshness();
    char down[24], up[24], primary[32], secondary[36];
    const bool down_available = available &&
        (latest.metric_valid_mask & PERF_METRIC_NETWORK_DOWN) != 0;
    const bool up_available = available &&
        (latest.metric_valid_mask & PERF_METRIC_NETWORK_UP) != 0;
    if (available && (down_available || up_available)) {
        if (down_available) format_network_rate(latest.network_down_bps, down, sizeof(down));
        else snprintf(down, sizeof(down), "N/A");
        if (up_available) format_network_rate(latest.network_up_bps, up, sizeof(up));
        else snprintf(up, sizeof(up), "N/A");
        snprintf(primary, sizeof(primary), "DOWN  %s", down);
        snprintf(secondary, sizeof(secondary), "UP  %s  /  LAST 60", up);
    } else {
        snprintf(primary, sizeof(primary), "--");
        snprintf(secondary, sizeof(secondary), "WAITING FOR WINDOWS TELEMETRY");
    }
    lv_label_set_text(primary_value_label, primary);
    lv_obj_set_style_text_font(primary_value_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(secondary_value_label, secondary);

    const uint64_t ceiling = network_history_ceiling();
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    fill_chart_missing();
    const size_t count = performance_data_history_count();
    const size_t offset = count < PERFORMANCE_HISTORY_CAPACITY ? PERFORMANCE_HISTORY_CAPACITY - count : 0;
    PerformanceSample sample = {};
    for (size_t i = 0; i < count && i < PERFORMANCE_HISTORY_CAPACITY; ++i) {
        if (!performance_data_get_history(i, sample)) continue;
        if (sample.metric_valid_mask & PERF_METRIC_NETWORK_DOWN) {
            const int32_t down_point = static_cast<int32_t>(
                (static_cast<double>(sample.network_down_bps) / static_cast<double>(ceiling)) * 1000.0);
            lv_chart_set_value_by_id(chart, chart_primary, offset + i, down_point);
        }
        if (sample.metric_valid_mask & PERF_METRIC_NETWORK_UP) {
            const int32_t up_point = static_cast<int32_t>(
                (static_cast<double>(sample.network_up_bps) / static_cast<double>(ceiling)) * 1000.0);
            lv_chart_set_value_by_id(chart, chart_secondary, offset + i, up_point);
        }
    }
    lv_chart_refresh(chart);
}

void update_pressure_view(const PerformanceSample &latest, bool available) {
    update_freshness();
    const bool temperatures_available = available && latest.temperature_valid_mask != 0;
    if (temperatures_available) {
        update_pressure_state(latest);
        const ThermalLevel pressure = displayed_pressure_level;
        lv_label_set_text(primary_value_label, pressure_text(pressure));
        lv_obj_set_style_text_color(primary_value_label, pressure_color(pressure), LV_PART_MAIN);
        char detail[64];
        snprintf(detail, sizeof(detail), "%s  /  ESTIMATE %.0f%%",
                 pressure_component_name(pressure_component(latest)),
                 smoothed_pressure * 100.0f);
        lv_label_set_text(secondary_value_label, detail);
    } else {
        lv_label_set_text(primary_value_label, "NO DATA");
        lv_obj_set_style_text_color(primary_value_label, foreground_color(), LV_PART_MAIN);
        lv_label_set_text(secondary_value_label,
                          available ? "NO TEMPERATURE SENSORS REPORTED" : "WAITING FOR WINDOWS TELEMETRY");
    }

    fill_chart_missing();
    const size_t count = performance_data_history_count();
    const size_t offset = count < PERFORMANCE_HISTORY_CAPACITY ? PERFORMANCE_HISTORY_CAPACITY - count : 0;
    PerformanceSample sample = {};
    for (size_t i = 0; i < count && i < PERFORMANCE_HISTORY_CAPACITY; ++i) {
        if (!performance_data_get_history(i, sample) || sample.temperature_valid_mask == 0) continue;
        const float pressure = performance_highest_thermal_pressure(sample);
        lv_chart_set_value_by_id(chart, chart_primary, offset + i,
                                 static_cast<int32_t>(lroundf(pressure * 100.0f)));
    }
    lv_chart_refresh(chart);
}

void refresh_current_view() {
    if (!screen || !content || current_view == PerformanceView::SELECTOR) return;
    PerformanceSample latest = {};
    const PerformanceConnectionState connection = performance_data_connection_state(millis());
    const bool available = performance_data_get_latest(latest) &&
                           connection != PerformanceConnectionState::DISCONNECTED;
    switch (current_view) {
        case PerformanceView::ALL_USAGE:
            update_usage_dashboard(latest, available);
            break;
        case PerformanceView::ALL_TEMPERATURES:
            update_temperature_dashboard(latest, available);
            break;
        case PerformanceView::NETWORK:
            update_network_graph(latest, available);
            break;
        case PerformanceView::THERMAL_PRESSURE:
            update_pressure_view(latest, available);
            break;
        default:
            update_metric_graph(latest, available);
            break;
    }
    if ((current_view == PerformanceView::ALL_USAGE ||
         current_view == PerformanceView::ALL_TEMPERATURES) &&
        connection == PerformanceConnectionState::STALE) {
        for (uint8_t i = 0; i < 4; ++i) set_tile_note(i, "STALE");
    }
    last_refresh_ms = millis();
}

lv_obj_t *create_screen_impl() {
    screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, nullptr);
    create_header();
    render_current_view();
    return screen;
}

} // namespace

void ui_performance_set_navigation_callbacks(UiPerformanceNavigationCallback home,
                                             UiPerformanceNavigationCallback back,
                                             void *context) {
    home_callback = home;
    back_callback = back;
    navigation_context = context;
}

lv_obj_t *ui_performance_create() {
    if (screen) return screen;
    last_metric_view = configured_default_view();
    current_view = last_metric_view;
    selector_tab = is_temperature_view(current_view) ? SelectorTab::TEMPERATURE : SelectorTab::USAGE;
    return create_screen_impl();
}

void ui_performance_tick() {
    if (!screen || lv_screen_active() != screen || current_view == PerformanceView::SELECTOR) return;
    const uint32_t now = millis();
    if (now - last_refresh_ms >= REFRESH_INTERVAL_MS) refresh_current_view();
}

lv_obj_t *ui_performance_recreate() {
    const bool was_active = screen && lv_screen_active() == screen;
    lv_obj_t *old = screen;
    screen = nullptr;
    content = nullptr;
    heading_label = nullptr;
    clear_widget_references();
    lv_obj_t *replacement = create_screen_impl();
    if (was_active) lv_screen_load(replacement);
    if (old && old != replacement) lv_obj_delete(old);
    return replacement;
}

lv_obj_t *ui_performance_screen() {
    return screen;
}

void ui_performance_apply_default_view() {
    last_metric_view = configured_default_view();
    current_view = last_metric_view;
    selector_tab = is_temperature_view(current_view)
                       ? SelectorTab::TEMPERATURE : SelectorTab::USAGE;
    if (screen) render_current_view();
}

#else

void ui_performance_set_navigation_callbacks(UiPerformanceNavigationCallback, UiPerformanceNavigationCallback,
                                             void *) {}
lv_obj_t *ui_performance_create() { return nullptr; }
void ui_performance_tick() {}
lv_obj_t *ui_performance_recreate() { return nullptr; }
lv_obj_t *ui_performance_screen() { return nullptr; }
void ui_performance_apply_default_view() {}

#endif
