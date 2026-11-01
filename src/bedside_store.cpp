#include "bedside_store.h"

#if defined(ARDUINO)
#include <Preferences.h>

namespace {
constexpr const char *NAMESPACE = "echo_bedside";
constexpr const char *STATE_KEY = "state_v1";

struct BedsidePersistedStateV1 {
    uint32_t revision;
    BedsideConfig config;
};
}

bool bedside_store_load(BedsideConfig &config, uint32_t &revision) {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) return false;
    BedsidePersistedStateV1 state{};
    const size_t size = preferences.getBytesLength(STATE_KEY);
    const bool read = size == sizeof(state) &&
                      preferences.getBytes(STATE_KEY, &state, sizeof(state)) ==
                          sizeof(state);
    preferences.end();
    if (!read || state.revision == 0 ||
        !bedside_validate_config(state.config)) return false;
    config = state.config;
    revision = state.revision;
    return true;
}

bool bedside_store_save(const BedsideConfig &config, uint32_t revision) {
    if (!bedside_validate_config(config) || revision == 0) return false;
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) return false;
    const BedsidePersistedStateV1 state{revision, config};
    // Persist revision and configuration as one blob so a power loss cannot
    // leave a new config paired with an old revision (or vice versa).
    const bool saved = preferences.putBytes(STATE_KEY, &state, sizeof(state)) ==
                       sizeof(state);
    preferences.end();
    return saved;
}
#else
bool bedside_store_load(BedsideConfig &, uint32_t &) { return false; }
bool bedside_store_save(const BedsideConfig &, uint32_t) { return false; }
#endif
