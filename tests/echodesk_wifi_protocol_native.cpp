#include <stdio.h>
#include <string.h>

#include "echodesk_wifi_protocol.h"

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,      \
                    #expression);                                             \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main() {
    CHECK(echodesk_wifi_valid_pairing_code("01234567"));
    CHECK(!echodesk_wifi_valid_pairing_code("1234567"));
    CHECK(!echodesk_wifi_valid_pairing_code("1234567x"));
    CHECK(echodesk_wifi_valid_ssid("EchoDesk"));
    CHECK(!echodesk_wifi_valid_ssid(""));
    CHECK(!echodesk_wifi_valid_ssid("123456789012345678901234567890123"));
    CHECK(echodesk_wifi_valid_password(""));
    CHECK(echodesk_wifi_valid_password(
        "1234567890123456789012345678901234567890123456789012345678901234"));
    CHECK(!echodesk_wifi_valid_password(
        "12345678901234567890123456789012345678901234567890123456789012345"));
    CHECK(echodesk_wifi_valid_client_token(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    CHECK(!echodesk_wifi_valid_client_token(
        "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
    CHECK(!echodesk_wifi_valid_client_token("abcd"));
    CHECK(echodesk_wifi_valid_nonce("Desktop_01-a"));
    CHECK(!echodesk_wifi_valid_nonce(""));
    CHECK(!echodesk_wifi_valid_nonce("spaces are not allowed"));
    CHECK(!echodesk_wifi_valid_nonce("123456789012345678901234567890123"));
    puts("echodesk_wifi_protocol_native: PASS");
    return 0;
}
