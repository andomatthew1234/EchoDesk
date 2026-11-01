#include <stdio.h>
#include <string.h>

#include "config.h"
#include "echodesk_setup.h"

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,      \
                    #expression);                                             \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main() {
    CHECK(strcmp(echodesk_setup_state_name(ECHODESK_SETUP_NOT_CONFIGURED),
                 "notConfigured") == 0);
    CHECK(strcmp(echodesk_setup_state_name(ECHODESK_SETUP_WAITING_FOR_DESKTOP),
                 "waitingForDesktop") == 0);
    CHECK(strcmp(echodesk_setup_state_name(ECHODESK_SETUP_IN_PROGRESS),
                 "inProgress") == 0);
    CHECK(strcmp(echodesk_setup_state_name(ECHODESK_SETUP_COMPLETED),
                 "completed") == 0);
    CHECK(echodesk_setup_normalize_state(255) == ECHODESK_SETUP_NOT_CONFIGURED);
    CHECK(echodesk_setup_initial_state(false, ECHODESK_SETUP_IN_PROGRESS,
                                        false) == ECHODESK_SETUP_NOT_CONFIGURED);
    CHECK(echodesk_setup_initial_state(false, ECHODESK_SETUP_NOT_CONFIGURED,
                                        true) == ECHODESK_SETUP_COMPLETED);
    CHECK(echodesk_setup_initial_state(true, ECHODESK_SETUP_IN_PROGRESS,
                                        true) == ECHODESK_SETUP_IN_PROGRESS);
    CHECK(echodesk_setup_initial_state(true, 255, true) ==
           ECHODESK_SETUP_NOT_CONFIGURED);
    CHECK(echodesk_setup_can_begin(ECHODESK_SETUP_NOT_CONFIGURED));
    CHECK(echodesk_setup_can_begin(ECHODESK_SETUP_WAITING_FOR_DESKTOP));
    CHECK(!echodesk_setup_can_begin(ECHODESK_SETUP_COMPLETED));
    CHECK(echodesk_setup_can_complete(ECHODESK_SETUP_NOT_CONFIGURED));
    CHECK(echodesk_setup_can_complete(ECHODESK_SETUP_WAITING_FOR_DESKTOP));
    CHECK(echodesk_setup_can_complete(ECHODESK_SETUP_IN_PROGRESS));
    CHECK(!echodesk_setup_can_complete(ECHODESK_SETUP_COMPLETED));
    CHECK(echodesk_setup_normalize_transport(ECHODESK_SETUP_TRANSPORT_USB) ==
          ECHODESK_SETUP_TRANSPORT_USB);
    CHECK(echodesk_setup_normalize_transport(ECHODESK_SETUP_TRANSPORT_WIFI) ==
          ECHODESK_SETUP_TRANSPORT_WIFI);
    CHECK(echodesk_setup_normalize_transport(255) ==
          ECHODESK_SETUP_TRANSPORT_USB);
    CHECK(strcmp(echodesk_setup_transport_name(ECHODESK_SETUP_TRANSPORT_USB),
                 "usb") == 0);
    CHECK(strcmp(echodesk_setup_transport_name(ECHODESK_SETUP_TRANSPORT_WIFI),
                 "wifi") == 0);
    puts("echodesk_setup_native: PASS");
    return 0;
}
