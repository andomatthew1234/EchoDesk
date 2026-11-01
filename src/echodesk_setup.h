#ifndef ECHODESK_SETUP_H
#define ECHODESK_SETUP_H

#include <stdint.h>

// Pure setup-state helpers shared by the NVS, serial and UI layers. Keeping
// transitions here makes the recovery rules native-testable without LVGL or
// Arduino hardware.
uint8_t echodesk_setup_normalize_state(uint8_t state);
uint8_t echodesk_setup_initial_state(bool has_setup_state,
                                     uint8_t stored_state,
                                     bool has_legacy_config);
const char *echodesk_setup_state_name(uint8_t state);
bool echodesk_setup_can_begin(uint8_t state);
bool echodesk_setup_can_complete(uint8_t state);
uint8_t echodesk_setup_normalize_transport(uint8_t transport);
const char *echodesk_setup_transport_name(uint8_t transport);

#endif // ECHODESK_SETUP_H
