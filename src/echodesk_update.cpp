#include "echodesk_update.h"

#include <string.h>

bool echodesk_update_parse_command(const char *command,
                                   EchoDeskUpdateScreen &screen,
                                   uint32_t &dismiss_after_ms) {
    if (!command) return false;
    if (strcmp(command, "echodesk_prepare_update") == 0) {
        screen = EchoDeskUpdateScreen::PREPARING;
        dismiss_after_ms = 0;
        return true;
    }
    if (strcmp(command, "echodesk_update_complete") == 0) {
        screen = EchoDeskUpdateScreen::COMPLETE;
        dismiss_after_ms = 3000;
        return true;
    }
    return false;
}

bool echodesk_update_should_ack(bool request_pending, bool screen_rendered) {
    return request_pending && screen_rendered;
}
