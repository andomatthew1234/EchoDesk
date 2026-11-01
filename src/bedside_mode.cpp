#include "bedside_mode.h"

#include <string.h>

namespace {

struct BedsideState {
    BedsideConfig config;
    BedsideRuntime runtime;
    uint32_t revision;
    int32_t last_fired_date;
    uint8_t last_fired_alarm;
};

// The display stack leaves very little static DRAM on the original ESP32.
// Allocate this bounded state once at boot instead of consuming scarce .bss.
BedsideState *g_state = nullptr;

bool valid_minute(uint16_t minute) { return minute < 1440; }

bool active_in_window(uint16_t minute, uint16_t start, uint16_t end) {
    if (start == end) return true;
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end;
}

bool alarm_matches(const BedsideAlarm &alarm, const BedsideLocalTime &now) {
    return alarm.enabled && alarm.minute_of_day == now.minute_of_day &&
           (alarm.weekdays & static_cast<uint8_t>(1u << now.weekday)) != 0;
}

void refresh_next_alarm(const BedsideLocalTime &now) {
    BedsideNextAlarm next{};
    if (!now.valid) {
        g_state->runtime.next_alarm = next;
        return;
    }

    uint32_t best_minutes = UINT32_MAX;
    for (uint8_t day = 0; day <= 7; ++day) {
        const uint8_t weekday = static_cast<uint8_t>((now.weekday + day) % 7);
        for (uint8_t i = 0; i < g_state->config.alarm_count; ++i) {
            const BedsideAlarm &alarm = g_state->config.alarms[i];
            if (!alarm.enabled ||
                (alarm.weekdays & static_cast<uint8_t>(1u << weekday)) == 0) {
                continue;
            }
            if (g_state->runtime.skipped_tomorrow &&
                ((day == 0 && now.date_key == g_state->runtime.skipped_date_key) ||
                 (day == 1 && now.date_key != g_state->runtime.skipped_date_key))) {
                continue;
            }

            int32_t delta = static_cast<int32_t>(day) * 1440 +
                            static_cast<int32_t>(alarm.minute_of_day) -
                            static_cast<int32_t>(now.minute_of_day);
            if (delta < 0 || (delta == 0 && now.second > 0)) continue;
            if (day == 0 && alarm.id == g_state->last_fired_alarm &&
                now.date_key == g_state->last_fired_date) {
                continue;
            }
            if (static_cast<uint32_t>(delta) >= best_minutes) continue;
            best_minutes = static_cast<uint32_t>(delta);
            next.valid = true;
            next.id = alarm.id;
            next.days_away = day;
            next.minute_of_day = alarm.minute_of_day;
            // Approximate because civil days can be 23/25 hours at DST changes.
            next.approximate_epoch = now.epoch +
                                     static_cast<uint64_t>(delta) * 60u -
                                     now.second;
            memcpy(next.label, alarm.label, sizeof(next.label));
            next.label[sizeof(next.label) - 1] = '\0';
        }
    }
    g_state->runtime.next_alarm = next;
}

} // namespace

BedsideConfig bedside_default_config() {
    BedsideConfig config{};
    config.clock_format = BedsideClockFormat::HOUR_12;
    config.show_seconds = false;
    config.clock_style = BedsideClockStyle::LARGE;
    config.widget_next_alarm = true;
    // Weather is represented in the desktop editor as a disabled Coming Soon
    // option. Keep the protocol field for forward compatibility, but do not
    // show a non-functional widget on first boot.
    config.widget_weather = false;
    config.alarm_count = 0;
    config.display_schedule_enabled = true;
    config.manual_brightness = 40;
    config.segment_count = 7;
    // Sorted local-time boundaries; the last segment wraps through midnight.
    config.segments[0] = {300, 40};   // 05:00-06:59
    config.segments[1] = {420, 70};   // 07:00-07:59
    config.segments[2] = {480, 100};  // 08:00-17:59
    config.segments[3] = {1080, 60};  // 18:00-18:59
    config.segments[4] = {1140, 40};  // 19:00-20:29
    config.segments[5] = {1230, 30};  // 20:30-21:29
    config.segments[6] = {1290, 1};   // 21:30-04:59
    config.night_shift = {true, false, 1140, 420};
    config.dark_theme = {true, false, 1140, 420};
    return config;
}

bool bedside_validate_config(const BedsideConfig &config) {
    if (config.clock_format != BedsideClockFormat::HOUR_12 &&
        config.clock_format != BedsideClockFormat::HOUR_24) return false;
    if (config.clock_style != BedsideClockStyle::LARGE &&
        config.clock_style != BedsideClockStyle::MINIMAL) return false;
    if (config.alarm_count > BEDSIDE_MAX_ALARMS ||
        config.segment_count == 0 || config.segment_count > BEDSIDE_MAX_SEGMENTS ||
        config.manual_brightness < 1 || config.manual_brightness > 100) {
        return false;
    }
    bool ids[256] = {};
    for (uint8_t i = 0; i < config.alarm_count; ++i) {
        const BedsideAlarm &alarm = config.alarms[i];
        if (alarm.id == 0 || alarm.id > BEDSIDE_MAX_ALARMS || ids[alarm.id] ||
            !valid_minute(alarm.minute_of_day) ||
            alarm.weekdays == 0 || (alarm.weekdays & 0x80u) != 0 ||
            alarm.label[BEDSIDE_ALARM_LABEL_BYTES - 1] != '\0') {
            return false;
        }
        ids[alarm.id] = true;
    }
    uint16_t previous = 0;
    for (uint8_t i = 0; i < config.segment_count; ++i) {
        const BedsideBrightnessSegment &segment = config.segments[i];
        if (!valid_minute(segment.start_minute) || segment.brightness < 1 ||
            segment.brightness > 100 || (i > 0 && segment.start_minute <= previous)) {
            return false;
        }
        previous = segment.start_minute;
    }
    const BedsideScheduledToggle *toggles[2] = {
        &config.night_shift, &config.dark_theme};
    for (const BedsideScheduledToggle *toggle : toggles) {
        if (!valid_minute(toggle->start_minute) ||
            !valid_minute(toggle->end_minute)) return false;
    }
    return true;
}

const char *bedside_result_message(BedsideResult result) {
    switch (result) {
        case BedsideResult::OK: return "accepted";
        case BedsideResult::STALE_REVISION: return "bedside revision is stale";
        case BedsideResult::INVALID_CONFIG: return "bedside configuration is invalid";
    }
    return "bedside request failed";
}

void bedside_init(const BedsideConfig &config, uint32_t revision) {
    if (!g_state) g_state = new BedsideState{};
    g_state->config = bedside_validate_config(config) ? config : bedside_default_config();
    g_state->revision = revision == 0 ? 1 : revision;
    g_state->runtime = {};
    g_state->last_fired_date = 0;
    g_state->last_fired_alarm = 0;
}

const BedsideConfig &bedside_get_config() { return g_state->config; }
uint32_t bedside_revision() { return g_state->revision; }

BedsideResult bedside_set_config(const BedsideConfig &config,
                                 uint32_t base_revision) {
    if (base_revision != g_state->revision) return BedsideResult::STALE_REVISION;
    if (!bedside_validate_config(config)) return BedsideResult::INVALID_CONFIG;
    g_state->config = config;
    if (++g_state->revision == 0) g_state->revision = 1;
    return BedsideResult::OK;
}

uint8_t bedside_brightness_for_minute(uint16_t minute_of_day) {
    if (!g_state->config.display_schedule_enabled) return g_state->config.manual_brightness;
    uint8_t selected = g_state->config.segments[g_state->config.segment_count - 1].brightness;
    for (uint8_t i = 0; i < g_state->config.segment_count; ++i) {
        if (g_state->config.segments[i].start_minute > minute_of_day) break;
        selected = g_state->config.segments[i].brightness;
    }
    return selected;
}

bool bedside_night_shift_active(uint16_t minute_of_day) {
    return g_state->config.night_shift.schedule_enabled
               ? active_in_window(minute_of_day, g_state->config.night_shift.start_minute,
                                  g_state->config.night_shift.end_minute)
               : g_state->config.night_shift.manual_enabled;
}

bool bedside_dark_theme_active(uint16_t minute_of_day) {
    return g_state->config.dark_theme.schedule_enabled
               ? active_in_window(minute_of_day, g_state->config.dark_theme.start_minute,
                                  g_state->config.dark_theme.end_minute)
               : g_state->config.dark_theme.manual_enabled;
}

void bedside_tick(const BedsideLocalTime &now) {
    g_state->runtime.time_valid = now.valid;
    if (!now.valid || now.weekday > 6 || !valid_minute(now.minute_of_day)) {
        g_state->runtime.next_alarm = {};
        return;
    }

    if (g_state->runtime.skipped_tomorrow &&
        now.date_key > g_state->runtime.skipped_date_key) {
        g_state->runtime.skipped_tomorrow = false;
        g_state->runtime.skipped_date_key = 0;
    }
    if (!g_state->runtime.alarm_ringing &&
        g_state->runtime.skipped_date_key != now.date_key) {
        for (uint8_t i = 0; i < g_state->config.alarm_count; ++i) {
            const BedsideAlarm &alarm = g_state->config.alarms[i];
            if (!alarm_matches(alarm, now) ||
                (g_state->last_fired_date == now.date_key &&
                 g_state->last_fired_alarm == alarm.id)) {
                continue;
            }
            g_state->runtime.alarm_ringing = true;
            g_state->runtime.ringing_alarm_id = alarm.id;
            g_state->runtime.display_sleeping = false;
            g_state->last_fired_date = now.date_key;
            g_state->last_fired_alarm = alarm.id;
            break;
        }
    }
    refresh_next_alarm(now);
}

const BedsideRuntime &bedside_runtime() { return g_state->runtime; }

void bedside_stop_alarm() {
    g_state->runtime.alarm_ringing = false;
    g_state->runtime.ringing_alarm_id = 0;
}

void bedside_cancel_tomorrow(int32_t tomorrow_date_key) {
    if (tomorrow_date_key <= 0) return;
    g_state->runtime.skipped_tomorrow = true;
    g_state->runtime.skipped_date_key = tomorrow_date_key;
}

void bedside_set_display_sleeping(bool sleeping) {
    g_state->runtime.display_sleeping = sleeping;
}
