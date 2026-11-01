#include "../src/echodesk_clock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static EchoDeskClockResult apply(EchoDeskClockAction action,
                                 EchoDeskClockMode mode,
                                 uint32_t now_ms) {
    EchoDeskClockCommand command{};
    command.action = action;
    command.mode = mode;
    return echodesk_clock_apply(command, now_ms);
}

int main() {
    EchoDeskClockSnapshot snapshot{};
    echodesk_clock_init(1000);
    echodesk_clock_capture(1000, snapshot);
    assert(snapshot.revision == 1);
    assert(snapshot.active_mode == EchoDeskClockMode::TIME);
    assert(snapshot.countdown.duration_seconds == 300);
    assert(snapshot.focus.focus_minutes == 25);
    assert(snapshot.focus.break_minutes == 5);
    assert(snapshot.focus.rounds == 4);

    // Stopwatch accumulation is monotonic and remains correct across millis()
    // wrapping as long as the module is sampled at least once per wrap period.
    echodesk_clock_init(UINT32_MAX - 500);
    assert(apply(EchoDeskClockAction::START, EchoDeskClockMode::STOPWATCH,
                 UINT32_MAX - 500) == EchoDeskClockResult::OK);
    echodesk_clock_capture(500, snapshot);
    assert(snapshot.stopwatch.elapsed_ms == 1001);
    assert(apply(EchoDeskClockAction::PAUSE, EchoDeskClockMode::STOPWATCH, 750) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(4000, snapshot);
    assert(snapshot.stopwatch.elapsed_ms == 1251);
    assert(apply(EchoDeskClockAction::RESET, EchoDeskClockMode::STOPWATCH, 4000) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(4000, snapshot);
    assert(snapshot.stopwatch.status == EchoDeskClockStatus::IDLE);
    assert(snapshot.stopwatch.elapsed_ms == 0);

    echodesk_clock_init(0);
    EchoDeskClockCommand configure{};
    configure.action = EchoDeskClockAction::CONFIGURE;
    configure.mode = EchoDeskClockMode::COUNTDOWN;
    configure.duration_seconds = 10;
    assert(echodesk_clock_apply(configure, 0) == EchoDeskClockResult::OK);
    assert(apply(EchoDeskClockAction::START, EchoDeskClockMode::COUNTDOWN, 0) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(2500, snapshot);
    assert(snapshot.countdown.remaining_seconds == 8);
    assert(snapshot.countdown.progress_per_mille == 250);
    echodesk_clock_capture(10000, snapshot);
    assert(snapshot.countdown.status == EchoDeskClockStatus::COMPLETE);
    assert(snapshot.countdown.remaining_seconds == 0);
    assert(snapshot.countdown.progress_per_mille == 1000);
    assert(apply(EchoDeskClockAction::START, EchoDeskClockMode::COUNTDOWN, 11000) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(12000, snapshot);
    assert(snapshot.countdown.remaining_seconds == 9);

    configure.duration_seconds = 0;
    const uint32_t revision_before_invalid = snapshot.revision;
    assert(echodesk_clock_apply(configure, 12000) ==
           EchoDeskClockResult::INVALID_CONFIG);
    echodesk_clock_capture(12000, snapshot);
    assert(snapshot.revision == revision_before_invalid);

    echodesk_clock_init(0);
    configure = {};
    configure.action = EchoDeskClockAction::CONFIGURE;
    configure.mode = EchoDeskClockMode::FOCUS;
    configure.focus_minutes = 1;
    configure.break_minutes = 1;
    configure.rounds = 2;
    assert(echodesk_clock_apply(configure, 0) == EchoDeskClockResult::OK);
    assert(apply(EchoDeskClockAction::START, EchoDeskClockMode::FOCUS, 0) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(60000, snapshot);
    assert(snapshot.focus.phase == EchoDeskFocusPhase::BREAK);
    assert(snapshot.focus.current_round == 1);
    assert(snapshot.focus.remaining_seconds == 60);
    echodesk_clock_capture(120000, snapshot);
    assert(snapshot.focus.phase == EchoDeskFocusPhase::FOCUS);
    assert(snapshot.focus.current_round == 2);
    assert(apply(EchoDeskClockAction::SKIP, EchoDeskClockMode::FOCUS, 120000) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(120000, snapshot);
    assert(snapshot.focus.status == EchoDeskClockStatus::COMPLETE);
    assert(snapshot.focus.progress_per_mille == 1000);

    echodesk_clock_init(0);
    configure.focus_minutes = 1;
    configure.break_minutes = 1;
    configure.rounds = 3;
    assert(echodesk_clock_apply(configure, 0) == EchoDeskClockResult::OK);
    assert(apply(EchoDeskClockAction::START, EchoDeskClockMode::FOCUS, 0) ==
           EchoDeskClockResult::OK);
    assert(apply(EchoDeskClockAction::PAUSE, EchoDeskClockMode::FOCUS, 1000) ==
           EchoDeskClockResult::OK);
    assert(apply(EchoDeskClockAction::SKIP, EchoDeskClockMode::FOCUS, 2000) ==
           EchoDeskClockResult::OK);
    echodesk_clock_capture(5000, snapshot);
    assert(snapshot.focus.status == EchoDeskClockStatus::PAUSED);
    assert(snapshot.focus.phase == EchoDeskFocusPhase::BREAK);
    assert(snapshot.focus.remaining_seconds == 60);

    assert(strcmp(echodesk_clock_mode_name(EchoDeskClockMode::COUNTDOWN),
                  "countdown") == 0);
    EchoDeskClockMode mode{};
    assert(echodesk_clock_parse_mode("focus", mode));
    assert(mode == EchoDeskClockMode::FOCUS);
    assert(!echodesk_clock_parse_mode("alarm", mode));
    EchoDeskClockAction action{};
    assert(echodesk_clock_parse_action("configure", action));
    assert(action == EchoDeskClockAction::CONFIGURE);

    puts("echodesk_clock_native: PASS");
    return 0;
}
