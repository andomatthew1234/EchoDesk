#ifndef MY_DEVICE_HEALTH_RUNTIME_H
#define MY_DEVICE_HEALTH_RUNTIME_H

#include "my_device_health.h"

struct MyDeviceHealthFacts {
    char firmware_version[16];
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;

    bool wifi_configured;
    bool wifi_connected;
    bool time_synced;
    char wifi_ssid[33];
    char wifi_ip[16];
    int16_t wifi_rssi_dbm;

    bool music_seen;
    uint32_t music_received_ms;
    MyDeviceHealthState music_state;

    bool performance_seen;
    uint32_t performance_received_ms;
    MyDeviceHealthState performance_state;
};

// Internal seam: production reads Arduino/firmware state, while the native
// regression test provides an in-memory adapter.
void my_device_health_read_facts(uint32_t now_ms, MyDeviceHealthFacts &out);

#endif // MY_DEVICE_HEALTH_RUNTIME_H
