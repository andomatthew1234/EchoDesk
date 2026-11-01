#include "../src/bedside_mode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace {

BedsideAlarm alarm(uint8_t id, uint16_t minute, uint8_t weekdays,
                   const char *label = "Alarm") {
    BedsideAlarm value{};
    value.id = id;
    value.enabled = true;
    value.minute_of_day = minute;
    value.weekdays = weekdays;
    strncpy(value.label, label, sizeof(value.label) - 1);
    return value;
}

BedsideLocalTime local(int32_t date, uint16_t minute, uint8_t second,
                       uint8_t weekday, uint64_t epoch = 2000000000ULL) {
    return {true, epoch, date, minute, second, weekday};
}

void test_defaults() {
    const BedsideConfig config = bedside_default_config();
    assert(bedside_validate_config(config));
    assert(config.alarm_count == 0);
    assert(config.segment_count == 7);
    assert(!config.widget_weather);
    bedside_init(config, 7);
    assert(bedside_revision() == 7);
    assert(bedside_brightness_for_minute(0) == 1);
    assert(bedside_brightness_for_minute(300) == 40);
    assert(bedside_brightness_for_minute(419) == 40);
    assert(bedside_brightness_for_minute(420) == 70);
    assert(bedside_brightness_for_minute(480) == 100);
    assert(bedside_brightness_for_minute(1080) == 60);
    assert(bedside_brightness_for_minute(1140) == 40);
    assert(bedside_brightness_for_minute(1230) == 30);
    assert(bedside_brightness_for_minute(1290) == 1);
    assert(bedside_night_shift_active(0));
    assert(!bedside_night_shift_active(600));
    assert(bedside_dark_theme_active(1200));
}

void test_validation_and_revision() {
    BedsideConfig config = bedside_default_config();
    bedside_init(config, 2);
    config.manual_brightness = 0;
    assert(!bedside_validate_config(config));
    assert(bedside_set_config(config, 2) == BedsideResult::INVALID_CONFIG);
    config = bedside_default_config();
    assert(bedside_set_config(config, 1) == BedsideResult::STALE_REVISION);
    config.display_schedule_enabled = false;
    config.manual_brightness = 17;
    assert(bedside_set_config(config, 2) == BedsideResult::OK);
    assert(bedside_revision() == 3);
    assert(bedside_brightness_for_minute(600) == 17);

    config = bedside_get_config();
    config.segment_count = 2;
    config.segments[0] = {600, 50};
    config.segments[1] = {600, 60};
    assert(!bedside_validate_config(config));
}

void test_alarm_once_stop_and_skip() {
    BedsideConfig config = bedside_default_config();
    config.alarm_count = 2;
    config.alarms[0] = alarm(1, 420, 0x7F, "Wake up");
    config.alarms[1] = alarm(2, 480, static_cast<uint8_t>(1u << 2), "Tuesday");
    bedside_init(config, 1);

    bedside_tick(local(20261101, 419, 30, 0));
    assert(!bedside_runtime().alarm_ringing);
    assert(bedside_runtime().next_alarm.valid);
    assert(bedside_runtime().next_alarm.id == 1);

    bedside_set_display_sleeping(true);
    bedside_tick(local(20261101, 420, 0, 0));
    assert(bedside_runtime().alarm_ringing);
    assert(!bedside_runtime().display_sleeping);
    bedside_stop_alarm();
    bedside_tick(local(20261101, 420, 20, 0));
    assert(!bedside_runtime().alarm_ringing); // cannot re-fire in same local day

    bedside_cancel_tomorrow(20261102);
    bedside_tick(local(20261101, 600, 0, 0));
    assert(bedside_runtime().skipped_tomorrow);
    assert(bedside_runtime().next_alarm.days_away >= 2);
    bedside_tick(local(20261102, 420, 0, 1));
    assert(!bedside_runtime().alarm_ringing);
    bedside_tick(local(20261103, 420, 0, 2));
    assert(!bedside_runtime().skipped_tomorrow);
    assert(bedside_runtime().alarm_ringing);
}

void test_manual_toggles() {
    BedsideConfig config = bedside_default_config();
    config.night_shift.schedule_enabled = false;
    config.night_shift.manual_enabled = true;
    config.dark_theme.schedule_enabled = false;
    config.dark_theme.manual_enabled = false;
    bedside_init(config, 1);
    assert(bedside_night_shift_active(600));
    assert(!bedside_dark_theme_active(1200));
}

} // namespace

int main() {
    test_defaults();
    test_validation_and_revision();
    test_alarm_once_stop_and_skip();
    test_manual_toggles();
    puts("bedside_mode_native: all tests passed");
    return 0;
}
