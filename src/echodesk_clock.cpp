#include "echodesk_clock.h"

#include <string.h>

namespace {

constexpr uint32_t DEFAULT_COUNTDOWN_SECONDS = 300;
constexpr uint16_t DEFAULT_FOCUS_MINUTES = 25;
constexpr uint16_t DEFAULT_BREAK_MINUTES = 5;
constexpr uint8_t DEFAULT_FOCUS_ROUNDS = 4;

struct StopwatchState {
    EchoDeskClockStatus status;
    uint64_t elapsed_ms;
    uint32_t last_ms;
};

struct CountdownState {
    EchoDeskClockStatus status;
    uint32_t duration_seconds;
    uint64_t elapsed_ms;
    uint32_t last_ms;
};

struct FocusState {
    EchoDeskClockStatus status;
    EchoDeskFocusPhase phase;
    uint16_t focus_minutes;
    uint16_t break_minutes;
    uint8_t rounds;
    uint8_t current_round;
    uint64_t segment_elapsed_ms;
    uint32_t last_ms;
};

EchoDeskClockMode active_mode = EchoDeskClockMode::TIME;
StopwatchState stopwatch{};
CountdownState countdown{};
FocusState focus{};
uint32_t revision = 1;

bool valid_mode(EchoDeskClockMode mode) {
    return mode >= EchoDeskClockMode::TIME && mode <= EchoDeskClockMode::FOCUS;
}

uint64_t seconds_to_ms(uint32_t seconds) {
    return static_cast<uint64_t>(seconds) * 1000ULL;
}

uint64_t focus_segment_duration_ms() {
    const uint32_t minutes = focus.phase == EchoDeskFocusPhase::FOCUS
        ? focus.focus_minutes : focus.break_minutes;
    return static_cast<uint64_t>(minutes) * 60000ULL;
}

void bump_revision() {
    ++revision;
    if (revision == 0) revision = 1;
}

void advance_stopwatch(uint32_t now_ms) {
    if (stopwatch.status != EchoDeskClockStatus::RUNNING) return;
    stopwatch.elapsed_ms += static_cast<uint32_t>(now_ms - stopwatch.last_ms);
    stopwatch.last_ms = now_ms;
}

void advance_countdown(uint32_t now_ms) {
    if (countdown.status != EchoDeskClockStatus::RUNNING) return;
    countdown.elapsed_ms += static_cast<uint32_t>(now_ms - countdown.last_ms);
    countdown.last_ms = now_ms;
    const uint64_t duration_ms = seconds_to_ms(countdown.duration_seconds);
    if (countdown.elapsed_ms >= duration_ms) {
        countdown.elapsed_ms = duration_ms;
        countdown.status = EchoDeskClockStatus::COMPLETE;
        bump_revision();
    }
}

void advance_focus_segment(bool preserve_running, uint32_t now_ms) {
    if (focus.phase == EchoDeskFocusPhase::FOCUS) {
        if (focus.current_round >= focus.rounds) {
            focus.status = EchoDeskClockStatus::COMPLETE;
            focus.segment_elapsed_ms = focus_segment_duration_ms();
            return;
        }
        focus.phase = EchoDeskFocusPhase::BREAK;
    } else {
        focus.phase = EchoDeskFocusPhase::FOCUS;
        ++focus.current_round;
    }
    focus.segment_elapsed_ms = 0;
    focus.status = preserve_running ? EchoDeskClockStatus::RUNNING
                                    : EchoDeskClockStatus::PAUSED;
    focus.last_ms = now_ms;
}

void advance_focus(uint32_t now_ms) {
    if (focus.status != EchoDeskClockStatus::RUNNING) return;
    focus.segment_elapsed_ms += static_cast<uint32_t>(now_ms - focus.last_ms);
    focus.last_ms = now_ms;
    while (focus.status == EchoDeskClockStatus::RUNNING) {
        const uint64_t duration_ms = focus_segment_duration_ms();
        if (focus.segment_elapsed_ms < duration_ms) break;
        const uint64_t overflow_ms = focus.segment_elapsed_ms - duration_ms;
        advance_focus_segment(true, now_ms);
        bump_revision();
        if (focus.status == EchoDeskClockStatus::RUNNING) {
            focus.segment_elapsed_ms = overflow_ms;
        }
    }
}

void advance_all(uint32_t now_ms) {
    advance_stopwatch(now_ms);
    advance_countdown(now_ms);
    advance_focus(now_ms);
}

uint32_t remaining_seconds(uint64_t duration_ms, uint64_t elapsed_ms) {
    if (elapsed_ms >= duration_ms) return 0;
    return static_cast<uint32_t>((duration_ms - elapsed_ms + 999ULL) / 1000ULL);
}

uint16_t progress_per_mille(uint64_t duration_ms, uint64_t elapsed_ms) {
    if (duration_ms == 0) return 0;
    if (elapsed_ms >= duration_ms) return 1000;
    return static_cast<uint16_t>((elapsed_ms * 1000ULL) / duration_ms);
}

EchoDeskClockResult start(EchoDeskClockMode mode, uint32_t now_ms) {
    switch (mode) {
        case EchoDeskClockMode::STOPWATCH:
            if (stopwatch.status == EchoDeskClockStatus::RUNNING) {
                if (active_mode != mode) {
                    active_mode = mode;
                    bump_revision();
                }
                return EchoDeskClockResult::OK;
            }
            stopwatch.status = EchoDeskClockStatus::RUNNING;
            stopwatch.last_ms = now_ms;
            break;
        case EchoDeskClockMode::COUNTDOWN:
            if (countdown.status == EchoDeskClockStatus::RUNNING) {
                if (active_mode != mode) {
                    active_mode = mode;
                    bump_revision();
                }
                return EchoDeskClockResult::OK;
            }
            if (countdown.status == EchoDeskClockStatus::COMPLETE) countdown.elapsed_ms = 0;
            countdown.status = EchoDeskClockStatus::RUNNING;
            countdown.last_ms = now_ms;
            break;
        case EchoDeskClockMode::FOCUS:
            if (focus.status == EchoDeskClockStatus::RUNNING) {
                if (active_mode != mode) {
                    active_mode = mode;
                    bump_revision();
                }
                return EchoDeskClockResult::OK;
            }
            if (focus.status == EchoDeskClockStatus::COMPLETE) {
                focus.phase = EchoDeskFocusPhase::FOCUS;
                focus.current_round = 1;
                focus.segment_elapsed_ms = 0;
            }
            focus.status = EchoDeskClockStatus::RUNNING;
            focus.last_ms = now_ms;
            break;
        default: return EchoDeskClockResult::INVALID_ACTION;
    }
    active_mode = mode;
    bump_revision();
    return EchoDeskClockResult::OK;
}

EchoDeskClockResult pause(EchoDeskClockMode mode) {
    EchoDeskClockStatus *status = nullptr;
    switch (mode) {
        case EchoDeskClockMode::STOPWATCH: status = &stopwatch.status; break;
        case EchoDeskClockMode::COUNTDOWN: status = &countdown.status; break;
        case EchoDeskClockMode::FOCUS: status = &focus.status; break;
        default: return EchoDeskClockResult::INVALID_ACTION;
    }
    if (*status == EchoDeskClockStatus::PAUSED) return EchoDeskClockResult::OK;
    if (*status != EchoDeskClockStatus::RUNNING) return EchoDeskClockResult::INVALID_STATE;
    *status = EchoDeskClockStatus::PAUSED;
    bump_revision();
    return EchoDeskClockResult::OK;
}

EchoDeskClockResult reset(EchoDeskClockMode mode) {
    switch (mode) {
        case EchoDeskClockMode::STOPWATCH:
            stopwatch.status = EchoDeskClockStatus::IDLE;
            stopwatch.elapsed_ms = 0;
            break;
        case EchoDeskClockMode::COUNTDOWN:
            countdown.status = EchoDeskClockStatus::IDLE;
            countdown.elapsed_ms = 0;
            break;
        case EchoDeskClockMode::FOCUS:
            focus.status = EchoDeskClockStatus::IDLE;
            focus.phase = EchoDeskFocusPhase::FOCUS;
            focus.current_round = 1;
            focus.segment_elapsed_ms = 0;
            break;
        default: return EchoDeskClockResult::INVALID_ACTION;
    }
    active_mode = mode;
    bump_revision();
    return EchoDeskClockResult::OK;
}

} // namespace

void echodesk_clock_init(uint32_t now_ms) {
    active_mode = EchoDeskClockMode::TIME;
    stopwatch = {EchoDeskClockStatus::IDLE, 0, now_ms};
    countdown = {EchoDeskClockStatus::IDLE, DEFAULT_COUNTDOWN_SECONDS, 0, now_ms};
    focus = {EchoDeskClockStatus::IDLE, EchoDeskFocusPhase::FOCUS,
             DEFAULT_FOCUS_MINUTES, DEFAULT_BREAK_MINUTES,
             DEFAULT_FOCUS_ROUNDS, 1, 0, now_ms};
    revision = 1;
}

EchoDeskClockResult echodesk_clock_apply(const EchoDeskClockCommand &command,
                                         uint32_t now_ms) {
    if (!valid_mode(command.mode)) return EchoDeskClockResult::INVALID_MODE;
    advance_all(now_ms);

    switch (command.action) {
        case EchoDeskClockAction::SELECT:
            if (active_mode != command.mode) {
                active_mode = command.mode;
                bump_revision();
            }
            return EchoDeskClockResult::OK;
        case EchoDeskClockAction::CONFIGURE:
            if (command.mode == EchoDeskClockMode::COUNTDOWN) {
                if (command.duration_seconds < ECHODESK_CLOCK_COUNTDOWN_MIN_SECONDS ||
                    command.duration_seconds > ECHODESK_CLOCK_COUNTDOWN_MAX_SECONDS) {
                    return EchoDeskClockResult::INVALID_CONFIG;
                }
                countdown.status = EchoDeskClockStatus::IDLE;
                countdown.duration_seconds = command.duration_seconds;
                countdown.elapsed_ms = 0;
                countdown.last_ms = now_ms;
            } else if (command.mode == EchoDeskClockMode::FOCUS) {
                if (command.focus_minutes < ECHODESK_CLOCK_FOCUS_MIN_MINUTES ||
                    command.focus_minutes > ECHODESK_CLOCK_FOCUS_MAX_MINUTES ||
                    command.break_minutes < ECHODESK_CLOCK_BREAK_MIN_MINUTES ||
                    command.break_minutes > ECHODESK_CLOCK_BREAK_MAX_MINUTES ||
                    command.rounds < ECHODESK_CLOCK_MIN_ROUNDS ||
                    command.rounds > ECHODESK_CLOCK_MAX_ROUNDS) {
                    return EchoDeskClockResult::INVALID_CONFIG;
                }
                focus.status = EchoDeskClockStatus::IDLE;
                focus.phase = EchoDeskFocusPhase::FOCUS;
                focus.focus_minutes = command.focus_minutes;
                focus.break_minutes = command.break_minutes;
                focus.rounds = command.rounds;
                focus.current_round = 1;
                focus.segment_elapsed_ms = 0;
                focus.last_ms = now_ms;
            } else {
                return EchoDeskClockResult::INVALID_ACTION;
            }
            active_mode = command.mode;
            bump_revision();
            return EchoDeskClockResult::OK;
        case EchoDeskClockAction::START: return start(command.mode, now_ms);
        case EchoDeskClockAction::PAUSE: return pause(command.mode);
        case EchoDeskClockAction::RESET: return reset(command.mode);
        case EchoDeskClockAction::SKIP:
            if (command.mode != EchoDeskClockMode::FOCUS) {
                return EchoDeskClockResult::INVALID_ACTION;
            }
            if (focus.status != EchoDeskClockStatus::RUNNING &&
                focus.status != EchoDeskClockStatus::PAUSED) {
                return EchoDeskClockResult::INVALID_STATE;
            }
            {
                const bool running = focus.status == EchoDeskClockStatus::RUNNING;
                advance_focus_segment(running, now_ms);
                active_mode = EchoDeskClockMode::FOCUS;
                bump_revision();
            }
            return EchoDeskClockResult::OK;
    }
    return EchoDeskClockResult::INVALID_ACTION;
}

void echodesk_clock_capture(uint32_t now_ms, EchoDeskClockSnapshot &out) {
    advance_all(now_ms);
    out.revision = revision;
    out.active_mode = active_mode;
    out.stopwatch = {stopwatch.status, stopwatch.elapsed_ms};

    const uint64_t countdown_duration_ms = seconds_to_ms(countdown.duration_seconds);
    out.countdown = {
        countdown.status,
        countdown.duration_seconds,
        remaining_seconds(countdown_duration_ms, countdown.elapsed_ms),
        progress_per_mille(countdown_duration_ms, countdown.elapsed_ms),
    };

    const uint64_t focus_duration_ms = focus_segment_duration_ms();
    out.focus = {
        focus.status,
        focus.phase,
        focus.focus_minutes,
        focus.break_minutes,
        focus.rounds,
        focus.current_round,
        remaining_seconds(focus_duration_ms, focus.segment_elapsed_ms),
        progress_per_mille(focus_duration_ms, focus.segment_elapsed_ms),
    };
}

const char *echodesk_clock_mode_name(EchoDeskClockMode mode) {
    switch (mode) {
        case EchoDeskClockMode::TIME: return "time";
        case EchoDeskClockMode::STOPWATCH: return "stopwatch";
        case EchoDeskClockMode::COUNTDOWN: return "countdown";
        case EchoDeskClockMode::FOCUS: return "focus";
    }
    return "time";
}

const char *echodesk_clock_status_name(EchoDeskClockStatus status) {
    switch (status) {
        case EchoDeskClockStatus::IDLE: return "idle";
        case EchoDeskClockStatus::RUNNING: return "running";
        case EchoDeskClockStatus::PAUSED: return "paused";
        case EchoDeskClockStatus::COMPLETE: return "complete";
    }
    return "idle";
}

const char *echodesk_focus_phase_name(EchoDeskFocusPhase phase) {
    return phase == EchoDeskFocusPhase::BREAK ? "break" : "focus";
}

bool echodesk_clock_parse_mode(const char *value, EchoDeskClockMode &out) {
    if (!value) return false;
    if (strcmp(value, "time") == 0) out = EchoDeskClockMode::TIME;
    else if (strcmp(value, "stopwatch") == 0) out = EchoDeskClockMode::STOPWATCH;
    else if (strcmp(value, "countdown") == 0) out = EchoDeskClockMode::COUNTDOWN;
    else if (strcmp(value, "focus") == 0) out = EchoDeskClockMode::FOCUS;
    else return false;
    return true;
}

bool echodesk_clock_parse_action(const char *value, EchoDeskClockAction &out) {
    if (!value) return false;
    if (strcmp(value, "select") == 0) out = EchoDeskClockAction::SELECT;
    else if (strcmp(value, "configure") == 0) out = EchoDeskClockAction::CONFIGURE;
    else if (strcmp(value, "start") == 0) out = EchoDeskClockAction::START;
    else if (strcmp(value, "pause") == 0) out = EchoDeskClockAction::PAUSE;
    else if (strcmp(value, "reset") == 0) out = EchoDeskClockAction::RESET;
    else if (strcmp(value, "skip") == 0) out = EchoDeskClockAction::SKIP;
    else return false;
    return true;
}
