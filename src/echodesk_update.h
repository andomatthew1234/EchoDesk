#ifndef ECHODESK_UPDATE_H
#define ECHODESK_UPDATE_H

#include <stdint.h>

enum class EchoDeskUpdateScreen : uint8_t {
    NONE = 0,
    PREPARING,
    COMPLETE,
};

// Pure command-to-screen mapping shared by the serial parser and native tests.
// A zero dismiss interval means the screen remains visible until reset.
bool echodesk_update_parse_command(const char *command,
                                   EchoDeskUpdateScreen &screen,
                                   uint32_t &dismiss_after_ms);

// Keeps the serial ACK coupled to a completed LVGL refresh rather than merely
// to command parsing.
bool echodesk_update_should_ack(bool request_pending, bool screen_rendered);

#endif // ECHODESK_UPDATE_H
