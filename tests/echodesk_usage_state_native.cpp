#include "../src/echodesk_usage_state.h"

#include <assert.h>
#include <stdio.h>

static EchoDeskUsageStateInput input(bool found, bool valid, bool notice,
                                     bool fetching, uint32_t now,
                                     uint32_t fetched, const char *message) {
    return {found, valid, notice, fetching, now, fetched, message};
}

int main() {
    using State = EchoDeskUsageDisplayState;

    assert(echodesk_classify_usage_state(
               input(false, false, false, false, 1000, 0, "")) ==
           State::NEVER_CONNECTED);
    assert(echodesk_classify_usage_state(
               input(true, false, true, false, 1000, 0, "Loading provider ...")) ==
           State::WAITING);
    assert(echodesk_classify_usage_state(
               input(true, false, false, true, 1000, 0, "")) ==
           State::WAITING);
    assert(echodesk_classify_usage_state(
               input(true, false, true, false, 1000, 0, "Sign in to Codex")) ==
           State::AUTH_REQUIRED);
    assert(echodesk_classify_usage_state(
               input(true, false, true, false, 1000, 0, "Rate limit unavailable")) ==
           State::ATTENTION);

    assert(echodesk_classify_usage_state(
               input(true, true, false, false, ECHODESK_CODEX_FRESH_MS, 1, "")) ==
           State::LIVE);
    assert(echodesk_classify_usage_state(
               input(true, true, false, false, ECHODESK_CODEX_FRESH_MS + 1, 1, "")) ==
           State::STALE);

    // A notice remains a current attention state even if an earlier successful
    // sample has an old timestamp. Its old bars must not reappear.
    assert(echodesk_classify_usage_state(
               input(true, true, true, false, ECHODESK_CODEX_FRESH_MS + 50, 1,
                     "Codex account needs attention")) == State::ATTENTION);

    // Unsigned millis arithmetic remains correct across rollover.
    assert(echodesk_classify_usage_state(
               input(true, true, false, false, 25, 0xFFFFFFF0u, "")) ==
           State::LIVE);

    assert(echodesk_usage_window_is_inactive(0, 0));
    assert(!echodesk_usage_window_is_inactive(1700000000, 0));
    assert(!echodesk_usage_window_is_inactive(0, 300));

    puts("echodesk_usage_state_native: PASS");
    return 0;
}
