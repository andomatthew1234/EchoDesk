#include "echodesk_setup.h"

#include "config.h"

uint8_t echodesk_setup_normalize_state(uint8_t state) {
    return state <= ECHODESK_SETUP_COMPLETED
               ? state
               : ECHODESK_SETUP_NOT_CONFIGURED;
}

uint8_t echodesk_setup_initial_state(bool has_setup_state,
                                     uint8_t stored_state,
                                     bool has_legacy_config) {
    if (has_setup_state) return echodesk_setup_normalize_state(stored_state);
    return has_legacy_config ? ECHODESK_SETUP_COMPLETED
                             : ECHODESK_SETUP_NOT_CONFIGURED;
}

const char *echodesk_setup_state_name(uint8_t state) {
    switch (echodesk_setup_normalize_state(state)) {
        case ECHODESK_SETUP_WAITING_FOR_DESKTOP: return "waitingForDesktop";
        case ECHODESK_SETUP_IN_PROGRESS: return "inProgress";
        case ECHODESK_SETUP_COMPLETED: return "completed";
        default: return "notConfigured";
    }
}

bool echodesk_setup_can_begin(uint8_t state) {
    state = echodesk_setup_normalize_state(state);
    return state != ECHODESK_SETUP_COMPLETED;
}

bool echodesk_setup_can_complete(uint8_t state) {
    state = echodesk_setup_normalize_state(state);
    // A verified firmware install may reboot into genuinely blank NVS before
    // Desktop sends complete. Accept that post-flash state as well as the two
    // interactive setup states; completed remains non-replayable.
    return state == ECHODESK_SETUP_NOT_CONFIGURED ||
           state == ECHODESK_SETUP_WAITING_FOR_DESKTOP ||
           state == ECHODESK_SETUP_IN_PROGRESS;
}

uint8_t echodesk_setup_normalize_transport(uint8_t transport) {
    return transport == ECHODESK_SETUP_TRANSPORT_WIFI
               ? ECHODESK_SETUP_TRANSPORT_WIFI
               : ECHODESK_SETUP_TRANSPORT_USB;
}

const char *echodesk_setup_transport_name(uint8_t transport) {
    return echodesk_setup_normalize_transport(transport) ==
                   ECHODESK_SETUP_TRANSPORT_WIFI
               ? "wifi"
               : "usb";
}
