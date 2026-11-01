#include "my_device_health.h"
#include "my_device_health_runtime.h"

#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t DESKTOP_LIVE_MS = 15000;
constexpr uint32_t CODEX_LIVE_MS = 90000;
constexpr uint32_t CODEX_STALE_MS = 300000;

bool host_seen = false;
uint32_t host_activity_ms = 0;
bool codex_seen = false;
uint32_t codex_activity_ms = 0;

MyDeviceHealthChannel timed_channel(bool seen, uint32_t received_ms,
                                    uint32_t now_ms, uint32_t live_ms,
                                    uint32_t stale_ms) {
    if (!seen) return {MyDeviceHealthState::WAITING, false, 0};
    const uint32_t age_ms = now_ms - received_ms;
    const MyDeviceHealthState state = age_ms <= live_ms
        ? MyDeviceHealthState::LIVE
        : age_ms <= stale_ms ? MyDeviceHealthState::STALE
                             : MyDeviceHealthState::OFFLINE;
    return {state, true, age_ms / 1000};
}

MyDeviceHealthChannel source_channel(bool seen, uint32_t received_ms,
                                      uint32_t now_ms,
                                      MyDeviceHealthState state) {
    if (!seen) return {MyDeviceHealthState::WAITING, false, 0};
    return {state, true, (now_ms - received_ms) / 1000};
}

void bounded_copy(char *destination, size_t capacity, const char *source) {
    if (!destination || capacity == 0) return;
    if (!source) source = "";
    snprintf(destination, capacity, "%s", source);
}
} // namespace

void my_device_health_init() {
    host_seen = false;
    host_activity_ms = 0;
    codex_seen = false;
    codex_activity_ms = 0;
}

void my_device_health_note_host_activity(uint32_t now_ms) {
    host_seen = true;
    host_activity_ms = now_ms;
}

void my_device_health_note_codex_activity(uint32_t now_ms) {
    codex_seen = true;
    codex_activity_ms = now_ms;
    my_device_health_note_host_activity(now_ms);
}

void my_device_health_capture(uint32_t now_ms, MyDeviceHealthSnapshot &out) {
    MyDeviceHealthFacts facts{};
    my_device_health_read_facts(now_ms, facts);
    memset(&out, 0, sizeof(out));

    bounded_copy(out.firmware_version, sizeof(out.firmware_version),
                 facts.firmware_version);
    out.uptime_seconds = facts.uptime_seconds;
    out.free_heap_bytes = facts.free_heap_bytes;
    out.desktop = timed_channel(host_seen, host_activity_ms, now_ms,
                                DESKTOP_LIVE_MS, DESKTOP_LIVE_MS);
    out.codex = timed_channel(codex_seen, codex_activity_ms, now_ms,
                              CODEX_LIVE_MS, CODEX_STALE_MS);
    out.music = source_channel(facts.music_seen, facts.music_received_ms,
                               now_ms, facts.music_state);
    out.performance = source_channel(
        facts.performance_seen, facts.performance_received_ms, now_ms,
        facts.performance_state);

    out.wifi_configured = facts.wifi_configured;
    out.wifi_connected = facts.wifi_connected;
    out.time_synced = facts.time_synced;
    bounded_copy(out.wifi_ssid, sizeof(out.wifi_ssid), facts.wifi_ssid);
    bounded_copy(out.wifi_ip, sizeof(out.wifi_ip), facts.wifi_ip);
    out.wifi_rssi_dbm = facts.wifi_rssi_dbm;
    out.wifi = {
        !facts.wifi_configured
            ? MyDeviceHealthState::NOT_CONFIGURED
            : facts.wifi_connected ? MyDeviceHealthState::LIVE
                                   : MyDeviceHealthState::OFFLINE,
        facts.wifi_configured,
        0,
    };

    bounded_copy(out.serial_transport, sizeof(out.serial_transport), "AIM1");
    out.scene_protocol = 1;
    out.control_protocol = 1;
    out.setup_protocol = 1;
    out.wifi_setup_protocol = 1;
    out.update_protocol = 1;
    out.performance_protocol = 1;
    out.music_protocol = 1;
}

const char *my_device_health_state_label(MyDeviceHealthState state) {
    switch (state) {
        case MyDeviceHealthState::NOT_CONFIGURED: return "NOT SET UP";
        case MyDeviceHealthState::WAITING: return "WAITING";
        case MyDeviceHealthState::LIVE: return "CONNECTED";
        case MyDeviceHealthState::STALE: return "DELAYED";
        case MyDeviceHealthState::OFFLINE: return "OFFLINE";
    }
    return "WAITING";
}

void my_device_health_format_age(const MyDeviceHealthChannel &channel,
                                 char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!channel.has_activity) {
        bounded_copy(out, out_size, "No data yet");
    } else if (channel.age_seconds < 60) {
        snprintf(out, out_size, "%lus ago",
                 static_cast<unsigned long>(channel.age_seconds));
    } else if (channel.age_seconds < 3600) {
        snprintf(out, out_size, "%lum ago",
                 static_cast<unsigned long>(channel.age_seconds / 60));
    } else {
        snprintf(out, out_size, "%luh ago",
                 static_cast<unsigned long>(channel.age_seconds / 3600));
    }
}

void my_device_health_format_uptime(uint32_t uptime_seconds,
                                    char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    const uint32_t days = uptime_seconds / 86400;
    const uint32_t hours = (uptime_seconds % 86400) / 3600;
    const uint32_t minutes = (uptime_seconds % 3600) / 60;
    if (days > 0) {
        snprintf(out, out_size, "%lud %luh", static_cast<unsigned long>(days),
                 static_cast<unsigned long>(hours));
    } else if (hours > 0) {
        snprintf(out, out_size, "%luh %lum", static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes));
    } else {
        snprintf(out, out_size, "%lum", static_cast<unsigned long>(minutes));
    }
}
