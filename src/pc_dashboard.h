#ifndef PC_DASHBOARD_H
#define PC_DASHBOARD_H

#include "performance_data.h"

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t PC_DASHBOARD_PROTOCOL_VERSION = 1;
constexpr uint8_t PC_DASHBOARD_MIN_WIDGETS = 1;
constexpr uint8_t PC_DASHBOARD_MAX_WIDGETS = 4;
constexpr uint32_t PC_DASHBOARD_STALE_AFTER_MS = 3000;
constexpr uint32_t PC_DASHBOARD_DISCONNECTED_AFTER_MS = 15000;

// These numeric values are persisted in NVS. Append new metrics; never reorder.
enum class PcDashboardMetric : uint8_t {
    BATTERY = 0,
    MICROPHONE = 1,
    STORAGE = 2,
    RAM = 3,
    CPU = 4,
    GPU = 5,
    NETWORK_DOWN = 6,
    NETWORK_UP = 7,
    CPU_TEMPERATURE = 8,
    GPU_TEMPERATURE = 9,
    MOTHERBOARD_TEMPERATURE = 10,
    COUNT = 11,
};

enum PcDashboardHostMetricMask : uint8_t {
    PC_DASH_HOST_BATTERY = 1u << 0,
    PC_DASH_HOST_MICROPHONE = 1u << 1,
    PC_DASH_HOST_STORAGE = 1u << 2,
};

enum class PcDashboardConnectionState : uint8_t {
    NEVER_RECEIVED = 0,
    LIVE,
    STALE,
    DISCONNECTED,
};

enum class PcDashboardResult : uint8_t {
    OK = 0,
    INVALID_COUNT,
    INVALID_METRIC,
    DUPLICATE_METRIC,
    INVALID_SAMPLE,
    STALE_REVISION,
};

struct PcDashboardConfig {
    uint8_t count;
    PcDashboardMetric metrics[PC_DASHBOARD_MAX_WIDGETS];
};

struct PcDashboardHostSample {
    uint32_t timestamp_epoch;
    uint32_t received_ms;
    float battery_pct;
    bool charging;
    bool microphone_in_use;
    uint64_t storage_used_bytes;
    uint64_t storage_total_bytes;
    uint8_t valid_mask;
};

struct PcDashboardSnapshot {
    uint32_t revision;
    PcDashboardConfig config;
    PcDashboardConnectionState host_state;
    PcDashboardHostSample host;
    bool has_host_sample;
    PerformanceConnectionState performance_state;
    PerformanceSample performance;
    bool has_performance_sample;
};

PcDashboardConfig pc_dashboard_default_config();
bool pc_dashboard_validate_config(const PcDashboardConfig &config,
                                  PcDashboardResult *result = nullptr);
bool pc_dashboard_metric_from_name(const char *name, PcDashboardMetric &metric);
const char *pc_dashboard_metric_name(PcDashboardMetric metric);
const char *pc_dashboard_result_message(PcDashboardResult result);

void pc_dashboard_init(const PcDashboardConfig &config);
const PcDashboardConfig &pc_dashboard_get_config();
uint32_t pc_dashboard_revision();
PcDashboardResult pc_dashboard_set_config(const PcDashboardConfig &config,
                                          uint32_t base_revision);

bool pc_dashboard_validate_host_sample(const PcDashboardHostSample &sample);
PcDashboardResult pc_dashboard_ingest_host_sample(
    const PcDashboardHostSample &sample, uint32_t now_ms);
void pc_dashboard_mark_disconnected();
PcDashboardConnectionState pc_dashboard_connection_state(uint32_t now_ms);
void pc_dashboard_capture(uint32_t now_ms, PcDashboardSnapshot &out);

#endif // PC_DASHBOARD_H
