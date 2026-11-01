//! EchoDesk PC telemetry sidecar.
//!
//! The helper owns LibreHardwareMonitor and writes NDJSON to stdout. It never
//! sees a COM port. Samples are coalesced here, and only `serial_service` may
//! turn the newest sample into a display frame.

use crate::serial_service::{self, Job};
use crate::state::AppState;
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Manager};

pub const PERFORMANCE_VERSION: u8 = 1;
pub const PERFORMANCE_MAX_BYTES: usize = 1024;
const PERFORMANCE_MAX_NETWORK_BYTES_PER_SECOND: u64 = 1_000_000_000_000_000;
const HELPER_ENV: &str = "AIMONITOR_PERFORMANCE_HELPER";
const HELPER_NAME: &str = "echodesk-performance-helper.exe";
#[cfg(windows)]
const HELPER_LINE_MAX_BYTES: usize = 2048;

#[cfg(windows)]
const fn performance_helper_creation_flags() -> u32 {
    // CREATE_NO_WINDOW. The helper is a supervised console application whose
    // stdout/stderr are pipes; it must never create a user-visible terminal.
    0x0800_0000
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PerformanceSample {
    #[serde(rename = "type")]
    pub kind: String,
    pub version: u8,
    pub timestamp: u64,
    pub cpu_pct: Option<f64>,
    pub gpu_pct: Option<f64>,
    pub ram_pct: Option<f64>,
    pub ram_used_bytes: Option<u64>,
    pub ram_total_bytes: Option<u64>,
    pub network: NetworkSample,
    pub temperature: TemperatureSample,
    #[serde(default, skip_serializing)]
    pub pc_dashboard: PcDashboardHostSample,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PcDashboardHostSample {
    pub battery_pct: Option<f64>,
    pub charging: Option<bool>,
    pub microphone_in_use: Option<bool>,
    pub storage_used_bytes: Option<u64>,
    pub storage_total_bytes: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NetworkSample {
    pub down_bps: Option<u64>,
    pub up_bps: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TemperatureSample {
    pub cpu_c: Option<f64>,
    pub gpu_c: Option<f64>,
    pub motherboard_c: Option<f64>,
}

impl PerformanceSample {
    pub fn parse(line: &str) -> Result<Self, String> {
        let sample: Self = serde_json::from_str(line).map_err(|e| format!("invalid JSON: {e}"))?;
        sample.validate()?;
        Ok(sample)
    }

    fn validate(&self) -> Result<(), String> {
        if self.kind != "performance" {
            return Err(format!("unexpected type {:?}", self.kind));
        }
        if self.version != PERFORMANCE_VERSION {
            return Err(format!("unsupported version {}", self.version));
        }
        if self.timestamp == 0 {
            return Err("timestamp must be positive".into());
        }
        for (name, value) in [
            ("cpuPct", self.cpu_pct),
            ("gpuPct", self.gpu_pct),
            ("ramPct", self.ram_pct),
        ] {
            if value.is_some_and(|v| !v.is_finite() || !(0.0..=100.0).contains(&v)) {
                return Err(format!("{name} is outside 0..100"));
            }
        }
        for (name, value) in [
            ("temperature.cpuC", self.temperature.cpu_c),
            ("temperature.gpuC", self.temperature.gpu_c),
            ("temperature.motherboardC", self.temperature.motherboard_c),
        ] {
            if value.is_some_and(|v| !v.is_finite() || !(-40.0..=150.0).contains(&v)) {
                return Err(format!("{name} is outside -40..150 C"));
            }
        }
        match (self.ram_used_bytes, self.ram_total_bytes) {
            (Some(used), Some(total)) if used <= total && total > 0 => {}
            (None, None) => {}
            (Some(_), Some(_)) => return Err("ramUsedBytes exceeds ramTotalBytes".into()),
            _ => return Err("RAM byte fields must be paired".into()),
        }
        if self
            .network
            .down_bps
            .is_some_and(|rate| rate > PERFORMANCE_MAX_NETWORK_BYTES_PER_SECOND)
            || self
                .network
                .up_bps
                .is_some_and(|rate| rate > PERFORMANCE_MAX_NETWORK_BYTES_PER_SECOND)
        {
            return Err("network rate exceeds sanity limit".into());
        }
        if self
            .pc_dashboard
            .battery_pct
            .is_some_and(|v| !v.is_finite() || !(0.0..=100.0).contains(&v))
        {
            return Err("pcDashboard.batteryPct is outside 0..100".into());
        }
        if self.pc_dashboard.battery_pct.is_some() != self.pc_dashboard.charging.is_some() {
            return Err("PC Dashboard battery fields must be paired".into());
        }
        match (
            self.pc_dashboard.storage_used_bytes,
            self.pc_dashboard.storage_total_bytes,
        ) {
            (Some(used), Some(total)) if total > 0 && used <= total => {}
            (None, None) => {}
            _ => return Err("PC Dashboard storage fields are invalid".into()),
        }
        Ok(())
    }

    /// Adds the transport receipt ID while preserving the versioned telemetry
    /// object. The AIM1 header carries the same ID.
    pub fn payload(&self, frame_id: i64) -> Result<String, String> {
        self.validate()?;
        let mut value = serde_json::to_value(self).map_err(|e| e.to_string())?;
        value["frameId"] = serde_json::json!(frame_id);
        let payload = value.to_string();
        if payload.len() >= PERFORMANCE_MAX_BYTES {
            return Err(format!("performance payload is {} bytes", payload.len()));
        }
        Ok(payload)
    }

    pub fn pc_dashboard_payload(&self, frame_id: i64) -> Result<Option<String>, String> {
        self.validate()?;
        let p = &self.pc_dashboard;
        if p.battery_pct.is_none()
            && p.microphone_in_use.is_none()
            && p.storage_used_bytes.is_none()
        {
            return Ok(None);
        }
        let mut value = serde_json::json!({
            "type": "pc_dashboard_sample",
            "version": 1,
            "timestamp": self.timestamp,
            "frameId": frame_id,
        });
        if let (Some(pct), Some(charging)) = (p.battery_pct, p.charging) {
            value["batteryPct"] = serde_json::json!(pct);
            value["charging"] = serde_json::json!(charging);
        }
        if let Some(in_use) = p.microphone_in_use {
            value["microphoneInUse"] = serde_json::json!(in_use);
        }
        if let (Some(used), Some(total)) = (p.storage_used_bytes, p.storage_total_bytes) {
            value["storageUsedBytes"] = serde_json::json!(used);
            value["storageTotalBytes"] = serde_json::json!(total);
        }
        let payload = value.to_string();
        if payload.len() >= PERFORMANCE_MAX_BYTES {
            return Err(format!("PC Dashboard payload is {} bytes", payload.len()));
        }
        Ok(Some(payload))
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PerformanceHealth {
    pub status: String,
    pub last_sample_at: Option<DateTime<Utc>>,
    pub error: Option<String>,
}

impl Default for PerformanceHealth {
    fn default() -> Self {
        Self {
            status: "starting".into(),
            last_sample_at: None,
            error: None,
        }
    }
}

#[derive(Debug, Default)]
struct FeedState {
    latest: Option<PerformanceSample>,
    notification_pending: bool,
    health: PerformanceHealth,
}

/// Single-slot latest-value mailbox. A producer can replace a queued sample,
/// but can enqueue at most one `PerformanceReady` job at a time.
#[derive(Debug, Clone, Default)]
pub struct PerformanceFeed {
    inner: Arc<Mutex<FeedState>>,
}

impl PerformanceFeed {
    /// Returns true exactly when the caller must enqueue `PerformanceReady`.
    pub fn publish(&self, sample: PerformanceSample) -> bool {
        let mut state = self.inner.lock().unwrap();
        state.latest = Some(sample);
        state.health.status = "live".into();
        state.health.last_sample_at = Some(Utc::now());
        state.health.error = None;
        if state.notification_pending {
            false
        } else {
            state.notification_pending = true;
            true
        }
    }

    pub fn take(&self) -> Option<PerformanceSample> {
        let mut state = self.inner.lock().unwrap();
        let sample = state.latest.take();
        state.notification_pending = false;
        sample
    }

    pub fn mark_starting(&self) {
        let mut state = self.inner.lock().unwrap();
        state.health.status = "starting".into();
        state.health.error = None;
    }

    pub fn mark_unavailable(&self) {
        let mut state = self.inner.lock().unwrap();
        state.health.status = "unavailable".into();
        state.health.error = Some("The Windows performance collector is unavailable.".into());
    }

    pub fn health(&self) -> PerformanceHealth {
        self.inner.lock().unwrap().health.clone()
    }
}

pub fn start(app: AppHandle) {
    #[cfg(windows)]
    start_windows(app);

    #[cfg(not(windows))]
    let _ = app;
}

#[cfg(windows)]
fn start_windows(app: AppHandle) {
    std::thread::Builder::new()
        .name("aimonitor-performance".into())
        .spawn(move || {
            let mut retry = std::time::Duration::from_secs(1);
            loop {
                app.state::<AppState>().performance.mark_starting();
                match run_helper_once(&app) {
                    Ok(()) => serial_service::log(&app, "Performance helper stopped; restarting"),
                    Err(e) => {
                        app.state::<AppState>().performance.mark_unavailable();
                        serial_service::log(&app, format!("Performance helper unavailable: {e}"))
                    }
                }
                std::thread::sleep(retry);
                retry = (retry * 2).min(std::time::Duration::from_secs(30));
            }
        })
        .expect("performance helper thread");
}

#[cfg(windows)]
fn run_helper_once(app: &AppHandle) -> Result<(), String> {
    use std::io::{BufRead, BufReader};
    use std::os::windows::process::CommandExt;
    use std::process::{Command, Stdio};

    let helper = resolve_helper(app).ok_or_else(|| "helper executable not found".to_string())?;
    let mut command = Command::new(&helper);
    command
        .creation_flags(performance_helper_creation_flags())
        .args(["--parent-pid", &std::process::id().to_string()])
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    let mut child = command
        .spawn()
        .map_err(|e| format!("failed to start: {e}"))?;

    if let Some(stderr) = child.stderr.take() {
        std::thread::spawn(move || {
            for line in BufReader::new(stderr).lines().map_while(Result::ok) {
                eprintln!("[performance-helper] {line}");
            }
        });
    }

    let Some(stdout) = child.stdout.take() else {
        let _ = child.kill();
        return Err("stdout unavailable".into());
    };
    serial_service::log(app, format!("Performance helper: {}", helper.display()));
    let mut reader = BufReader::new(stdout);
    loop {
        match read_bounded_line(&mut reader, HELPER_LINE_MAX_BYTES) {
            Ok(Some(line)) => match PerformanceSample::parse(&line) {
                Ok(sample) => {
                    let state = app.state::<AppState>();
                    if state.performance.publish(sample) {
                        let _ = state.serial.send(Job::PerformanceReady);
                    }
                }
                Err(e) => eprintln!("[performance-helper] sample rejected: {e}"),
            },
            Ok(None) => break,
            Err(e) if e.kind() == std::io::ErrorKind::InvalidData => {
                eprintln!("[performance-helper] sample rejected: {e}");
            }
            Err(e) => return Err(format!("stdout read failed: {e}")),
        }
    }
    let status = child.wait().map_err(|e| format!("wait failed: {e}"))?;
    Err(format!("exited: {status}"))
}

#[cfg(windows)]
fn read_bounded_line<R: std::io::BufRead>(
    reader: &mut R,
    maximum: usize,
) -> std::io::Result<Option<String>> {
    use std::io::{Error, ErrorKind};

    let mut bytes = Vec::with_capacity(maximum.min(1024));
    let mut oversized = false;
    let mut saw_bytes = false;
    loop {
        let buffer = reader.fill_buf()?;
        if buffer.is_empty() {
            if !saw_bytes {
                return Ok(None);
            }
            break;
        }
        saw_bytes = true;
        let newline = buffer.iter().position(|byte| *byte == b'\n');
        let take = newline.unwrap_or(buffer.len());
        if !oversized {
            if bytes.len().saturating_add(take) > maximum {
                oversized = true;
            } else {
                bytes.extend_from_slice(&buffer[..take]);
            }
        }
        reader.consume(take + usize::from(newline.is_some()));
        if newline.is_some() {
            break;
        }
    }
    if oversized {
        return Err(Error::new(
            ErrorKind::InvalidData,
            "helper line exceeds limit",
        ));
    }
    if bytes.last() == Some(&b'\r') {
        bytes.pop();
    }
    String::from_utf8(bytes)
        .map(Some)
        .map_err(|_| Error::new(ErrorKind::InvalidData, "helper line is not UTF-8"))
}

#[cfg(windows)]
fn resolve_helper(app: &AppHandle) -> Option<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(path) = std::env::var_os(HELPER_ENV) {
        candidates.push(PathBuf::from(path));
    }
    if let Ok(resources) = app.path().resource_dir() {
        candidates.push(resources.join("performance").join(HELPER_NAME));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            candidates.push(dir.join("performance").join(HELPER_NAME));
        }
    }
    candidates.push(
        Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("..")
            .join("tools")
            .join("echodesk-performance-helper")
            .join("bin")
            .join("Release")
            .join("net8.0")
            .join("win-x64")
            .join("publish")
            .join(HELPER_NAME),
    );
    first_existing(candidates)
}

#[cfg(windows)]
fn first_existing(candidates: impl IntoIterator<Item = PathBuf>) -> Option<PathBuf> {
    candidates.into_iter().find(|path| path.is_file())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample(timestamp: u64) -> PerformanceSample {
        PerformanceSample {
            kind: "performance".into(),
            version: 1,
            timestamp,
            cpu_pct: Some(12.5),
            gpu_pct: None,
            ram_pct: Some(61.0),
            ram_used_bytes: Some(16),
            ram_total_bytes: Some(32),
            network: NetworkSample {
                down_bps: Some(1234),
                up_bps: None,
            },
            temperature: TemperatureSample {
                cpu_c: Some(51.25),
                gpu_c: None,
                motherboard_c: None,
            },
            pc_dashboard: PcDashboardHostSample::default(),
        }
    }

    #[test]
    fn parses_canonical_sidecar_sample_and_builds_small_payload() {
        let line = r#"{"type":"performance","version":1,"timestamp":1793333400,"cpuPct":12.5,"gpuPct":null,"ramPct":61.0,"ramUsedBytes":16,"ramTotalBytes":32,"network":{"downBps":1234,"upBps":null},"temperature":{"cpuC":51.25,"gpuC":null,"motherboardC":null}}"#;
        let parsed = PerformanceSample::parse(line).unwrap();
        assert_eq!(parsed, sample(1_793_333_400));
        let payload = parsed.payload(42).unwrap();
        assert!(payload.len() < PERFORMANCE_MAX_BYTES);
        let value: serde_json::Value = serde_json::from_str(&payload).unwrap();
        assert_eq!(value["type"], "performance");
        assert_eq!(value["frameId"], 42);
        assert_eq!(value["network"]["downBps"], 1234);
    }

    #[test]
    fn builds_bounded_pc_dashboard_sample_and_omits_unavailable_fields() {
        let mut value = sample(1_793_333_400);
        value.pc_dashboard = PcDashboardHostSample {
            battery_pct: None,
            charging: None,
            microphone_in_use: Some(false),
            storage_used_bytes: Some(25),
            storage_total_bytes: Some(100),
        };
        let payload = value.pc_dashboard_payload(43).unwrap().unwrap();
        let json: serde_json::Value = serde_json::from_str(&payload).unwrap();
        assert_eq!(json["type"], "pc_dashboard_sample");
        assert_eq!(json["microphoneInUse"], false);
        assert_eq!(json["storageUsedBytes"], 25);
        assert!(json.get("batteryPct").is_none());
    }

    #[test]
    fn rejects_bad_versions_percentages_and_memory_totals() {
        let mut value = sample(1);
        value.version = 2;
        assert!(value.payload(1).unwrap_err().contains("version"));
        value.version = 1;
        value.cpu_pct = Some(101.0);
        assert!(value.payload(1).unwrap_err().contains("cpuPct"));
        value.cpu_pct = None;
        value.ram_used_bytes = Some(33);
        assert!(value.payload(1).unwrap_err().contains("ramUsedBytes"));
        value.ram_used_bytes = None;
        assert!(value.payload(1).unwrap_err().contains("paired"));
        value.ram_total_bytes = None;
        value.temperature.cpu_c = Some(151.0);
        assert!(value.payload(1).unwrap_err().contains("temperature.cpuC"));
    }

    #[test]
    fn feed_coalesces_to_latest_sample_and_rearms_after_take() {
        let feed = PerformanceFeed::default();
        assert_eq!(feed.health().status, "starting");
        assert!(feed.publish(sample(1)));
        assert_eq!(feed.health().status, "live");
        assert!(feed.health().last_sample_at.is_some());
        assert!(!feed.publish(sample(2)));
        assert_eq!(feed.take().unwrap().timestamp, 2);
        assert!(feed.publish(sample(3)));
        assert_eq!(feed.take().unwrap().timestamp, 3);
        assert!(feed.take().is_none());
        feed.mark_unavailable();
        assert_eq!(feed.health().status, "unavailable");
    }

    #[cfg(windows)]
    #[test]
    fn performance_helper_is_configured_without_a_console_window() {
        assert_eq!(
            performance_helper_creation_flags(),
            0x0800_0000,
            "the supervised helper must not create the terminal window users reported"
        );
    }
}
