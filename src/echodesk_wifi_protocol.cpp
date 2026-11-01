#include "echodesk_wifi_protocol.h"

#include <string.h>

bool echodesk_wifi_valid_pairing_code(const char *value) {
    if (!value || strlen(value) != ECHODESK_WIFI_PAIRING_CODE_LENGTH) return false;
    for (size_t i = 0; i < ECHODESK_WIFI_PAIRING_CODE_LENGTH; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

bool echodesk_wifi_valid_ssid(const char *value) {
    if (!value) return false;
    const size_t length = strlen(value);
    return length >= 1 && length <= 32;
}

bool echodesk_wifi_valid_password(const char *value) {
    return value && strlen(value) <= 64;
}

bool echodesk_wifi_valid_client_token(const char *value) {
    if (!value || strlen(value) != ECHODESK_WIFI_CLIENT_TOKEN_LENGTH) return false;
    for (size_t i = 0; i < ECHODESK_WIFI_CLIENT_TOKEN_LENGTH; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool echodesk_wifi_valid_nonce(const char *value) {
    if (!value) return false;
    const size_t length = strlen(value);
    if (length < 1 || length > 32) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}
