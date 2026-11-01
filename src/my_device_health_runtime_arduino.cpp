#include "my_device_health_runtime.h"

#include <string.h>

#if defined(ECHODESK_OS)

#include "config.h"
#include "music_data.h"
#include "performance_data.h"
#include "wifi_time.h"

#include <Arduino.h>
#include <esp_timer.h>
namespace {
MyDeviceHealthState music_health_state(MusicConnectionState state) {
    switch (state) {
        case MusicConnectionState::LIVE: return MyDeviceHealthState::LIVE;
        case MusicConnectionState::STALE: return MyDeviceHealthState::STALE;
        case MusicConnectionState::DISCONNECTED: return MyDeviceHealthState::OFFLINE;
        case MusicConnectionState::NEVER_RECEIVED: return MyDeviceHealthState::WAITING;
    }
    return MyDeviceHealthState::WAITING;
}

MyDeviceHealthState performance_health_state(PerformanceConnectionState state) {
    switch (state) {
        case PerformanceConnectionState::LIVE: return MyDeviceHealthState::LIVE;
        case PerformanceConnectionState::STALE: return MyDeviceHealthState::STALE;
        case PerformanceConnectionState::DISCONNECTED: return MyDeviceHealthState::OFFLINE;
        case PerformanceConnectionState::NEVER_RECEIVED: return MyDeviceHealthState::WAITING;
    }
    return MyDeviceHealthState::WAITING;
}
} // namespace

void my_device_health_read_facts(uint32_t now_ms, MyDeviceHealthFacts &out) {
    memset(&out, 0, sizeof(out));
    strlcpy(out.firmware_version, APP_VERSION, sizeof(out.firmware_version));
    out.uptime_seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    out.free_heap_bytes = ESP.getFreeHeap();

    out.wifi_configured = wifi_time_has_credentials();
    out.wifi_connected = wifi_time_is_connected();
    out.time_synced = wifi_time_is_synced();
    wifi_time_get_ssid(out.wifi_ssid, sizeof(out.wifi_ssid));
    wifi_time_get_ip(out.wifi_ip, sizeof(out.wifi_ip));
    out.wifi_rssi_dbm = wifi_time_rssi_dbm();

    MusicState music{};
    out.music_seen = music_data_get(music);
    out.music_received_ms = out.music_seen ? music.received_ms : 0;
    out.music_state = music_health_state(music_data_connection_state(now_ms));

    PerformanceSample performance{};
    out.performance_seen = performance_data_get_latest(performance);
    out.performance_received_ms = out.performance_seen ? performance.received_ms : 0;
    out.performance_state = performance_health_state(
        performance_data_connection_state(now_ms));
}

#else

void my_device_health_read_facts(uint32_t, MyDeviceHealthFacts &out) {
    memset(&out, 0, sizeof(out));
}

#endif // ECHODESK_OS
