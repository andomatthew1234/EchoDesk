#ifndef MY_DEVICE_HEALTH_H
#define MY_DEVICE_HEALTH_H

#include <stddef.h>
#include <stdint.h>

enum class MyDeviceHealthState : uint8_t {
    NOT_CONFIGURED = 0,
    WAITING,
    LIVE,
    STALE,
    OFFLINE,
};

struct MyDeviceHealthChannel {
    MyDeviceHealthState state;
    bool has_activity;
    uint32_t age_seconds;
};

struct MyDeviceHealthSnapshot {
    char firmware_version[16];
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;

    MyDeviceHealthChannel desktop;
    MyDeviceHealthChannel wifi;
    MyDeviceHealthChannel codex;
    MyDeviceHealthChannel music;
    MyDeviceHealthChannel performance;

    bool wifi_configured;
    bool wifi_connected;
    bool time_synced;
    char wifi_ssid[33];
    char wifi_ip[16];
    int16_t wifi_rssi_dbm;

    char serial_transport[8];
    uint8_t scene_protocol;
    uint8_t control_protocol;
    uint8_t setup_protocol;
    uint8_t wifi_setup_protocol;
    uint8_t update_protocol;
    uint8_t performance_protocol;
    uint8_t music_protocol;
};

// Reset runtime activity timestamps at boot.
void my_device_health_init();

// Called only after the serial parser accepts a host command or framed payload.
// Receiving arbitrary bytes is deliberately not enough to claim Desktop is live.
void my_device_health_note_host_activity(uint32_t now_ms);

// Codex notices are valid source frames but do not populate UsageData::last_fetch.
// This dedicated hook keeps their source age honest without changing that legacy
// structure or conflating Codex freshness with unrelated music traffic.
void my_device_health_note_codex_activity(uint32_t now_ms);

// One bounded read for the UI. Runtime details remain behind this interface.
void my_device_health_capture(uint32_t now_ms, MyDeviceHealthSnapshot &out);

const char *my_device_health_state_label(MyDeviceHealthState state);
void my_device_health_format_age(const MyDeviceHealthChannel &channel,
                                 char *out, size_t out_size);
void my_device_health_format_uptime(uint32_t uptime_seconds,
                                    char *out, size_t out_size);

#endif // MY_DEVICE_HEALTH_H
