#include "performance_data.h"

#include <math.h>
#include <string.h>

namespace {

constexpr uint64_t MAX_NETWORK_BPS = 1000000000000000ULL; // 1 PB/s sanity cap
constexpr float MIN_TEMPERATURE_C = -40.0f;
constexpr float MAX_TEMPERATURE_C = 150.0f;

#ifndef PERF_CPU_WARM_C
#define PERF_CPU_WARM_C 70.0f
#endif
#ifndef PERF_CPU_HOT_C
#define PERF_CPU_HOT_C 85.0f
#endif
#ifndef PERF_CPU_CRITICAL_C
#define PERF_CPU_CRITICAL_C 95.0f
#endif
#ifndef PERF_GPU_WARM_C
#define PERF_GPU_WARM_C 75.0f
#endif
#ifndef PERF_GPU_HOT_C
#define PERF_GPU_HOT_C 85.0f
#endif
#ifndef PERF_GPU_CRITICAL_C
#define PERF_GPU_CRITICAL_C 95.0f
#endif
#ifndef PERF_BOARD_WARM_C
#define PERF_BOARD_WARM_C 55.0f
#endif
#ifndef PERF_BOARD_HOT_C
#define PERF_BOARD_HOT_C 70.0f
#endif
#ifndef PERF_BOARD_CRITICAL_C
#define PERF_BOARD_CRITICAL_C 85.0f
#endif

PerformanceSample history[PERFORMANCE_HISTORY_CAPACITY];
size_t history_head = 0; // next write position
size_t history_size = 0;
bool explicitly_disconnected = false;

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

bool valid_percentage(float value) {
    return isfinite(value) && value >= 0.0f && value <= 100.0f;
}

bool valid_temperature(float value) {
    return isfinite(value) && value >= MIN_TEMPERATURE_C && value <= MAX_TEMPERATURE_C;
}

#if !defined(PERFORMANCE_DATA_NO_JSON)
bool parse_required_float(JsonVariantConst value, float &out) {
    if (!value.is<float>()) return false;
    out = value.as<float>();
    return isfinite(out);
}

bool parse_required_u64(JsonVariantConst value, uint64_t &out) {
    if (!value.is<uint64_t>()) return false;
    out = value.as<uint64_t>();
    return true;
}

bool parse_optional_float(JsonVariantConst value, float &out, uint8_t &mask,
                          uint8_t bit) {
    if (value.isNull()) return true;
    if (!parse_required_float(value, out)) return false;
    mask |= bit;
    return true;
}

bool parse_optional_u64(JsonVariantConst value, uint64_t &out, uint8_t &mask,
                        uint8_t bit) {
    if (value.isNull()) return true;
    if (!parse_required_u64(value, out)) return false;
    mask |= bit;
    return true;
}
#endif

void append_sample(const PerformanceSample &sample) {
    history[history_head] = sample;
    history_head = (history_head + 1) % PERFORMANCE_HISTORY_CAPACITY;
    if (history_size < PERFORMANCE_HISTORY_CAPACITY) ++history_size;
    explicitly_disconnected = false;
}

} // namespace

void performance_data_init() {
    memset(history, 0, sizeof(history));
    history_head = 0;
    history_size = 0;
    explicitly_disconnected = false;
}

#if !defined(PERFORMANCE_DATA_NO_JSON)
PerformanceParseResult performance_data_parse_json(
    JsonVariantConst root,
    uint32_t now_ms,
    const char **error_out
) {
    static const char *error = nullptr;
    error = nullptr;
    if (error_out) *error_out = nullptr;

    const char *type = root["type"] | "";
    if (strcmp(type, "performance") != 0) {
        return PerformanceParseResult::NOT_PERFORMANCE;
    }

    auto reject = [&](const char *message) {
        error = message;
        if (error_out) *error_out = error;
        return PerformanceParseResult::REJECTED;
    };

    if (!root["version"].is<uint8_t>() ||
        root["version"].as<uint8_t>() != PERFORMANCE_PROTOCOL_VERSION) {
        return reject("unsupported performance version");
    }

    if (root["connected"].is<bool>() && !root["connected"].as<bool>()) {
        performance_data_mark_disconnected();
        return PerformanceParseResult::ACCEPTED;
    }

    PerformanceSample sample{};
    if (!root["timestamp"].is<uint32_t>()) {
        return reject("invalid performance timestamp");
    }
    sample.timestamp_epoch = root["timestamp"].as<uint32_t>();
    sample.received_ms = now_ms;

    if (!parse_optional_float(root["cpuPct"], sample.cpu_pct,
                              sample.metric_valid_mask, PERF_METRIC_CPU) ||
        !parse_optional_float(root["gpuPct"], sample.gpu_pct,
                              sample.metric_valid_mask, PERF_METRIC_GPU) ||
        !parse_optional_float(root["ramPct"], sample.ram_pct,
                              sample.metric_valid_mask, PERF_METRIC_RAM)) {
        return reject("invalid performance percentage");
    }

    JsonVariantConst ram_used = root["ramUsedBytes"];
    JsonVariantConst ram_total = root["ramTotalBytes"];
    if (ram_used.isNull() != ram_total.isNull()) {
        return reject("RAM byte fields must be paired");
    }
    if (!ram_used.isNull() &&
        (!parse_required_u64(ram_used, sample.ram_used_bytes) ||
         !parse_required_u64(ram_total, sample.ram_total_bytes))) {
        return reject("invalid RAM byte fields");
    }

    JsonVariantConst network = root["network"];
    if (!network.is<JsonObjectConst>() ||
        !parse_optional_u64(network["downBps"], sample.network_down_bps,
                            sample.metric_valid_mask, PERF_METRIC_NETWORK_DOWN) ||
        !parse_optional_u64(network["upBps"], sample.network_up_bps,
                            sample.metric_valid_mask, PERF_METRIC_NETWORK_UP)) {
        return reject("invalid performance network rates");
    }

    JsonVariantConst temperature = root["temperature"];
    if (!temperature.isNull()) {
        if (!temperature.is<JsonObjectConst>()) {
            return reject("invalid performance temperature object");
        }
        struct TemperatureField {
            const char *key;
            float *destination;
            uint8_t mask;
        };
        TemperatureField fields[] = {
            {"cpuC", &sample.cpu_temp_c, PERF_TEMP_CPU},
            {"gpuC", &sample.gpu_temp_c, PERF_TEMP_GPU},
            {"motherboardC", &sample.motherboard_temp_c, PERF_TEMP_MOTHERBOARD},
        };
        for (const TemperatureField &field : fields) {
            JsonVariantConst value = temperature[field.key];
            if (value.isNull()) continue;
            if (!parse_required_float(value, *field.destination)) {
                return reject("invalid performance temperature");
            }
            sample.temperature_valid_mask |= field.mask;
        }
    }

    if (!performance_data_ingest(sample, now_ms)) {
        return reject("performance values out of range");
    }
    return PerformanceParseResult::ACCEPTED;
}
#endif

bool performance_data_ingest(const PerformanceSample &input, uint32_t now_ms) {
    PerformanceSample sample = input;
    sample.received_ms = now_ms;
    if (!performance_validate_sample(sample)) return false;
    append_sample(sample);
    return true;
}

void performance_data_mark_disconnected() {
    explicitly_disconnected = true;
}

PerformanceConnectionState performance_data_connection_state(uint32_t now_ms) {
    if (explicitly_disconnected) return PerformanceConnectionState::DISCONNECTED;
    if (history_size == 0) return PerformanceConnectionState::NEVER_RECEIVED;

    const PerformanceSample &latest = history[(history_head + PERFORMANCE_HISTORY_CAPACITY - 1) %
                                               PERFORMANCE_HISTORY_CAPACITY];
    const uint32_t elapsed = now_ms - latest.received_ms;
    if (elapsed <= PERFORMANCE_STALE_AFTER_MS) return PerformanceConnectionState::LIVE;
    if (elapsed <= PERFORMANCE_DISCONNECTED_AFTER_MS) return PerformanceConnectionState::STALE;
    return PerformanceConnectionState::DISCONNECTED;
}

bool performance_data_get_latest(PerformanceSample &out) {
    if (history_size == 0) return false;
    out = history[(history_head + PERFORMANCE_HISTORY_CAPACITY - 1) %
                  PERFORMANCE_HISTORY_CAPACITY];
    return true;
}

size_t performance_data_history_count() {
    return history_size;
}

bool performance_data_get_history(size_t oldest_index, PerformanceSample &out) {
    if (oldest_index >= history_size) return false;
    const size_t oldest = (history_head + PERFORMANCE_HISTORY_CAPACITY - history_size) %
                          PERFORMANCE_HISTORY_CAPACITY;
    out = history[(oldest + oldest_index) % PERFORMANCE_HISTORY_CAPACITY];
    return true;
}

bool performance_validate_sample(const PerformanceSample &sample) {
    constexpr uint8_t ALL_METRICS = PERF_METRIC_CPU | PERF_METRIC_GPU |
        PERF_METRIC_RAM | PERF_METRIC_NETWORK_DOWN | PERF_METRIC_NETWORK_UP;
    if (sample.timestamp_epoch == 0 ||
        (sample.metric_valid_mask & ~ALL_METRICS) ||
        ((sample.metric_valid_mask & PERF_METRIC_CPU) && !valid_percentage(sample.cpu_pct)) ||
        ((sample.metric_valid_mask & PERF_METRIC_GPU) && !valid_percentage(sample.gpu_pct)) ||
        ((sample.metric_valid_mask & PERF_METRIC_RAM) && !valid_percentage(sample.ram_pct)) ||
        sample.network_down_bps > MAX_NETWORK_BPS ||
        sample.network_up_bps > MAX_NETWORK_BPS ||
        (sample.ram_total_bytes == 0 && sample.ram_used_bytes != 0) ||
        (sample.ram_total_bytes != 0 && sample.ram_used_bytes > sample.ram_total_bytes) ||
        (sample.temperature_valid_mask & ~(PERF_TEMP_CPU | PERF_TEMP_GPU | PERF_TEMP_MOTHERBOARD))) {
        return false;
    }
    if ((sample.temperature_valid_mask & PERF_TEMP_CPU) && !valid_temperature(sample.cpu_temp_c)) return false;
    if ((sample.temperature_valid_mask & PERF_TEMP_GPU) && !valid_temperature(sample.gpu_temp_c)) return false;
    if ((sample.temperature_valid_mask & PERF_TEMP_MOTHERBOARD) && !valid_temperature(sample.motherboard_temp_c)) return false;
    return true;
}

ThermalThresholds performance_thermal_thresholds(uint8_t component_bit) {
    switch (component_bit) {
        case PERF_TEMP_GPU: return {PERF_GPU_WARM_C, PERF_GPU_HOT_C, PERF_GPU_CRITICAL_C};
        case PERF_TEMP_MOTHERBOARD: return {PERF_BOARD_WARM_C, PERF_BOARD_HOT_C, PERF_BOARD_CRITICAL_C};
        case PERF_TEMP_CPU:
        default: return {PERF_CPU_WARM_C, PERF_CPU_HOT_C, PERF_CPU_CRITICAL_C};
    }
}

float performance_thermal_pressure(float temperature_c, ThermalThresholds thresholds) {
    if (!isfinite(temperature_c) || !isfinite(thresholds.warm_c) ||
        !isfinite(thresholds.hot_c) || !isfinite(thresholds.critical_c) ||
        thresholds.warm_c >= thresholds.hot_c || thresholds.hot_c >= thresholds.critical_c) {
        return 0.0f;
    }
    const float normal_floor = thresholds.warm_c - 20.0f;
    if (temperature_c <= normal_floor) return 0.0f;
    if (temperature_c < thresholds.warm_c) {
        return ((temperature_c - normal_floor) / (thresholds.warm_c - normal_floor)) / 3.0f;
    }
    if (temperature_c < thresholds.hot_c) {
        return (1.0f + (temperature_c - thresholds.warm_c) /
               (thresholds.hot_c - thresholds.warm_c)) / 3.0f;
    }
    if (temperature_c < thresholds.critical_c) {
        return (2.0f + (temperature_c - thresholds.hot_c) /
               (thresholds.critical_c - thresholds.hot_c)) / 3.0f;
    }
    return 1.0f;
}

float performance_highest_thermal_pressure(const PerformanceSample &sample) {
    float highest = 0.0f;
    if (sample.temperature_valid_mask & PERF_TEMP_CPU) {
        highest = performance_thermal_pressure(sample.cpu_temp_c,
                                                performance_thermal_thresholds(PERF_TEMP_CPU));
    }
    if (sample.temperature_valid_mask & PERF_TEMP_GPU) {
        const float value = performance_thermal_pressure(sample.gpu_temp_c,
                                                         performance_thermal_thresholds(PERF_TEMP_GPU));
        if (value > highest) highest = value;
    }
    if (sample.temperature_valid_mask & PERF_TEMP_MOTHERBOARD) {
        const float value = performance_thermal_pressure(
            sample.motherboard_temp_c,
            performance_thermal_thresholds(PERF_TEMP_MOTHERBOARD));
        if (value > highest) highest = value;
    }
    return highest;
}

float performance_smooth_pressure(float previous, float current,
                                  float rise_alpha, float fall_alpha) {
    previous = clamp01(previous);
    current = clamp01(current);
    rise_alpha = clamp01(rise_alpha);
    fall_alpha = clamp01(fall_alpha);
    const float alpha = current >= previous ? rise_alpha : fall_alpha;
    return clamp01(previous + (current - previous) * alpha);
}

ThermalLevel performance_thermal_level_with_hysteresis(
    float pressure,
    ThermalLevel previous,
    float hysteresis
) {
    pressure = clamp01(pressure);
    if (!isfinite(hysteresis)) hysteresis = 0.0f;
    if (hysteresis < 0.0f) hysteresis = 0.0f;
    if (hysteresis > 0.20f) hysteresis = 0.20f;

    constexpr float WARM_BOUNDARY = 1.0f / 3.0f;
    constexpr float HOT_BOUNDARY = 2.0f / 3.0f;
    constexpr float CRITICAL_BOUNDARY = 0.99f;

    ThermalLevel raw = ThermalLevel::NORMAL;
    if (pressure >= CRITICAL_BOUNDARY) raw = ThermalLevel::CRITICAL;
    else if (pressure >= HOT_BOUNDARY) raw = ThermalLevel::HOT;
    else if (pressure >= WARM_BOUNDARY) raw = ThermalLevel::WARM;

    if (static_cast<uint8_t>(raw) >= static_cast<uint8_t>(previous)) return raw;

    float release_boundary = 0.0f;
    switch (previous) {
        case ThermalLevel::CRITICAL: release_boundary = CRITICAL_BOUNDARY; break;
        case ThermalLevel::HOT: release_boundary = HOT_BOUNDARY; break;
        case ThermalLevel::WARM: release_boundary = WARM_BOUNDARY; break;
        case ThermalLevel::NORMAL: return raw;
    }
    return pressure >= release_boundary - hysteresis ? previous : raw;
}

NetworkScaledRate performance_scale_network_bps(uint64_t bytes_per_second) {
    const double bits_per_second = static_cast<double>(bytes_per_second) * 8.0;
    NetworkScaledRate scaled{static_cast<float>(bits_per_second), NetworkUnit::BPS};
    if (bits_per_second >= 1000000000.0) {
        scaled.value = static_cast<float>(bits_per_second / 1000000000.0);
        scaled.unit = NetworkUnit::GBPS;
    } else if (bits_per_second >= 1000000.0) {
        scaled.value = static_cast<float>(bits_per_second / 1000000.0);
        scaled.unit = NetworkUnit::MBPS;
    } else if (bits_per_second >= 1000.0) {
        scaled.value = static_cast<float>(bits_per_second / 1000.0);
        scaled.unit = NetworkUnit::KBPS;
    }
    return scaled;
}

const char *performance_network_unit_label(NetworkUnit unit) {
    switch (unit) {
        case NetworkUnit::GBPS: return "Gbps";
        case NetworkUnit::MBPS: return "Mbps";
        case NetworkUnit::KBPS: return "Kbps";
        case NetworkUnit::BPS:
        default: return "bps";
    }
}

uint64_t performance_network_graph_ceiling_bps(uint64_t down_bps, uint64_t up_bps) {
    uint64_t target = down_bps > up_bps ? down_bps : up_bps;
    if (target < 1000ULL) return 1000ULL;

    uint64_t magnitude = 1;
    while (target / magnitude >= 10ULL && magnitude <= UINT64_MAX / 10ULL) {
        magnitude *= 10ULL;
    }
    const uint64_t leading = (target + magnitude - 1ULL) / magnitude;
    uint64_t nice_leading = leading <= 1ULL ? 1ULL : leading <= 2ULL ? 2ULL : leading <= 5ULL ? 5ULL : 10ULL;
    if (nice_leading > UINT64_MAX / magnitude) return UINT64_MAX;
    return nice_leading * magnitude;
}
