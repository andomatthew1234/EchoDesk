#include "../src/my_device_health.h"
#include "../src/my_device_health_runtime.h"

#include <stdio.h>
#include <string.h>

static MyDeviceHealthFacts test_facts{};

void my_device_health_read_facts(uint32_t, MyDeviceHealthFacts &out) {
    out = test_facts;
}

static bool check(bool condition, const char *message) {
    if (condition) return true;
    fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

int main() {
    strcpy(test_facts.firmware_version, "2.32.0");
    test_facts.uptime_seconds = 93784;
    test_facts.free_heap_bytes = 123456;
    test_facts.wifi_configured = true;
    test_facts.wifi_connected = true;
    test_facts.time_synced = true;
    strcpy(test_facts.wifi_ssid, "Studio");
    strcpy(test_facts.wifi_ip, "192.168.1.44");
    test_facts.wifi_rssi_dbm = -55;
    test_facts.music_seen = true;
    test_facts.music_received_ms = 98000;
    test_facts.music_state = MyDeviceHealthState::LIVE;
    test_facts.performance_seen = true;
    test_facts.performance_received_ms = 80000;
    test_facts.performance_state = MyDeviceHealthState::OFFLINE;

    my_device_health_init();
    MyDeviceHealthSnapshot snapshot{};
    my_device_health_capture(100000, snapshot);
    if (!check(snapshot.desktop.state == MyDeviceHealthState::WAITING,
               "Desktop starts waiting") ||
        !check(snapshot.codex.state == MyDeviceHealthState::WAITING,
               "Codex starts waiting") ||
        !check(snapshot.music.state == MyDeviceHealthState::LIVE &&
                   snapshot.music.age_seconds == 2,
               "Music age is derived from receipt time") ||
        !check(snapshot.performance.state == MyDeviceHealthState::OFFLINE &&
                   snapshot.performance.age_seconds == 20,
               "Performance preserves source state and age") ||
        !check(snapshot.wifi.state == MyDeviceHealthState::LIVE &&
                   strcmp(snapshot.wifi_ip, "192.168.1.44") == 0,
               "Wi-Fi details are bounded into the snapshot") ||
        !check(strcmp(snapshot.serial_transport, "AIM1") == 0 &&
                   snapshot.update_protocol == 1,
               "Protocol versions are reported")) {
        return 1;
    }

    my_device_health_note_host_activity(100000);
    my_device_health_note_codex_activity(100000);
    my_device_health_capture(114999, snapshot);
    if (!check(snapshot.desktop.state == MyDeviceHealthState::LIVE,
               "Desktop is connected through fifteen seconds") ||
        !check(snapshot.codex.state == MyDeviceHealthState::LIVE,
               "Codex is live after a valid frame")) {
        return 1;
    }

    my_device_health_capture(115001, snapshot);
    if (!check(snapshot.desktop.state == MyDeviceHealthState::OFFLINE,
               "Desktop becomes offline after the bounded activity window")) {
        return 1;
    }
    my_device_health_capture(200001, snapshot);
    if (!check(snapshot.codex.state == MyDeviceHealthState::STALE,
               "Codex becomes delayed before expiry")) {
        return 1;
    }
    my_device_health_capture(400001, snapshot);
    if (!check(snapshot.codex.state == MyDeviceHealthState::OFFLINE,
               "Codex expires after five minutes")) {
        return 1;
    }

    char text[24];
    my_device_health_format_age(snapshot.performance, text, sizeof(text));
    if (!check(strcmp(text, "5m ago") == 0,
               "Age formatting is compact") ) return 1;
    my_device_health_format_uptime(93784, text, sizeof(text));
    if (!check(strcmp(text, "1d 2h") == 0,
               "Uptime formatting is compact")) return 1;

    test_facts.wifi_configured = false;
    test_facts.wifi_connected = false;
    my_device_health_capture(400001, snapshot);
    if (!check(snapshot.wifi.state == MyDeviceHealthState::NOT_CONFIGURED,
               "Wi-Fi distinguishes not configured from offline")) return 1;

    puts("my_device_health_native: PASS");
    return 0;
}
