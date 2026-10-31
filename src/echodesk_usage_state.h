#ifndef ECHODESK_USAGE_STATE_H
#define ECHODESK_USAGE_STATE_H

#include <stddef.h>
#include <stdint.h>

// EchoDesk deliberately owns this display policy instead of teaching the
// schema-1 parser about UI states. Keeping it dependency-free also makes the
// five-minute safety boundary testable on the host.
constexpr uint32_t ECHODESK_CODEX_FRESH_MS = 5UL * 60UL * 1000UL;

enum class EchoDeskUsageDisplayState : uint8_t {
    NEVER_CONNECTED = 0,
    WAITING,
    AUTH_REQUIRED,
    ATTENTION,
    LIVE,
    STALE,
};

struct EchoDeskUsageStateInput {
    bool provider_found;
    bool usage_valid;
    bool notice_only;
    bool fetching;
    uint32_t now_ms;
    uint32_t last_fetch_ms;
    const char *message;
};

inline char echodesk_usage_ascii_lower(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline bool echodesk_usage_contains_ascii_ci(const char *text, const char *needle) {
    if (!text || !needle || needle[0] == '\0') return false;
    for (const char *start = text; *start; ++start) {
        size_t offset = 0;
        while (needle[offset] && start[offset] &&
               echodesk_usage_ascii_lower(start[offset]) ==
                   echodesk_usage_ascii_lower(needle[offset])) {
            ++offset;
        }
        if (needle[offset] == '\0') return true;
    }
    return false;
}

inline bool echodesk_usage_is_auth_message(const char *message) {
    return echodesk_usage_contains_ascii_ci(message, "sign in") ||
           echodesk_usage_contains_ascii_ci(message, "signin") ||
           echodesk_usage_contains_ascii_ci(message, "log in") ||
           echodesk_usage_contains_ascii_ci(message, "login") ||
           echodesk_usage_contains_ascii_ci(message, "auth") ||
           echodesk_usage_contains_ascii_ci(message, "session expired");
}

inline bool echodesk_usage_is_waiting_message(const char *message) {
    return echodesk_usage_contains_ascii_ci(message, "waiting") ||
           echodesk_usage_contains_ascii_ci(message, "loading") ||
           echodesk_usage_contains_ascii_ci(message, "fetching");
}

inline bool echodesk_usage_window_is_inactive(int64_t reset_epoch,
                                              uint32_t window_minutes) {
    return reset_epoch <= 0 && window_minutes == 0;
}

inline EchoDeskUsageDisplayState
echodesk_classify_usage_state(const EchoDeskUsageStateInput &input) {
    const bool has_message = input.message && input.message[0] != '\0';

    // A notice is a current host-reported state, not an expired usage sample.
    // Interpret it before consulting the timestamp retained from older data.
    if (input.provider_found && input.notice_only && has_message) {
        if (echodesk_usage_is_auth_message(input.message)) {
            return EchoDeskUsageDisplayState::AUTH_REQUIRED;
        }
        if (input.fetching || echodesk_usage_is_waiting_message(input.message)) {
            return EchoDeskUsageDisplayState::WAITING;
        }
        return EchoDeskUsageDisplayState::ATTENTION;
    }

    if (input.provider_found && input.fetching) {
        return EchoDeskUsageDisplayState::WAITING;
    }

    if (input.provider_found && input.usage_valid && input.last_fetch_ms != 0) {
        // Unsigned subtraction is intentional: millis() rollover stays safe.
        const uint32_t age_ms = input.now_ms - input.last_fetch_ms;
        return age_ms < ECHODESK_CODEX_FRESH_MS
                   ? EchoDeskUsageDisplayState::LIVE
                   : EchoDeskUsageDisplayState::STALE;
    }

    if (input.provider_found && has_message) {
        return echodesk_usage_is_auth_message(input.message)
                   ? EchoDeskUsageDisplayState::AUTH_REQUIRED
                   : EchoDeskUsageDisplayState::ATTENTION;
    }

    return EchoDeskUsageDisplayState::NEVER_CONNECTED;
}

#endif // ECHODESK_USAGE_STATE_H
