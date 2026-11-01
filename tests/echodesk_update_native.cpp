#include "../src/echodesk_update.h"

#include <stdio.h>

static bool check(bool condition, const char *message) {
    if (condition) return true;
    fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

int main() {
    EchoDeskUpdateScreen screen = EchoDeskUpdateScreen::NONE;
    uint32_t dismiss_after_ms = 123;

    if (!check(echodesk_update_parse_command("echodesk_prepare_update", screen,
                                             dismiss_after_ms),
               "prepare command accepted") ||
        !check(screen == EchoDeskUpdateScreen::PREPARING,
               "prepare screen selected") ||
        !check(dismiss_after_ms == 0, "prepare screen remains visible")) {
        return 1;
    }

    if (!check(echodesk_update_parse_command("echodesk_update_complete", screen,
                                             dismiss_after_ms),
               "complete command accepted") ||
        !check(screen == EchoDeskUpdateScreen::COMPLETE,
               "complete screen selected") ||
        !check(dismiss_after_ms == 3000, "complete screen timeout")) {
        return 1;
    }

    if (!check(!echodesk_update_parse_command(nullptr, screen, dismiss_after_ms),
               "null command rejected") ||
        !check(!echodesk_update_parse_command("reboot", screen,
                                              dismiss_after_ms),
               "unrelated command rejected")) {
        return 1;
    }

    if (!check(!echodesk_update_should_ack(true, false),
               "prepare ACK waits for rendered frame") ||
        !check(!echodesk_update_should_ack(false, true),
               "render without request is not acknowledged") ||
        !check(echodesk_update_should_ack(true, true),
               "rendered requested screen is acknowledged")) {
        return 1;
    }

    puts("echodesk_update_native: PASS");
    return 0;
}
