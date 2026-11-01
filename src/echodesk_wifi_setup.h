#ifndef ECHODESK_WIFI_SETUP_H
#define ECHODESK_WIFI_SETUP_H

#include <stdint.h>

enum class EchoDeskWifiSetupStatus : uint8_t {
    IDLE,
    STARTING,
    PROVISIONING,
    CONNECTING,
    CONNECTED,
    EXPIRED,
    ERROR,
};

void echodesk_wifi_setup_init();
void echodesk_wifi_setup_tick();
void echodesk_wifi_setup_request_start();
void echodesk_wifi_setup_stop();

EchoDeskWifiSetupStatus echodesk_wifi_setup_status();
const char *echodesk_wifi_setup_ap_ssid();
const char *echodesk_wifi_setup_pairing_code();
const char *echodesk_wifi_setup_device_id();
bool echodesk_wifi_setup_is_active();

#endif // ECHODESK_WIFI_SETUP_H
