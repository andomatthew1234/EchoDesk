#ifndef PERFORMANCE_DATA_H
#define PERFORMANCE_DATA_H

#if !defined(PERFORMANCE_DATA_NO_JSON)
#include <ArduinoJson.h>
#endif
#include <stddef.h>
#include <stdint.h>

// Version 1 wire payload:
// {"type":"performance","version":1,"timestamp":<unix seconds>,
//  "cpuPct":0..100,"gpuPct":0..100,"ramPct":0..100,
//  "ramUsedBytes":optional,"ramTotalBytes":optional,
//  "network":{"downBps":0..,"upBps":0..},
//  "temperature":{"cpuC":optional,"gpuC":optional,
//                 "motherboardC":optional}}
// A payload with connected:false is an explicit disconnect notification and
// does not add a sample to history.
constexpr uint8_t PERFORMANCE_PROTOCOL_VERSION = 1;
constexpr size_t PERFORMANCE_HISTORY_CAPACITY = 60;
constexpr uint32_t PERFORMANCE_STALE_AFTER_MS = 3000;
constexpr uint32_t PERFORMANCE_DISCONNECTED_AFTER_MS = 15000;

enum class PerformanceParseResult : uint8_t {
    NOT_PERFORMANCE = 0,
    ACCEPTED,
    REJECTED,
};

enum class PerformanceConnectionState : uint8_t {
    NEVER_RECEIVED = 0,
    LIVE,
    STALE,
    DISCONNECTED,
};

enum PerformanceTemperatureMask : uint8_t {
    PERF_TEMP_CPU = 1u << 0,
    PERF_TEMP_GPU = 1u << 1,
    PERF_TEMP_MOTHERBOARD = 1u << 2,
};

enum PerformanceMetricMask : uint8_t {
    PERF_METRIC_CPU = 1u << 0,
    PERF_METRIC_GPU = 1u << 1,
    PERF_METRIC_RAM = 1u << 2,
    PERF_METRIC_NETWORK_DOWN = 1u << 3,
    PERF_METRIC_NETWORK_UP = 1u << 4,
};

struct PerformanceSample {
    uint32_t timestamp_epoch;
    uint32_t received_ms;
    float cpu_pct;
    float gpu_pct;
    float ram_pct;
    uint64_t ram_used_bytes;
    uint64_t ram_total_bytes;
    uint64_t network_down_bps;
    uint64_t network_up_bps;
    float cpu_temp_c;
    float gpu_temp_c;
    float motherboard_temp_c;
    uint8_t metric_valid_mask;
    uint8_t temperature_valid_mask;
};

struct ThermalThresholds {
    float warm_c;
    float hot_c;
    float critical_c;
};

enum class ThermalLevel : uint8_t {
    NORMAL = 0,
    WARM,
    HOT,
    CRITICAL,
};

enum class NetworkUnit : uint8_t {
    BPS = 0,
    KBPS,
    MBPS,
    GBPS,
};

struct NetworkScaledRate {
    float value;
    NetworkUnit unit;
};

void performance_data_init();

// Returns NOT_PERFORMANCE without modifying state for all existing command,
// Codex usage, and plugin frames. error_out points at a static string for a
// rejected performance frame and is valid until the next call.
#if !defined(PERFORMANCE_DATA_NO_JSON)
PerformanceParseResult performance_data_parse_json(
    JsonVariantConst root,
    uint32_t now_ms,
    const char **error_out = nullptr
);
#endif

// Validates and appends a sidecar sample. `received_ms` is assigned here so
// callers cannot make a fresh sample appear stale (or vice versa).
bool performance_data_ingest(const PerformanceSample &sample, uint32_t now_ms);

void performance_data_mark_disconnected();
PerformanceConnectionState performance_data_connection_state(uint32_t now_ms);
bool performance_data_get_latest(PerformanceSample &out);
size_t performance_data_history_count();
bool performance_data_get_history(size_t oldest_index, PerformanceSample &out);

// Pure validation and presentation helpers. These do not read global state and
// are suitable for native unit tests.
bool performance_validate_sample(const PerformanceSample &sample);
ThermalThresholds performance_thermal_thresholds(uint8_t component_bit);
float performance_thermal_pressure(float temperature_c, ThermalThresholds thresholds);
float performance_highest_thermal_pressure(const PerformanceSample &sample);
float performance_smooth_pressure(float previous, float current,
                                  float rise_alpha = 0.40f,
                                  float fall_alpha = 0.15f);
ThermalLevel performance_thermal_level_with_hysteresis(
    float pressure,
    ThermalLevel previous,
    float hysteresis = 0.04f
);
NetworkScaledRate performance_scale_network_bps(uint64_t bytes_per_second);
const char *performance_network_unit_label(NetworkUnit unit);
uint64_t performance_network_graph_ceiling_bps(uint64_t down_bps, uint64_t up_bps);

#endif // PERFORMANCE_DATA_H
