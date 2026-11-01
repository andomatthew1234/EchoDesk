#ifndef ECHODESK_CLOCK_H
#define ECHODESK_CLOCK_H

#include <stdint.h>

constexpr uint8_t ECHODESK_CLOCK_PROTOCOL_VERSION = 1;
constexpr uint32_t ECHODESK_CLOCK_COUNTDOWN_MIN_SECONDS = 1;
constexpr uint32_t ECHODESK_CLOCK_COUNTDOWN_MAX_SECONDS = 86400;
constexpr uint16_t ECHODESK_CLOCK_FOCUS_MIN_MINUTES = 1;
constexpr uint16_t ECHODESK_CLOCK_FOCUS_MAX_MINUTES = 180;
constexpr uint16_t ECHODESK_CLOCK_BREAK_MIN_MINUTES = 1;
constexpr uint16_t ECHODESK_CLOCK_BREAK_MAX_MINUTES = 60;
constexpr uint8_t ECHODESK_CLOCK_MIN_ROUNDS = 1;
constexpr uint8_t ECHODESK_CLOCK_MAX_ROUNDS = 12;

enum class EchoDeskClockMode : uint8_t {
    TIME = 0,
    STOPWATCH,
    COUNTDOWN,
    FOCUS,
};

enum class EchoDeskClockStatus : uint8_t {
    IDLE = 0,
    RUNNING,
    PAUSED,
    COMPLETE,
};

enum class EchoDeskFocusPhase : uint8_t {
    FOCUS = 0,
    BREAK,
};

enum class EchoDeskClockAction : uint8_t {
    SELECT = 0,
    CONFIGURE,
    START,
    PAUSE,
    RESET,
    SKIP,
};

enum class EchoDeskClockResult : uint8_t {
    OK = 0,
    INVALID_MODE,
    INVALID_ACTION,
    INVALID_CONFIG,
    INVALID_STATE,
};

struct EchoDeskClockCommand {
    EchoDeskClockAction action = EchoDeskClockAction::SELECT;
    EchoDeskClockMode mode = EchoDeskClockMode::TIME;
    uint32_t duration_seconds = 0;
    uint16_t focus_minutes = 0;
    uint16_t break_minutes = 0;
    uint8_t rounds = 0;
};

struct EchoDeskStopwatchSnapshot {
    EchoDeskClockStatus status;
    uint64_t elapsed_ms;
};

struct EchoDeskCountdownSnapshot {
    EchoDeskClockStatus status;
    uint32_t duration_seconds;
    uint32_t remaining_seconds;
    uint16_t progress_per_mille;
};

struct EchoDeskFocusSnapshot {
    EchoDeskClockStatus status;
    EchoDeskFocusPhase phase;
    uint16_t focus_minutes;
    uint16_t break_minutes;
    uint8_t rounds;
    uint8_t current_round;
    uint32_t remaining_seconds;
    uint16_t progress_per_mille;
};

struct EchoDeskClockSnapshot {
    uint32_t revision;
    EchoDeskClockMode active_mode;
    EchoDeskStopwatchSnapshot stopwatch;
    EchoDeskCountdownSnapshot countdown;
    EchoDeskFocusSnapshot focus;
};

// Runtime state is deliberately independent from LVGL and transport. Callers
// supply the monotonic millisecond clock, making the interface deterministic in
// native tests and safe across the 32-bit millis() wrap.
void echodesk_clock_init(uint32_t now_ms);
EchoDeskClockResult echodesk_clock_apply(const EchoDeskClockCommand &command,
                                         uint32_t now_ms);
void echodesk_clock_capture(uint32_t now_ms, EchoDeskClockSnapshot &out);

const char *echodesk_clock_mode_name(EchoDeskClockMode mode);
const char *echodesk_clock_status_name(EchoDeskClockStatus status);
const char *echodesk_focus_phase_name(EchoDeskFocusPhase phase);
bool echodesk_clock_parse_mode(const char *value, EchoDeskClockMode &out);
bool echodesk_clock_parse_action(const char *value, EchoDeskClockAction &out);

#endif // ECHODESK_CLOCK_H
