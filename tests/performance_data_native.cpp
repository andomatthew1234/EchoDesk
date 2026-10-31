#define PERFORMANCE_DATA_NO_JSON 1
#include "../src/performance_data.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near(float actual, float expected, float tolerance = 0.001f) {
    return fabsf(actual - expected) <= tolerance;
}

static PerformanceSample sample(uint32_t timestamp, float cpu) {
    PerformanceSample value{};
    value.timestamp_epoch = timestamp;
    value.cpu_pct = cpu;
    value.metric_valid_mask = PERF_METRIC_CPU;
    value.cpu_temp_c = 50.0f;
    value.temperature_valid_mask = PERF_TEMP_CPU;
    return value;
}

int main() {
    performance_data_init();
    for (uint32_t i = 1; i <= 65; ++i) {
        PerformanceSample value = sample(i, static_cast<float>(i % 101));
        assert(performance_data_ingest(value, i * 1000));
    }
    assert(performance_data_history_count() == PERFORMANCE_HISTORY_CAPACITY);
    PerformanceSample value{};
    assert(performance_data_get_history(0, value) && value.timestamp_epoch == 6);
    assert(performance_data_get_latest(value) && value.timestamp_epoch == 65);
    assert(performance_data_connection_state(65000) == PerformanceConnectionState::LIVE);
    assert(performance_data_connection_state(69001) == PerformanceConnectionState::STALE);
    assert(performance_data_connection_state(81001) == PerformanceConnectionState::DISCONNECTED);
    performance_data_mark_disconnected();
    assert(performance_data_connection_state(65000) == PerformanceConnectionState::DISCONNECTED);

    PerformanceSample missing = sample(100, 0.0f);
    missing.metric_valid_mask = 0;
    missing.temperature_valid_mask = 0;
    assert(performance_validate_sample(missing));
    missing.metric_valid_mask = PERF_METRIC_GPU;
    missing.gpu_pct = 101.0f;
    assert(!performance_validate_sample(missing));
    missing.gpu_pct = 50.0f;
    missing.metric_valid_mask = 0x80;
    assert(!performance_validate_sample(missing));

    const ThermalThresholds cpu = performance_thermal_thresholds(PERF_TEMP_CPU);
    assert(near(performance_thermal_pressure(cpu.warm_c, cpu), 1.0f / 3.0f));
    assert(near(performance_thermal_pressure(cpu.hot_c, cpu), 2.0f / 3.0f));
    assert(near(performance_thermal_pressure(cpu.critical_c, cpu), 1.0f));
    assert(near(performance_smooth_pressure(0.2f, 0.7f), 0.4f));
    assert(performance_thermal_level_with_hysteresis(0.35f, ThermalLevel::NORMAL) == ThermalLevel::WARM);
    assert(performance_thermal_level_with_hysteresis(0.31f, ThermalLevel::WARM) == ThermalLevel::WARM);
    assert(performance_thermal_level_with_hysteresis(0.25f, ThermalLevel::WARM) == ThermalLevel::NORMAL);

    PerformanceSample thermal = sample(101, 10.0f);
    thermal.gpu_temp_c = 90.0f;
    thermal.temperature_valid_mask = PERF_TEMP_CPU | PERF_TEMP_GPU;
    assert(performance_highest_thermal_pressure(thermal) > 0.8f);

    const NetworkScaledRate rate = performance_scale_network_bps(125000);
    assert(rate.unit == NetworkUnit::MBPS);
    assert(near(rate.value, 1.0f));
    assert(performance_network_graph_ceiling_bps(1200, 600) == 2000);

    puts("performance_data_native: PASS");
    return 0;
}
