#ifndef WIFI_TIME_H
#define WIFI_TIME_H

#include <Arduino.h>

void wifi_time_init();
void wifi_time_tick();

bool wifi_time_has_credentials();
bool wifi_time_is_connected();
bool wifi_time_is_synced();
bool wifi_time_has_client_token();
const char *wifi_time_client_token();
void wifi_time_set_provisioning(bool active);

void wifi_time_save_credentials(const char *ssid, const char *password);
bool wifi_time_store_credentials(const char *ssid, const char *password);
bool wifi_time_save_client_token(const char *token);
void wifi_time_forget_credentials();
bool wifi_time_connect_now(uint32_t timeout_ms);

void wifi_time_print_status(const char *type = "wifi_status");
void wifi_time_print_scan();

#endif // WIFI_TIME_H
