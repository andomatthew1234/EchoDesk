#include "pc_dashboard.h"

#include <math.h>
#include <string.h>

namespace {

constexpr uint8_t ALL_HOST_METRICS = PC_DASH_HOST_BATTERY |
                                     PC_DASH_HOST_MICROPHONE |
                                     PC_DASH_HOST_STORAGE;

PcDashboardConfig dashboard_config = pc_dashboard_default_config();
PcDashboardHostSample host_sample{};
bool has_host_sample = false;
bool explicitly_disconnected = false;
uint32_t revision = 1;

void set_result(PcDashboardResult *out, PcDashboardResult value) {
    if (out) *out = value;
}

void bump_revision() {
    ++revision;
    if (revision == 0) revision = 1;
}

} // namespace

PcDashboardConfig pc_dashboard_default_config() {
    PcDashboardConfig config{};
    config.count = PC_DASHBOARD_MAX_WIDGETS;
    config.metrics[0] = PcDashboardMetric::CPU;
    config.metrics[1] = PcDashboardMetric::RAM;
    config.metrics[2] = PcDashboardMetric::BATTERY;
    config.metrics[3] = PcDashboardMetric::STORAGE;
    return config;
}

bool pc_dashboard_validate_config(const PcDashboardConfig &config,
                                  PcDashboardResult *result) {
    if (config.count < PC_DASHBOARD_MIN_WIDGETS ||
        config.count > PC_DASHBOARD_MAX_WIDGETS) {
        set_result(result, PcDashboardResult::INVALID_COUNT);
        return false;
    }
    for (uint8_t i = 0; i < config.count; ++i) {
        if (static_cast<uint8_t>(config.metrics[i]) >=
            static_cast<uint8_t>(PcDashboardMetric::COUNT)) {
            set_result(result, PcDashboardResult::INVALID_METRIC);
            return false;
        }
        for (uint8_t j = 0; j < i; ++j) {
            if (config.metrics[i] == config.metrics[j]) {
                set_result(result, PcDashboardResult::DUPLICATE_METRIC);
                return false;
            }
        }
    }
    set_result(result, PcDashboardResult::OK);
    return true;
}

bool pc_dashboard_metric_from_name(const char *name, PcDashboardMetric &metric) {
    if (!name) return false;
    struct Entry { const char *name; PcDashboardMetric metric; };
    static constexpr Entry entries[] = {
        {"battery", PcDashboardMetric::BATTERY},
        {"microphone", PcDashboardMetric::MICROPHONE},
        {"storage", PcDashboardMetric::STORAGE},
        {"ram", PcDashboardMetric::RAM},
        {"cpu", PcDashboardMetric::CPU},
        {"gpu", PcDashboardMetric::GPU},
        {"networkDown", PcDashboardMetric::NETWORK_DOWN},
        {"networkUp", PcDashboardMetric::NETWORK_UP},
        {"cpuTemperature", PcDashboardMetric::CPU_TEMPERATURE},
        {"gpuTemperature", PcDashboardMetric::GPU_TEMPERATURE},
        {"motherboardTemperature", PcDashboardMetric::MOTHERBOARD_TEMPERATURE},
    };
    for (const Entry &entry : entries) {
        if (strcmp(name, entry.name) == 0) {
            metric = entry.metric;
            return true;
        }
    }
    return false;
}

const char *pc_dashboard_metric_name(PcDashboardMetric metric) {
    switch (metric) {
        case PcDashboardMetric::BATTERY: return "battery";
        case PcDashboardMetric::MICROPHONE: return "microphone";
        case PcDashboardMetric::STORAGE: return "storage";
        case PcDashboardMetric::RAM: return "ram";
        case PcDashboardMetric::CPU: return "cpu";
        case PcDashboardMetric::GPU: return "gpu";
        case PcDashboardMetric::NETWORK_DOWN: return "networkDown";
        case PcDashboardMetric::NETWORK_UP: return "networkUp";
        case PcDashboardMetric::CPU_TEMPERATURE: return "cpuTemperature";
        case PcDashboardMetric::GPU_TEMPERATURE: return "gpuTemperature";
        case PcDashboardMetric::MOTHERBOARD_TEMPERATURE:
            return "motherboardTemperature";
        case PcDashboardMetric::COUNT: break;
    }
    return "unknown";
}

const char *pc_dashboard_result_message(PcDashboardResult result) {
    switch (result) {
        case PcDashboardResult::OK: return "accepted";
        case PcDashboardResult::INVALID_COUNT: return "select between one and four metrics";
        case PcDashboardResult::INVALID_METRIC: return "unknown PC Dashboard metric";
        case PcDashboardResult::DUPLICATE_METRIC: return "PC Dashboard metrics must be unique";
        case PcDashboardResult::INVALID_SAMPLE: return "invalid PC Dashboard sample";
        case PcDashboardResult::STALE_REVISION: return "stale PC Dashboard revision";
    }
    return "invalid PC Dashboard request";
}

void pc_dashboard_init(const PcDashboardConfig &config) {
    dashboard_config = pc_dashboard_validate_config(config)
        ? config : pc_dashboard_default_config();
    memset(&host_sample, 0, sizeof(host_sample));
    has_host_sample = false;
    explicitly_disconnected = false;
    revision = 1;
}

const PcDashboardConfig &pc_dashboard_get_config() { return dashboard_config; }

uint32_t pc_dashboard_revision() { return revision; }

PcDashboardResult pc_dashboard_set_config(const PcDashboardConfig &config,
                                          uint32_t base_revision) {
    if (base_revision != revision) return PcDashboardResult::STALE_REVISION;
    PcDashboardResult result = PcDashboardResult::OK;
    if (!pc_dashboard_validate_config(config, &result)) return result;
    dashboard_config = config;
    bump_revision();
    return PcDashboardResult::OK;
}

bool pc_dashboard_validate_host_sample(const PcDashboardHostSample &sample) {
    if (sample.timestamp_epoch == 0 || sample.valid_mask == 0 ||
        (sample.valid_mask & ~ALL_HOST_METRICS)) return false;
    if ((sample.valid_mask & PC_DASH_HOST_BATTERY) &&
        (!isfinite(sample.battery_pct) || sample.battery_pct < 0.0f ||
         sample.battery_pct > 100.0f)) return false;
    if (sample.valid_mask & PC_DASH_HOST_STORAGE) {
        if (sample.storage_total_bytes == 0 ||
            sample.storage_used_bytes > sample.storage_total_bytes) return false;
    } else if (sample.storage_used_bytes != 0 || sample.storage_total_bytes != 0) {
        return false;
    }
    return true;
}

PcDashboardResult pc_dashboard_ingest_host_sample(
    const PcDashboardHostSample &input, uint32_t now_ms) {
    PcDashboardHostSample next = input;
    next.received_ms = now_ms;
    if (!pc_dashboard_validate_host_sample(next)) {
        return PcDashboardResult::INVALID_SAMPLE;
    }
    host_sample = next;
    has_host_sample = true;
    explicitly_disconnected = false;
    return PcDashboardResult::OK;
}

void pc_dashboard_mark_disconnected() { explicitly_disconnected = true; }

PcDashboardConnectionState pc_dashboard_connection_state(uint32_t now_ms) {
    if (explicitly_disconnected) return PcDashboardConnectionState::DISCONNECTED;
    if (!has_host_sample) return PcDashboardConnectionState::NEVER_RECEIVED;
    const uint32_t elapsed = now_ms - host_sample.received_ms;
    if (elapsed <= PC_DASHBOARD_STALE_AFTER_MS) return PcDashboardConnectionState::LIVE;
    if (elapsed <= PC_DASHBOARD_DISCONNECTED_AFTER_MS) return PcDashboardConnectionState::STALE;
    return PcDashboardConnectionState::DISCONNECTED;
}

void pc_dashboard_capture(uint32_t now_ms, PcDashboardSnapshot &out) {
    memset(&out, 0, sizeof(out));
    out.revision = revision;
    out.config = dashboard_config;
    out.host_state = pc_dashboard_connection_state(now_ms);
    out.has_host_sample = has_host_sample;
    if (has_host_sample) out.host = host_sample;
    out.performance_state = performance_data_connection_state(now_ms);
    out.has_performance_sample = performance_data_get_latest(out.performance);
}
