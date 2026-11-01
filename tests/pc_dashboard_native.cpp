#include "../src/pc_dashboard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    for (uint8_t count = 1; count <= PC_DASHBOARD_MAX_WIDGETS; ++count) {
        for (uint8_t index = 0; index < count; ++index) {
            const PcDashboardTileGeometry tile =
                pc_dashboard_tile_geometry(count, index);
            assert(tile.x >= 0 && tile.y >= 0);
            assert(tile.x + tile.width <= 320);
            assert(tile.y + tile.height <= 200);
            for (uint8_t earlier = 0; earlier < index; ++earlier) {
                const PcDashboardTileGeometry other =
                    pc_dashboard_tile_geometry(count, earlier);
                const bool overlaps = tile.x < other.x + other.width &&
                    tile.x + tile.width > other.x &&
                    tile.y < other.y + other.height &&
                    tile.y + tile.height > other.y;
                assert(!overlaps);
            }
        }
    }
    const PcDashboardTileGeometry third = pc_dashboard_tile_geometry(3, 2);
    assert(third.x == 8 && third.width == 304);

    PcDashboardConfig defaults = pc_dashboard_default_config();
    assert(defaults.count == 4);
    assert(defaults.metrics[0] == PcDashboardMetric::CPU);
    assert(defaults.metrics[1] == PcDashboardMetric::RAM);
    assert(defaults.metrics[2] == PcDashboardMetric::BATTERY);
    assert(defaults.metrics[3] == PcDashboardMetric::STORAGE);
    assert(pc_dashboard_validate_config(defaults));

    PcDashboardMetric parsed = PcDashboardMetric::COUNT;
    assert(pc_dashboard_metric_from_name("networkDown", parsed));
    assert(parsed == PcDashboardMetric::NETWORK_DOWN);
    assert(strcmp(pc_dashboard_metric_name(PcDashboardMetric::MICROPHONE),
                  "microphone") == 0);
    assert(!pc_dashboard_metric_from_name("disk", parsed));

    PcDashboardConfig invalid = defaults;
    invalid.count = 0;
    PcDashboardResult reason = PcDashboardResult::OK;
    assert(!pc_dashboard_validate_config(invalid, &reason));
    assert(reason == PcDashboardResult::INVALID_COUNT);
    invalid = defaults;
    invalid.metrics[1] = invalid.metrics[0];
    assert(!pc_dashboard_validate_config(invalid, &reason));
    assert(reason == PcDashboardResult::DUPLICATE_METRIC);
    invalid = defaults;
    invalid.metrics[0] = PcDashboardMetric::COUNT;
    assert(!pc_dashboard_validate_config(invalid, &reason));
    assert(reason == PcDashboardResult::INVALID_METRIC);

    performance_data_init();
    pc_dashboard_init(defaults);
    assert(pc_dashboard_revision() == 1);
    PcDashboardSnapshot snapshot{};
    pc_dashboard_capture(100, snapshot);
    assert(snapshot.host_state == PcDashboardConnectionState::NEVER_RECEIVED);
    assert(!snapshot.has_host_sample);
    assert(snapshot.performance_state == PerformanceConnectionState::NEVER_RECEIVED);

    PcDashboardConfig changed{};
    changed.count = 2;
    changed.metrics[0] = PcDashboardMetric::MICROPHONE;
    changed.metrics[1] = PcDashboardMetric::NETWORK_UP;
    assert(pc_dashboard_set_config(changed, 99) ==
           PcDashboardResult::STALE_REVISION);
    assert(pc_dashboard_set_config(changed, 1) == PcDashboardResult::OK);
    assert(pc_dashboard_revision() == 2);
    assert(pc_dashboard_get_config().count == 2);

    PcDashboardHostSample host{};
    host.timestamp_epoch = 1'800'000'000;
    // No BATTERY bit: a desktop without a battery must not surface false 0%.
    host.microphone_in_use = true;
    host.storage_used_bytes = 300;
    host.storage_total_bytes = 1000;
    host.valid_mask = PC_DASH_HOST_MICROPHONE | PC_DASH_HOST_STORAGE;
    assert(pc_dashboard_validate_host_sample(host));
    assert(pc_dashboard_ingest_host_sample(host, 500) == PcDashboardResult::OK);
    pc_dashboard_capture(500, snapshot);
    assert(snapshot.host_state == PcDashboardConnectionState::LIVE);
    assert((snapshot.host.valid_mask & PC_DASH_HOST_BATTERY) == 0);
    assert(snapshot.host.microphone_in_use);
    assert(snapshot.host.storage_used_bytes == 300);
    pc_dashboard_capture(4000, snapshot);
    assert(snapshot.host_state == PcDashboardConnectionState::STALE);
    pc_dashboard_capture(16000, snapshot);
    assert(snapshot.host_state == PcDashboardConnectionState::DISCONNECTED);

    PcDashboardHostSample bad = host;
    bad.valid_mask |= PC_DASH_HOST_BATTERY;
    bad.battery_pct = 101.0f;
    assert(!pc_dashboard_validate_host_sample(bad));
    bad = host;
    bad.storage_used_bytes = 1001;
    assert(!pc_dashboard_validate_host_sample(bad));
    bad = host;
    bad.valid_mask = 0;
    assert(!pc_dashboard_validate_host_sample(bad));

    PerformanceSample performance{};
    performance.timestamp_epoch = 1'800'000'001;
    performance.cpu_pct = 42.5f;
    performance.ram_pct = 61.0f;
    performance.metric_valid_mask = PERF_METRIC_CPU | PERF_METRIC_RAM;
    assert(performance_data_ingest(performance, 800));
    pc_dashboard_capture(800, snapshot);
    assert(snapshot.has_performance_sample);
    assert(snapshot.performance_state == PerformanceConnectionState::LIVE);
    assert(snapshot.performance.cpu_pct == 42.5f);

    pc_dashboard_mark_disconnected();
    pc_dashboard_capture(801, snapshot);
    assert(snapshot.host_state == PcDashboardConnectionState::DISCONNECTED);

    puts("pc_dashboard_native: PASS");
    return 0;
}
