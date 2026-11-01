#ifndef BEDSIDE_MODE_H
#define BEDSIDE_MODE_H

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t BEDSIDE_PROTOCOL_VERSION = 1;
constexpr uint8_t BEDSIDE_MAX_ALARMS = 8;
constexpr uint8_t BEDSIDE_MAX_SEGMENTS = 8;
constexpr size_t BEDSIDE_ALARM_LABEL_BYTES = 25;

enum class BedsideClockFormat : uint8_t { HOUR_12 = 0, HOUR_24 = 1 };
enum class BedsideClockStyle : uint8_t { LARGE = 0, MINIMAL = 1 };

struct BedsideAlarm {
    uint8_t id;
    bool enabled;
    uint16_t minute_of_day;
    // Bit 0 is Sunday, bit 6 is Saturday. At least one bit must be set.
    uint8_t weekdays;
    char label[BEDSIDE_ALARM_LABEL_BYTES];
};

struct BedsideBrightnessSegment {
    uint16_t start_minute;
    uint8_t brightness;
};

struct BedsideScheduledToggle {
    bool schedule_enabled;
    bool manual_enabled;
    uint16_t start_minute;
    uint16_t end_minute;
};

struct BedsideConfig {
    BedsideClockFormat clock_format;
    bool show_seconds;
    BedsideClockStyle clock_style;
    bool widget_next_alarm;
    bool widget_weather;
    uint8_t alarm_count;
    BedsideAlarm alarms[BEDSIDE_MAX_ALARMS];
    bool display_schedule_enabled;
    uint8_t manual_brightness;
    uint8_t segment_count;
    BedsideBrightnessSegment segments[BEDSIDE_MAX_SEGMENTS];
    BedsideScheduledToggle night_shift;
    BedsideScheduledToggle dark_theme;
};

struct BedsideLocalTime {
    bool valid;
    uint64_t epoch;
    int32_t date_key;       // local YYYYMMDD
    uint16_t minute_of_day;
    uint8_t second;
    uint8_t weekday;        // 0 Sunday .. 6 Saturday
};

struct BedsideNextAlarm {
    bool valid;
    uint8_t id;
    uint8_t days_away;
    uint16_t minute_of_day;
    uint64_t approximate_epoch;
    char label[BEDSIDE_ALARM_LABEL_BYTES];
};

struct BedsideRuntime {
    bool time_valid;
    bool alarm_ringing;
    bool display_sleeping;
    bool skipped_tomorrow;
    uint8_t ringing_alarm_id;
    int32_t skipped_date_key;
    BedsideNextAlarm next_alarm;
};

enum class BedsideResult : uint8_t {
    OK,
    STALE_REVISION,
    INVALID_CONFIG,
};

BedsideConfig bedside_default_config();
bool bedside_validate_config(const BedsideConfig &config);
const char *bedside_result_message(BedsideResult result);

// Initializes the in-memory state from the separately persisted Bedside blob.
void bedside_init(const BedsideConfig &config, uint32_t revision);
const BedsideConfig &bedside_get_config();
uint32_t bedside_revision();
BedsideResult bedside_set_config(const BedsideConfig &config,
                                 uint32_t base_revision);

uint8_t bedside_brightness_for_minute(uint16_t minute_of_day);
bool bedside_night_shift_active(uint16_t minute_of_day);
bool bedside_dark_theme_active(uint16_t minute_of_day);

// Called from the UI loop with local wall-clock time. Alarm state is retained
// in this module so rebuilding the LVGL screen cannot re-fire an alarm.
void bedside_tick(const BedsideLocalTime &now);
const BedsideRuntime &bedside_runtime();
void bedside_stop_alarm();
void bedside_cancel_tomorrow(int32_t tomorrow_date_key);
void bedside_set_display_sleeping(bool sleeping);

#endif // BEDSIDE_MODE_H
