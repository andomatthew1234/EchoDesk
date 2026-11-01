#ifndef ECHODESK_WIFI_PROTOCOL_H
#define ECHODESK_WIFI_PROTOCOL_H

#include <stddef.h>

#define ECHODESK_WIFI_PROTOCOL_VERSION 1
#define ECHODESK_WIFI_DISCOVERY_PORT 39320
#define ECHODESK_WIFI_SERVICE_PORT 39321
#define ECHODESK_WIFI_MAX_MESSAGE_BYTES 1024
#define ECHODESK_WIFI_PAIRING_CODE_LENGTH 8
#define ECHODESK_WIFI_CLIENT_TOKEN_LENGTH 64

bool echodesk_wifi_valid_pairing_code(const char *value);
bool echodesk_wifi_valid_ssid(const char *value);
bool echodesk_wifi_valid_password(const char *value);
bool echodesk_wifi_valid_client_token(const char *value);
bool echodesk_wifi_valid_nonce(const char *value);

#endif // ECHODESK_WIFI_PROTOCOL_H
