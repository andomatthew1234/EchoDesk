//! EchoDesk Desktop v0.2 consumer firmware workflow.
//!
//! The UI selects only an allowlisted hardware model and operation. Paths,
//! offsets and chip targets always come from the trusted packaged manifest.

use crate::serial_service::{self, ConnectionState, Job};
use crate::state::AppState;
use aimonitor_flash::{
    flash_image, parse_firmware_manifest, probe_device, validate_merged_image, verify_sha256,
    DeviceProbe, FirmwareManifest, FirmwareRelease, FlashEvent, TargetChip, FLASH_BAUD,
};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::path::{Path, PathBuf};
use std::sync::atomic::Ordering;
use std::sync::mpsc;
use std::time::{Duration, Instant};
use tauri::{AppHandle, Manager};

pub const MINI_MODEL: &str = "EchoDesk Mini";
pub const MINI_HARDWARE_ID: &str = "freenove-fnk0114a-2p4";
pub const MINI_REPORTED_MODEL: &str = "Freenove FNK0114A_2P4";
pub const DESKTOP_VERSION: &str = "0.2.7";
const RECONNECT_TIMEOUT: Duration = Duration::from_secs(30);
const SETUP_RECONNECT_TIMEOUT: Duration = Duration::from_secs(15);
const PAUSE_TIMEOUT: Duration = Duration::from_secs(5);
const SETTLE_DELAY: Duration = Duration::from_millis(500);
const MAX_FIRMWARE_BYTES: u64 = 4 * 1024 * 1024;

#[derive(Debug, Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareDevice {
    pub port: String,
    pub display_name: String,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub adapter: Option<String>,
    pub usb_serial_number: Option<String>,
    pub identity_status: String,
    pub chip_family: Option<String>,
    pub flash_size_bytes: Option<u64>,
    pub echo_desk_handshake: bool,
    pub firmware_version: Option<String>,
    pub hardware_model: Option<String>,
    pub setup_state: Option<String>,
    pub setup_version: Option<u64>,
    pub device_mode: String,
    pub in_bootloader: bool,
    pub compatible: bool,
    pub requires_board_confirmation: bool,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareManagerState {
    pub status: String,
    pub operation: Option<String>,
    pub stage: Option<String>,
    pub progress_percent: Option<u32>,
    pub message: Option<String>,
    pub error_code: Option<String>,
    pub error_message: Option<String>,
    pub selected_port: Option<String>,
    pub selected_hardware_model: Option<String>,
    pub board_confirmed: bool,
    pub write_completed: bool,
    pub reconnected: bool,
    pub settings_backup_available: bool,
    pub update_notifications_enabled: bool,
    pub current_version: Option<String>,
    pub available_version: Option<String>,
    pub release_notes: Option<String>,
    pub show_unknown_devices: bool,
    pub setup_phase: Option<String>,
    pub setup_target_port: Option<String>,
    pub setup_completed: bool,
    pub devices: Vec<FirmwareDevice>,
    #[serde(skip)]
    cancel_requested: bool,
    #[serde(skip)]
    settings_backup: Option<Value>,
    #[serde(skip)]
    selected_vid: Option<u16>,
    #[serde(skip)]
    selected_pid: Option<u16>,
    #[serde(skip)]
    selected_usb_serial_number: Option<String>,
}

impl Default for FirmwareManagerState {
    fn default() -> Self {
        Self {
            status: "idle".into(),
            operation: None,
            stage: None,
            progress_percent: None,
            message: None,
            error_code: None,
            error_message: None,
            selected_port: None,
            selected_hardware_model: None,
            board_confirmed: false,
            write_completed: false,
            reconnected: false,
            settings_backup_available: false,
            update_notifications_enabled: true,
            current_version: None,
            available_version: None,
            release_notes: None,
            show_unknown_devices: false,
            setup_phase: None,
            setup_target_port: None,
            setup_completed: false,
            devices: Vec::new(),
            cancel_requested: false,
            settings_backup: None,
            selected_vid: None,
            selected_pid: None,
            selected_usb_serial_number: None,
        }
    }
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ScanRequest {
    #[serde(default)]
    pub reveal_unknown: bool,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SelectDeviceRequest {
    pub port: String,
    pub hardware_model: String,
    #[serde(default)]
    pub board_confirmed: bool,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ManualRecoveryRequest {
    pub port: String,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SetupPortRequest {
    pub port: String,
}

#[derive(Debug, Clone, Copy, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum InstallMode {
    Install,
    Update,
    Reinstall,
    Repair,
}

impl InstallMode {
    fn wire(self) -> &'static str {
        match self {
            Self::Install => "install",
            Self::Update => "update",
            Self::Reinstall => "reinstall",
            Self::Repair => "repair",
        }
    }
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InstallRequest {
    pub mode: InstallMode,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum InstallPolicy {
    Normal,
    Esp32OnlyRecovery,
}

fn validate_probe_for_policy(
    policy: InstallPolicy,
    probe: Option<DeviceProbe>,
) -> Result<(), &'static str> {
    let probe = probe.ok_or("unsupported_chip")?;
    if probe.chip != TargetChip::Esp32 {
        return Err("wrong_chip");
    }
    if policy == InstallPolicy::Normal && probe.flash_size_bytes != Some(MAX_FIRMWARE_BYTES) {
        return Err("wrong_flash_size");
    }
    Ok(())
}

fn validate_version_for_policy(
    policy: InstallPolicy,
    mode: InstallMode,
    current: Option<&str>,
    available: &str,
) -> Result<(), &'static str> {
    if policy == InstallPolicy::Esp32OnlyRecovery {
        return Ok(());
    }
    if matches!(mode, InstallMode::Update)
        && !current
            .is_some_and(|version| aimonitor_core::semver::compare(version, available).is_lt())
    {
        return Err("firmware_not_newer");
    }
    if current.is_some_and(|version| aimonitor_core::semver::compare(version, available).is_gt()) {
        return Err("firmware_downgrade_blocked");
    }
    Ok(())
}

fn validate_desktop_version_for_policy(
    policy: InstallPolicy,
    desktop: &str,
    minimum: &str,
) -> Result<(), &'static str> {
    if policy == InstallPolicy::Normal && !aimonitor_core::semver::at_least(desktop, minimum) {
        return Err("desktop_update_required");
    }
    Ok(())
}

fn bump(app: &AppHandle) {
    app.state::<AppState>()
        .desktop_broker_revision
        .fetch_add(1, Ordering::AcqRel);
}

fn update_state(app: &AppHandle, update: impl FnOnce(&mut FirmwareManagerState)) {
    update(&mut app.state::<AppState>().firmware_manager.lock().unwrap());
    bump(app);
}

pub fn snapshot(app: &AppHandle) -> FirmwareManagerState {
    let enabled = app
        .state::<AppState>()
        .settings
        .lock()
        .unwrap()
        .firmware_update_notifications;
    let mut snapshot = app
        .state::<AppState>()
        .firmware_manager
        .lock()
        .unwrap()
        .clone();
    snapshot.update_notifications_enabled = enabled;
    snapshot
}

fn reported_state(app: &AppHandle, port: &str) -> Option<Value> {
    let state = app.state::<AppState>();
    let connection = state.connection.lock().unwrap();
    if connection.state != ConnectionState::Connected || connection.port.as_deref() != Some(port) {
        return None;
    }
    drop(connection);
    let reported = state
        .echo_desk_state
        .lock()
        .unwrap()
        .as_ref()
        .filter(|value| {
            value.get("hardwareModel").and_then(Value::as_str) == Some(MINI_REPORTED_MODEL)
        })
        .cloned();
    reported
}

fn reported_mini(app: &AppHandle, port: &str) -> bool {
    reported_state(app, port).is_some()
}

fn discovery_classification(
    proven_mini_handshake: bool,
    usb_serial_candidate: bool,
) -> (bool, bool) {
    // A generic USB adapter (including CH340) is a candidate that the user may
    // explicitly select for a bootloader probe; it never proves the attached
    // board. Legacy motherboard COM ports are listed but cannot be selected.
    if proven_mini_handshake {
        (true, false)
    } else {
        (usb_serial_candidate, true)
    }
}

fn port_matches_pin(
    expected_port: &str,
    expected_vid: Option<u16>,
    expected_pid: Option<u16>,
    expected_serial: Option<&str>,
    candidate: &aimonitor_serial::ports::PortCandidate,
) -> bool {
    if candidate.vid != expected_vid || candidate.pid != expected_pid {
        return false;
    }
    match expected_serial {
        Some(serial) => candidate.serial_number.as_deref() == Some(serial),
        None => candidate.name == expected_port,
    }
}

fn setup_pin_active(state: &FirmwareManagerState, port: &str) -> bool {
    state.setup_target_port.as_deref() == Some(port)
        && !matches!(state.setup_phase.as_deref(), Some("completed" | "cancelled"))
}

fn identity_is_visible(identity_status: &str, reveal_unknown: bool) -> bool {
    identity_status != "unknown" || reveal_unknown
}

fn discovery_status(device_count: usize, selected_still_present: bool) -> &'static str {
    if device_count == 0 {
        "noDevice"
    } else if !selected_still_present {
        "selectionRequired"
    } else {
        "ready"
    }
}

fn enumerate(app: &AppHandle, reveal_unknown: bool) -> Vec<FirmwareDevice> {
    let connection = app.state::<AppState>().connection.lock().unwrap().clone();
    let previous = app
        .state::<AppState>()
        .firmware_manager
        .lock()
        .unwrap()
        .devices
        .clone();
    aimonitor_serial::ports::list_ports()
        .into_iter()
        .filter_map(|port| {
            let connected = connection.state == ConnectionState::Connected
                && connection.port.as_deref() == Some(port.name.as_str());
            let proven = connected && reported_mini(app, &port.name);
            let reported = proven.then(|| reported_state(app, &port.name)).flatten();
            let prior = previous.iter().find(|candidate| {
                candidate.port == port.name
                    && candidate.vid == port.vid
                    && candidate.pid == port.pid
                    && candidate.usb_serial_number == port.serial_number
            });
            let proven_bootloader = prior.is_some_and(|candidate| {
                candidate.identity_status == "bootloader"
                    && candidate.chip_family.as_deref() == Some("esp32")
            });
            let identity_status = if proven {
                "verified"
            } else if proven_bootloader {
                "bootloader"
            } else {
                "unknown"
            };
            if !identity_is_visible(identity_status, reveal_unknown) {
                return None;
            }
            let setup_state = reported
                .as_ref()
                .and_then(|state| state.get("setupState"))
                .and_then(Value::as_str)
                .map(str::to_string);
            let setup_version = reported
                .as_ref()
                .and_then(|state| state.get("setupVersion"))
                .and_then(Value::as_u64);
            let device_mode = match (setup_state.as_deref(), proven_bootloader) {
                (Some("completed"), _) => "configured",
                (Some(_), _) => "setup",
                (None, true) => "bootloader",
                (None, false) if proven => "legacy",
                _ => "candidate",
            };
            let usb_serial_candidate = port.vid.is_some() && port.pid.is_some();
            let (compatible, requires_board_confirmation) =
                discovery_classification(proven, usb_serial_candidate);
            let firmware_version = reported
                .as_ref()
                .and_then(|state| state.get("firmwareVersion"))
                .and_then(Value::as_str)
                .map(str::to_string)
                .or_else(|| {
                    connected
                        .then(|| connection.info.as_ref().map(|info| info.version.clone()))
                        .flatten()
                });
            let product = port
                .product
                .clone()
                .unwrap_or_else(|| "USB serial device".into());
            Some(FirmwareDevice {
                display_name: format!("{} — {}", port.name, product),
                port: port.name,
                vid: port.vid,
                pid: port.pid,
                adapter: port.chip.map(str::to_string),
                usb_serial_number: port.serial_number,
                identity_status: identity_status.into(),
                chip_family: prior.and_then(|candidate| candidate.chip_family.clone()),
                flash_size_bytes: prior.and_then(|candidate| candidate.flash_size_bytes),
                echo_desk_handshake: proven,
                firmware_version,
                hardware_model: (proven || proven_bootloader).then(|| MINI_MODEL.into()),
                setup_state,
                setup_version,
                device_mode: device_mode.into(),
                in_bootloader: proven_bootloader,
                compatible: compatible || proven_bootloader,
                requires_board_confirmation: requires_board_confirmation && !proven_bootloader,
            })
        })
        .collect()
}

pub fn scan(app: &AppHandle, request: ScanRequest) -> FirmwareManagerState {
    update_state(app, |state| {
        state.status = "scanning".into();
        state.error_code = None;
        state.error_message = None;
    });
    let devices = enumerate(app, request.reveal_unknown);
    update_state(app, |state| {
        state.show_unknown_devices = request.reveal_unknown;
        state.devices = devices;
        if let Some(selected) = &state.selected_port {
            if !state.devices.iter().any(|device| &device.port == selected)
                && !setup_pin_active(state, selected)
            {
                state.selected_port = None;
                state.selected_hardware_model = None;
                state.board_confirmed = false;
                state.selected_vid = None;
                state.selected_pid = None;
                state.selected_usb_serial_number = None;
            }
        }
        state.status = discovery_status(state.devices.len(), state.selected_port.is_some()).into();
    });
    snapshot(app)
}

fn pause_serial(app: &AppHandle) -> Result<(), String> {
    let (send, receive) = mpsc::channel();
    serial_service::send(app, Job::Pause(send));
    receive
        .recv_timeout(PAUSE_TIMEOUT)
        .map_err(|_| "serial_release_timeout".to_string())?;
    std::thread::sleep(SETTLE_DELAY);
    Ok(())
}

fn resume_serial(app: &AppHandle, diagnostic: bool) {
    serial_service::send(
        app,
        Job::Resume {
            diagnostic_after_connect: diagnostic,
        },
    );
}

struct OperationGuard {
    app: AppHandle,
    resume: bool,
}

impl Drop for OperationGuard {
    fn drop(&mut self) {
        if self.resume {
            resume_serial(&self.app, false);
        }
        self.app
            .state::<AppState>()
            .flashing
            .store(false, Ordering::SeqCst);
    }
}

pub fn select_device(
    app: &AppHandle,
    request: SelectDeviceRequest,
) -> Result<FirmwareManagerState, String> {
    if request.hardware_model != MINI_MODEL {
        return Err("unsupported_hardware_model".into());
    }
    // Selection must be able to resolve an explicitly revealed candidate even
    // when the normal scan is filtering unknown serial adapters.
    let mut devices = enumerate(app, true);
    let device = devices
        .iter()
        .find(|device| device.port == request.port)
        .cloned()
        .ok_or_else(|| "device_not_found".to_string())?;
    if !device.compatible {
        return Err("unsupported_serial_adapter".into());
    }
    if !device.echo_desk_handshake && !request.board_confirmed {
        return Err("board_confirmation_required".into());
    }

    // A versioned EchoDesk handshake is already authoritative. Probing it here
    // toggles DTR/RTS, resets the ESP32 and was the source of the onboarding
    // hang: setup_begin raced the temporary disconnect. Unknown candidates are
    // still ROM-probed before they can become a pinned bootloader identity.
    let chip = if device.echo_desk_handshake {
        None
    } else {
        let state = app.state::<AppState>();
        if state.flashing.swap(true, Ordering::SeqCst) {
            return Err("firmware_operation_running".into());
        }
        let mut guard = OperationGuard {
            app: app.clone(),
            resume: false,
        };
        pause_serial(app)?;
        guard.resume = true;
        let probe = match probe_device(&request.port) {
            Ok(Some(probe)) if probe.chip == TargetChip::Esp32 => probe,
            Ok(Some(_)) => return Err("wrong_chip".into()),
            Ok(None) => return Err("unsupported_chip".into()),
            Err(error) => return Err(format!("bootloader_identification_failed:{error}")),
        };
        if probe.flash_size_bytes != Some(MAX_FIRMWARE_BYTES) {
            return Err("wrong_flash_size".into());
        }
        if let Some(row) = devices.iter_mut().find(|row| row.port == request.port) {
            row.flash_size_bytes = probe.flash_size_bytes;
            row.chip_family = Some("esp32".into());
            row.identity_status = "bootloader".into();
            row.device_mode = "bootloader".into();
            row.in_bootloader = true;
            row.compatible = true;
            row.requires_board_confirmation = false;
            row.hardware_model = Some(MINI_MODEL.into());
        }
        Some("esp32".to_string())
    };

    update_state(app, |state| {
        state.devices = devices;
        if let Some(row) = state
            .devices
            .iter_mut()
            .find(|row| row.port == request.port)
        {
            row.chip_family = chip.clone();
        }
        if !state.show_unknown_devices {
            state
                .devices
                .retain(|row| row.identity_status != "unknown");
        }
        state.selected_port = Some(request.port);
        state.selected_hardware_model = Some(MINI_MODEL.into());
        state.board_confirmed = device.echo_desk_handshake || request.board_confirmed;
        state.current_version = device.firmware_version;
        state.selected_vid = device.vid;
        state.selected_pid = device.pid;
        state.selected_usb_serial_number = device.usb_serial_number;
        state.setup_phase = None;
        state.setup_target_port = None;
        state.setup_completed = false;
        state.status = "ready".into();
        state.error_code = None;
        state.error_message = None;
    });
    Ok(snapshot(app))
}

fn packaged_firmware_dir() -> Result<PathBuf, String> {
    let executable = std::env::current_exe().map_err(|error| error.to_string())?;
    let production = executable
        .parent()
        .ok_or_else(|| "host_directory_unavailable".to_string())?
        .join("firmware");
    if production.join("firmware-manifest.json").is_file() {
        return Ok(production);
    }
    // Source-tree fallback is compile-time fixed and is never supplied by IPC.
    let development = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("desktop")
        .join("firmware");
    if development.join("firmware-manifest.json").is_file() {
        Ok(development)
    } else {
        Err("firmware_manifest_missing".into())
    }
}

fn manifest() -> Result<(PathBuf, FirmwareManifest), String> {
    let directory = packaged_firmware_dir()?;
    let path = directory.join("firmware-manifest.json");
    let bytes = std::fs::read(&path).map_err(|_| "firmware_manifest_unreadable".to_string())?;
    let sidecar = path.with_extension("json.sha256");
    let expected = std::fs::read_to_string(&sidecar)
        .map_err(|_| "firmware_manifest_signature_missing".to_string())?;
    let expected = expected
        .split_whitespace()
        .next()
        .ok_or_else(|| "firmware_manifest_signature_invalid".to_string())?;
    if !verify_sha256(&bytes, expected) {
        return Err("firmware_manifest_integrity_failed".into());
    }
    let parsed = parse_firmware_manifest(&bytes)
        .map_err(|error| format!("firmware_manifest_invalid:{error}"))?;
    Ok((directory, parsed))
}

fn mini_release(manifest: &FirmwareManifest) -> Result<FirmwareRelease, String> {
    manifest
        .releases
        .iter()
        .filter(|release| {
            release.hardware_model == MINI_MODEL
                && release.hardware_id == MINI_HARDWARE_ID
                && release.supported_chip == "esp32"
                && release.image_type == "merged"
                && release.flash_address == 0
                && release.flash_mode.eq_ignore_ascii_case("dio")
                && release.flash_frequency.eq_ignore_ascii_case("40m")
                && release.flash_size.eq_ignore_ascii_case("4MB")
        })
        .max_by(|left, right| {
            aimonitor_core::semver::compare(&left.firmware_version, &right.firmware_version)
        })
        .cloned()
        .ok_or_else(|| "no_supported_firmware_release".to_string())
}

pub fn check_updates(app: &AppHandle) -> Result<FirmwareManagerState, String> {
    update_state(app, |state| {
        state.status = "checking".into();
        state.error_code = None;
        state.error_message = None;
    });
    let result = (|| {
        let (_, manifest) = manifest()?;
        let release = mini_release(&manifest)?;
        if !aimonitor_core::semver::at_least(DESKTOP_VERSION, &release.minimum_desktop_version) {
            return Err("desktop_update_required".to_string());
        }
        Ok(release)
    })();
    let release = match result {
        Ok(release) => release,
        Err(internal) => {
            let (code, message) = public_error(&internal);
            update_state(app, |state| {
                state.status = "error".into();
                state.error_code = Some(code.into());
                state.error_message = Some(message.into());
            });
            return Err(internal);
        }
    };
    update_state(app, |state| {
        state.available_version = Some(release.firmware_version.clone());
        state.release_notes = Some(release.release_notes);
        state.status = match state.current_version.as_deref() {
            Some(current)
                if aimonitor_core::semver::compare(current, &release.firmware_version).is_lt() =>
            {
                "updateAvailable".into()
            }
            Some(_) => "upToDate".into(),
            None => "ready".into(),
        };
    });
    Ok(snapshot(app))
}

fn resolve_image(directory: &Path, release: &FirmwareRelease) -> Result<Vec<u8>, String> {
    let canonical_root = directory
        .canonicalize()
        .map_err(|_| "firmware_directory_unavailable".to_string())?;
    let candidate = directory.join(&release.image_file);
    if candidate.is_file() {
        let canonical = candidate
            .canonicalize()
            .map_err(|_| "firmware_file_unavailable".to_string())?;
        if !canonical.starts_with(&canonical_root) {
            return Err("firmware_path_rejected".into());
        }
        return std::fs::read(canonical).map_err(|_| "firmware_file_unreadable".into());
    }
    let url = release
        .download_url
        .as_deref()
        .filter(|url| url.starts_with("https://"))
        .ok_or_else(|| "firmware_file_unavailable".to_string())?;
    let mut response = ureq::get(url)
        .call()
        .map_err(|error| format!("firmware_download_failed:{error}"))?;
    response
        .body_mut()
        .with_config()
        .limit(MAX_FIRMWARE_BYTES)
        .read_to_vec()
        .map_err(|error| format!("firmware_download_failed:{error}"))
}

fn validate_image(release: &FirmwareRelease, image: &[u8]) -> Result<(), String> {
    if image.len() as u64 != release.size || image.len() as u64 > MAX_FIRMWARE_BYTES {
        return Err("firmware_size_mismatch".into());
    }
    if !verify_sha256(image, &release.sha256) {
        return Err("firmware_checksum_mismatch".into());
    }
    validate_merged_image(image, TargetChip::Esp32)
        .map_err(|_| "firmware_layout_invalid".to_string())
}

fn settings_backup_from_state(value: &Value) -> Value {
    json!({
        "revision": value.get("revision").cloned().unwrap_or(json!(0)),
        "settings": value.get("settings").cloned().unwrap_or_else(|| json!({})),
        "activeApp": value.get("activeApp").cloned().unwrap_or(Value::Null),
    })
}

fn backup_settings(app: &AppHandle) -> Option<Value> {
    let value = app
        .state::<AppState>()
        .echo_desk_state
        .lock()
        .unwrap()
        .clone()?;
    Some(settings_backup_from_state(&value))
}

fn reconnect_verified(
    connection: ConnectionState,
    actual_port: Option<&str>,
    actual_version: Option<&str>,
    expected_port: &str,
    expected_version: &str,
    proven_mini: bool,
) -> bool {
    connection == ConnectionState::Connected
        && actual_port == Some(expected_port)
        && actual_version == Some(expected_version)
        && proven_mini
}

fn request_device(app: &AppHandle, method: &str, params: Value) -> Result<Value, String> {
    let (send, receive) = mpsc::channel();
    serial_service::send(
        app,
        Job::EchoDeskControl {
            method: method.into(),
            params,
            reply: send,
        },
    );
    receive
        .recv_timeout(Duration::from_secs(4))
        .map_err(|_| "settings_restore_timeout".to_string())?
        .map_err(|value| {
            value
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("settings_restore_failed")
                .to_string()
        })
}

fn validate_setup_selection(
    selected: &FirmwareManagerState,
    requested_port: &str,
    present: bool,
) -> Result<(), &'static str> {
    if selected.selected_port.as_deref() != Some(requested_port) {
        return Err("setup_device_selection_changed");
    }
    if selected.selected_hardware_model.as_deref() != Some(MINI_MODEL) || !selected.board_confirmed
    {
        return Err("board_confirmation_required");
    }
    if !present && !setup_pin_active(selected, requested_port) {
        return Err("device_not_found");
    }
    Ok(())
}

fn selected_setup_port(
    app: &AppHandle,
    request: &SetupPortRequest,
) -> Result<FirmwareManagerState, String> {
    let selected = snapshot(app);
    let present = selected_target_present(&selected, &request.port);
    validate_setup_selection(&selected, &request.port, present).map_err(str::to_string)?;
    Ok(selected)
}

fn wait_for_reported_state(app: &AppHandle, port: &str, timeout: Duration) -> Option<Value> {
    let deadline = Instant::now() + timeout;
    loop {
        let selected = snapshot(app);
        let same_usb_target = aimonitor_serial::ports::list_ports()
            .iter()
            .find(|candidate| candidate.name == port)
            .is_some_and(|candidate| {
                port_matches_pin(
                    port,
                    selected.selected_vid,
                    selected.selected_pid,
                    selected.selected_usb_serial_number.as_deref(),
                    candidate,
                )
            });
        if same_usb_target {
            if let Some(state) = reported_state(app, port) {
                return Some(state);
            }
        }
        if Instant::now() >= deadline {
            return None;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

fn selected_target_present(state: &FirmwareManagerState, port: &str) -> bool {
    aimonitor_serial::ports::list_ports()
        .iter()
        .find(|candidate| candidate.name == port)
        .is_some_and(|candidate| {
            port_matches_pin(
                port,
                state.selected_vid,
                state.selected_pid,
                state.selected_usb_serial_number.as_deref(),
                candidate,
            )
        })
}

fn wait_for_selected_target(app: &AppHandle, port: &str, timeout: Duration) -> bool {
    let deadline = Instant::now() + timeout;
    loop {
        let state = snapshot(app);
        if selected_target_present(&state, port) {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

/// Starts the versioned on-device setup session on the already selected and
/// hardware-verified port. Legacy firmware and a confirmed ESP32 bootloader
/// candidate cannot understand this preparation command; both may continue to
/// the existing installer, which will provide setup-capable firmware.
pub fn setup_begin(
    app: &AppHandle,
    request: SetupPortRequest,
) -> Result<FirmwareManagerState, String> {
    let selected = selected_setup_port(app, &request)?;
    let expected_handshake = selected
        .devices
        .iter()
        .find(|device| device.port == request.port)
        .is_some_and(|device| device.echo_desk_handshake);
    update_state(app, |state| {
        state.operation = Some("guidedSetup".into());
        state.setup_target_port = Some(request.port.clone());
        state.setup_phase = Some("beginning".into());
        state.setup_completed = false;
        state.message = Some("Starting guided setup on the selected EchoDesk…".into());
        state.error_code = None;
        state.error_message = None;
    });
    let result = (|| {
        if expected_handshake {
            let reported = reported_state(app, &request.port)
                .ok_or_else(|| "setup_device_reconnect_timeout".to_string())?;
            if reported.get("setupVersion").and_then(Value::as_u64) == Some(1) {
                request_device(app, "setup_begin", json!({})).map(|_| "ready")
            } else {
                Ok("firmwareRequired")
            }
        } else {
            Ok("firmwareRequired")
        }
    })();
    match result {
        Ok(phase) => {
            update_state(app, |state| {
                state.setup_phase = Some(phase.into());
                state.message = Some(if phase == "ready" {
                    "EchoDesk acknowledged the guided setup session.".into()
                } else {
                    "The selected device needs the bundled EchoDesk OS before setup can finish."
                        .into()
                });
            });
            Ok(snapshot(app))
        }
        Err(error) => {
            update_state(app, |state| {
                state.setup_phase = Some("retryableError".into());
                state.message = Some(
                    "EchoDesk disconnected while setup was starting. Reconnect the same device and retry."
                        .into(),
                );
            });
            Err(error)
        }
    }
}

/// Completes setup only after the same selected port has reconnected as a
/// proven EchoDesk Mini running the versioned setup protocol.
pub fn setup_complete(
    app: &AppHandle,
    request: SetupPortRequest,
) -> Result<FirmwareManagerState, String> {
    selected_setup_port(app, &request)?;
    let current = snapshot(app);
    if current.setup_completed
        && current.setup_target_port.as_deref() == Some(request.port.as_str())
    {
        return Ok(current);
    }
    update_state(app, |state| {
        state.operation = Some("guidedSetup".into());
        state.setup_target_port = Some(request.port.clone());
        state.setup_phase = Some("completing".into());
        state.message = Some("Waiting for the selected EchoDesk to confirm setup…".into());
        state.error_code = None;
        state.error_message = None;
    });
    let result = (|| {
        let reported = wait_for_reported_state(app, &request.port, SETUP_RECONNECT_TIMEOUT)
            .ok_or_else(|| "setup_device_reconnect_timeout".to_string())?;
        if reported.get("setupVersion").and_then(Value::as_u64) != Some(1) {
            return Err("setup_protocol_unavailable".into());
        }
        if reported.get("setupState").and_then(Value::as_str) != Some("completed") {
            request_device(app, "setup_complete", json!({}))?;
        }
        Ok(())
    })();
    if let Err(error) = result {
        update_state(app, |state| {
            state.setup_phase = Some("retryableError".into());
            state.message = Some(
                "Setup is waiting for the same EchoDesk to reconnect. Reconnect it and retry."
                    .into(),
            );
        });
        return Err(error);
    }
    update_state(app, |state| {
        state.setup_phase = Some("completed".into());
        state.setup_completed = true;
        state.message = Some("EchoDesk setup is complete.".into());
        state.error_code = None;
        state.error_message = None;
    });
    Ok(snapshot(app))
}

/// Factory reset is intentionally an application-level NVS operation. The
/// device firmware owns which setup keys are cleared; the host never erases
/// flash or supplies an arbitrary address.
pub fn factory_reset(
    app: &AppHandle,
    request: SetupPortRequest,
) -> Result<FirmwareManagerState, String> {
    let connection = app.state::<AppState>().connection.lock().unwrap().clone();
    if connection.state != ConnectionState::Connected
        || connection.port.as_deref() != Some(request.port.as_str())
        || !reported_mini(app, &request.port)
    {
        return Err("setup_device_selection_changed".into());
    }
    request_device(app, "factory_reset", json!({}))?;
    update_state(app, |state| {
        state.setup_target_port = Some(request.port);
        state.setup_phase = Some("waitingForDesktop".into());
        state.setup_completed = false;
        state.message = Some("Factory reset accepted. EchoDesk is restarting into setup.".into());
    });
    Ok(snapshot(app))
}

fn restore_settings(app: &AppHandle, backup: &Value) {
    let current = app
        .state::<AppState>()
        .echo_desk_state
        .lock()
        .unwrap()
        .clone();
    let Some(base_revision) = current
        .as_ref()
        .and_then(|value| value.get("revision"))
        .and_then(Value::as_u64)
    else {
        return;
    };
    let settings = backup.get("settings").cloned().unwrap_or_else(|| json!({}));
    if settings
        .as_object()
        .is_some_and(|settings| !settings.is_empty())
    {
        let _ = request_device(
            app,
            "set_settings",
            json!({"baseRevision": base_revision, "persist": true, "settings": settings}),
        );
    }
    if let Some(active) = backup
        .get("activeApp")
        .and_then(Value::as_str)
        .filter(|active| !active.is_empty())
    {
        let _ = request_device(app, "open_app", json!({"app": active}));
    }
}

#[cfg(windows)]
struct SleepGuard;

#[cfg(windows)]
impl SleepGuard {
    fn new() -> Self {
        use windows::Win32::System::Power::{
            SetThreadExecutionState, ES_CONTINUOUS, ES_SYSTEM_REQUIRED,
        };
        unsafe {
            let _ = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        }
        Self
    }
}

#[cfg(windows)]
impl Drop for SleepGuard {
    fn drop(&mut self) {
        use windows::Win32::System::Power::{SetThreadExecutionState, ES_CONTINUOUS};
        unsafe {
            let _ = SetThreadExecutionState(ES_CONTINUOUS);
        }
    }
}

#[cfg(not(windows))]
struct SleepGuard;

#[cfg(not(windows))]
impl SleepGuard {
    fn new() -> Self {
        Self
    }
}

fn cancelled(app: &AppHandle) -> bool {
    app.state::<AppState>()
        .firmware_manager
        .lock()
        .unwrap()
        .cancel_requested
}

fn run_install(
    app: AppHandle,
    mode: InstallMode,
    port: String,
    policy: InstallPolicy,
) -> Result<(), String> {
    let state = app.state::<AppState>();
    if state.flashing.swap(true, Ordering::SeqCst) {
        return Err("firmware_operation_running".into());
    }
    let mut guard = OperationGuard {
        app: app.clone(),
        resume: false,
    };
    let guided = setup_pin_active(&snapshot(&app), &port);
    if guided {
        update_state(&app, |state| {
            state.setup_phase = Some("waitingForReconnect".into());
            state.message = Some(
                "Waiting for the same EchoDesk to reconnect before installation…".into(),
            );
        });
        if !wait_for_selected_target(&app, &port, SETUP_RECONNECT_TIMEOUT) {
            return Err("setup_device_reconnect_timeout".into());
        }
        update_state(&app, |state| {
            state.setup_phase = Some("installing".into());
            state.message = Some("The selected EchoDesk is verified. Preparing EchoDesk OS…".into());
        });
    }
    let backup = backup_settings(&app);
    update_state(&app, |state| {
        state.settings_backup = backup.clone();
        state.settings_backup_available = backup.is_some();
        state.status = "preparing".into();
        state.stage = Some("preparing".into());
    });

    let (directory, catalogue) = manifest()?;
    let release = mini_release(&catalogue)?;
    validate_desktop_version_for_policy(policy, DESKTOP_VERSION, &release.minimum_desktop_version)
        .map_err(str::to_string)?;
    let current = snapshot(&app).current_version;
    validate_version_for_policy(policy, mode, current.as_deref(), &release.firmware_version)
        .map_err(str::to_string)?;
    update_state(&app, |state| {
        state.status = "installing".into();
        state.stage = Some("verifying_firmware".into());
        state.available_version = Some(release.firmware_version.clone());
        state.release_notes = Some(release.release_notes.clone());
    });
    let image = resolve_image(&directory, &release)?;
    validate_image(&release, &image)?;
    if cancelled(&app) {
        return Err("operation_cancelled".into());
    }

    pause_serial(&app)?;
    guard.resume = true;
    update_state(&app, |state| {
        state.status = "enteringBootloader".into();
        state.stage = Some("entering_bootloader".into());
    });
    match probe_device(&port) {
        Ok(probe) => validate_probe_for_policy(policy, probe).map_err(str::to_string)?,
        Err(error) => return Err(format!("bootloader_identification_failed:{error}")),
    }
    if cancelled(&app) {
        return Err("operation_cancelled".into());
    }

    let _sleep_guard = SleepGuard::new();
    let flash_result =
        flash_image(
            &port,
            &image,
            FLASH_BAUD,
            TargetChip::Esp32,
            &mut |event| match event {
                FlashEvent::Connecting | FlashEvent::Connected { .. } | FlashEvent::Erasing => {
                    update_state(&app, |state| {
                        state.status = "enteringBootloader".into();
                        state.stage = Some("entering_bootloader".into());
                        state.progress_percent = None;
                    });
                }
                FlashEvent::Writing { written, total } => update_state(&app, |state| {
                    state.status = "writing".into();
                    state.stage = Some("writing".into());
                    state.progress_percent = (total > 0)
                        .then(|| ((written as f64 / total as f64) * 100.0).round() as u32);
                }),
                FlashEvent::Verifying => update_state(&app, |state| {
                    state.status = "verifying".into();
                    state.stage = Some("verifying_installation".into());
                    state.progress_percent = None;
                }),
                FlashEvent::Rebooting => update_state(&app, |state| {
                    state.write_completed = true;
                    state.status = "restarting".into();
                    state.stage = Some("restarting".into());
                    state.progress_percent = None;
                }),
                FlashEvent::Done => update_state(&app, |state| {
                    state.write_completed = true;
                    state.status = "reconnecting".into();
                    state.stage = Some("reconnecting".into());
                    state.progress_percent = None;
                }),
            },
        )
        .map_err(|error| format!("flash_write_failed:{error}"));
    if flash_result.is_err() {
        return flash_result;
    }

    resume_serial(&app, true);
    guard.resume = false;
    let deadline = Instant::now() + RECONNECT_TIMEOUT;
    let mut verified = false;
    while Instant::now() < deadline {
        let connection = state.connection.lock().unwrap().clone();
        if selected_target_present(&snapshot(&app), &port)
            && reconnect_verified(
            connection.state,
            connection.port.as_deref(),
            connection.info.as_ref().map(|info| info.version.as_str()),
            &port,
            &release.firmware_version,
            reported_mini(&app, &port),
        )
        {
            verified = true;
            break;
        }
        std::thread::sleep(Duration::from_millis(250));
    }
    if !verified {
        update_state(&app, |state| {
            state.status = "partial".into();
            state.write_completed = true;
            state.reconnected = false;
            state.error_code = Some("reconnect_verification_failed".into());
            state.error_message = Some(
                "Firmware was written, but EchoDesk OS did not reconnect and verify in time."
                    .into(),
            );
            if setup_pin_active(state, &port) {
                state.setup_phase = Some("retryableError".into());
                state.message = Some(
                    "Firmware was written. Reconnect the same EchoDesk and retry setup completion."
                        .into(),
                );
            }
        });
        return Ok(());
    }

    if let Some(backup) = backup.as_ref() {
        restore_settings(&app, backup);
    }
    {
        let mut settings = state.settings.lock().unwrap();
        settings.installed_firmware_version = Some(release.firmware_version.clone());
        settings.save(&app);
    }
    update_state(&app, |state| {
        state.status = "success".into();
        state.stage = None;
        state.write_completed = true;
        state.reconnected = true;
        state.current_version = Some(release.firmware_version);
        state.error_code = None;
        state.error_message = None;
        if setup_pin_active(state, &port) {
            state.setup_phase = Some("awaitingCompletion".into());
            state.message = Some("EchoDesk OS is verified. Finishing device setup…".into());
        }
    });
    Ok(())
}

pub fn install(app: &AppHandle, request: InstallRequest) -> Result<FirmwareManagerState, String> {
    let selected = snapshot(app);
    if matches!(
        selected.status.as_str(),
        "preparing"
            | "installing"
            | "enteringBootloader"
            | "writing"
            | "verifying"
            | "restarting"
            | "reconnecting"
    ) {
        return Err("firmware_operation_running".into());
    }
    let port = selected
        .selected_port
        .clone()
        .ok_or_else(|| "device_not_found".to_string())?;
    if !selected_target_present(&selected, &port) && !setup_pin_active(&selected, &port) {
        return Err("device_not_found".into());
    }
    if selected.selected_hardware_model.as_deref() != Some(MINI_MODEL) || !selected.board_confirmed
    {
        return Err("board_confirmation_required".into());
    }
    update_state(app, |state| {
        state.status = "preparing".into();
        state.operation = Some(request.mode.wire().into());
        state.stage = Some("preparing".into());
        state.progress_percent = None;
        state.message = None;
        state.error_code = None;
        state.error_message = None;
        state.write_completed = false;
        state.reconnected = false;
        state.cancel_requested = false;
    });
    let worker_app = app.clone();
    std::thread::Builder::new()
        .name("echodesk-firmware-install".into())
        .spawn(move || {
            if let Err(error) = run_install(
                worker_app.clone(),
                request.mode,
                port.clone(),
                InstallPolicy::Normal,
            ) {
                let cancelled = error == "operation_cancelled";
                update_state(&worker_app, |state| {
                    state.status = if cancelled { "cancelled" } else { "error" }.into();
                    if setup_pin_active(state, &port) {
                        state.setup_phase = Some(if cancelled {
                            "cancelled"
                        } else {
                            "retryableError"
                        }
                        .into());
                    }
                    if !cancelled {
                        let (code, message) = public_error(&error);
                        state.error_code = Some(code.into());
                        state.error_message = Some(message.into());
                    }
                    state.progress_percent = None;
                });
            }
        })
        .map_err(|error| format!("firmware_worker_failed:{error}"))?;
    Ok(snapshot(app))
}

/// Explicit recovery escape hatch for a user-selected serial port. This does
/// not trust adapter metadata, an EchoDesk handshake, a hardware-model claim,
/// the reported flash capacity, or the installed firmware version. The
/// bundled image and manifest are still verified, and the ROM bootloader must
/// positively identify the target as an original ESP32 before any write.
pub fn manual_recovery(
    app: &AppHandle,
    request: ManualRecoveryRequest,
) -> Result<FirmwareManagerState, String> {
    let current = snapshot(app);
    if matches!(
        current.status.as_str(),
        "preparing"
            | "installing"
            | "enteringBootloader"
            | "writing"
            | "verifying"
            | "restarting"
            | "reconnecting"
    ) {
        return Err("firmware_operation_running".into());
    }
    let port = aimonitor_serial::ports::list_ports()
        .into_iter()
        .find(|item| item.name == request.port)
        .map(|item| item.name)
        .ok_or_else(|| "device_not_found".to_string())?;

    update_state(app, |state| {
        state.status = "preparing".into();
        state.operation = Some("manualRecovery".into());
        state.stage = Some("preparing".into());
        state.progress_percent = None;
        state.message = Some(
            "Manual recovery will continue only if the selected port reports an ESP32 bootloader."
                .into(),
        );
        state.error_code = None;
        state.error_message = None;
        state.selected_port = Some(port.clone());
        state.selected_hardware_model = None;
        state.board_confirmed = false;
        state.write_completed = false;
        state.reconnected = false;
        state.cancel_requested = false;
    });
    let worker_app = app.clone();
    std::thread::Builder::new()
        .name("echodesk-firmware-manual-recovery".into())
        .spawn(move || {
            if let Err(error) = run_install(
                worker_app.clone(),
                InstallMode::Repair,
                port,
                InstallPolicy::Esp32OnlyRecovery,
            ) {
                let cancelled = error == "operation_cancelled";
                update_state(&worker_app, |state| {
                    state.status = if cancelled { "cancelled" } else { "error" }.into();
                    if !cancelled {
                        let (code, message) = public_error(&error);
                        state.error_code = Some(code.into());
                        state.error_message = Some(message.into());
                    }
                    state.progress_percent = None;
                });
            }
        })
        .map_err(|error| format!("firmware_worker_failed:{error}"))?;
    Ok(snapshot(app))
}

pub fn cancel(app: &AppHandle) -> Result<FirmwareManagerState, String> {
    let current = snapshot(app);
    if !matches!(
        current.status.as_str(),
        "preparing"
            | "installing"
            | "enteringBootloader"
            | "writing"
            | "verifying"
            | "restarting"
            | "reconnecting"
    ) {
        return Ok(current);
    }
    if matches!(
        current.stage.as_deref(),
        Some("writing" | "verifying_installation" | "restarting" | "reconnecting")
    ) {
        return Err("cannot_cancel_active_write".into());
    }
    update_state(app, |state| {
        state.cancel_requested = true;
        state.message = Some("Cancelling before the firmware write begins…".into());
    });
    Ok(snapshot(app))
}

pub fn set_update_notifications(app: &AppHandle, enabled: bool) -> FirmwareManagerState {
    {
        let app_state = app.state::<AppState>();
        let mut settings = app_state.settings.lock().unwrap();
        settings.firmware_update_notifications = enabled;
        settings.save(app);
    }
    bump(app);
    snapshot(app)
}

/// Stable, non-sensitive error contract for the consumer UI. Raw paths and
/// tool diagnostics remain in the host log and are never returned over IPC.
pub fn public_error(internal: &str) -> (&'static str, &'static str) {
    let code = internal.split(':').next().unwrap_or(internal);
    match code {
        "wrong_chip" | "unsupported_chip" | "wrong_flash_size" | "unsupported_hardware_model"
        | "unsupported_serial_adapter" => (
            "wrong_hardware",
            "This device does not match the selected EchoDesk Mini hardware.",
        ),
        "board_confirmation_required" => (
            "board_confirmation_required",
            "Confirm that the selected device is an EchoDesk Mini before continuing.",
        ),
        "firmware_checksum_mismatch" | "firmware_manifest_integrity_failed" => (
            "checksum_mismatch",
            "Firmware integrity verification failed. Reinstall EchoDesk Desktop and try again.",
        ),
        "firmware_size_mismatch" | "firmware_layout_invalid" | "firmware_manifest_invalid" => (
            "invalid_firmware",
            "The packaged firmware is not valid for EchoDesk Mini.",
        ),
        "serial_release_timeout" | "device_not_found" => (
            "port_busy",
            "EchoDesk could not access the selected USB device. Close serial tools, reconnect it, and retry.",
        ),
        "bootloader_identification_failed" => (
            "bootloader_unavailable",
            "EchoDesk could not identify the ESP32 bootloader. Reconnect the device and follow recovery mode instructions.",
        ),
        "flash_write_failed" => (
            "write_failed",
            "The firmware write did not complete. Keep the device connected and use Repair.",
        ),
        "firmware_downgrade_blocked" => (
            "downgrade_blocked",
            "This firmware is older than the installed version and cannot be installed automatically.",
        ),
        "desktop_update_required" => (
            "desktop_update_required",
            "Update EchoDesk Desktop before installing this firmware.",
        ),
        "cannot_cancel_active_write" => (
            "write_in_progress",
            "The write is in progress and cannot be interrupted safely.",
        ),
        "setup_device_selection_changed" => (
            "setup_device_changed",
            "The selected EchoDesk changed or disconnected. Select the device again before continuing.",
        ),
        "setup_device_reconnect_timeout" => (
            "setup_reconnect_timeout",
            "The selected EchoDesk did not reconnect in time. Reconnect it and retry this setup step.",
        ),
        "setup_protocol_unavailable" => (
            "setup_protocol_unavailable",
            "Install the bundled EchoDesk OS before completing guided setup.",
        ),
        _ => (
            "firmware_error",
            "EchoDesk could not complete the firmware operation. Reconnect the device and try again.",
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn install_modes_are_allowlisted() {
        for (json, expected) in [
            (r#"{"mode":"install"}"#, "install"),
            (r#"{"mode":"update"}"#, "update"),
            (r#"{"mode":"reinstall"}"#, "reinstall"),
            (r#"{"mode":"repair"}"#, "repair"),
        ] {
            let request: InstallRequest = serde_json::from_str(json).unwrap();
            assert_eq!(request.mode.wire(), expected);
        }
        assert!(serde_json::from_str::<InstallRequest>(r#"{"mode":"erase"}"#).is_err());
        assert!(
            serde_json::from_str::<InstallRequest>(r#"{"mode":"install","address":4096}"#).is_err()
        );
    }

    #[test]
    fn scan_reveals_unknown_devices_only_when_explicitly_requested() {
        let hidden: ScanRequest = serde_json::from_str(r#"{}"#).unwrap();
        let revealed: ScanRequest =
            serde_json::from_str(r#"{"revealUnknown":true}"#).unwrap();
        assert!(!hidden.reveal_unknown);
        assert!(revealed.reveal_unknown);
        assert!(serde_json::from_str::<ScanRequest>(
            r#"{"revealUnknown":true,"trustCh340":true}"#
        )
        .is_err());
        assert!(!identity_is_visible("unknown", false));
        assert!(identity_is_visible("unknown", true));
        assert!(identity_is_visible("verified", false));
        assert!(identity_is_visible("bootloader", false));
    }

    #[test]
    fn selection_rejects_arbitrary_fields_and_models() {
        assert!(serde_json::from_str::<SelectDeviceRequest>(
            r#"{"port":"COM5","hardwareModel":"EchoDesk Mini","boardConfirmed":true,"image":"evil.bin"}"#
        )
        .is_err());
        let request: SelectDeviceRequest = serde_json::from_str(
            r#"{"port":"COM5","hardwareModel":"Other","boardConfirmed":true}"#,
        )
        .unwrap();
        assert_ne!(request.hardware_model, MINI_MODEL);
    }

    #[test]
    fn manual_recovery_accepts_only_a_port() {
        let request: ManualRecoveryRequest = serde_json::from_str(r#"{"port":"COM7"}"#).unwrap();
        assert_eq!(request.port, "COM7");
        assert!(serde_json::from_str::<ManualRecoveryRequest>(
            r#"{"port":"COM7","image":"evil.bin"}"#
        )
        .is_err());
        assert!(
            serde_json::from_str::<ManualRecoveryRequest>(r#"{"port":"COM7","address":4096}"#)
                .is_err()
        );
    }

    #[test]
    fn guided_setup_requests_accept_only_the_pinned_port() {
        let request: SetupPortRequest = serde_json::from_str(r#"{"port":"COM7"}"#).unwrap();
        assert_eq!(request.port, "COM7");
        assert!(
            serde_json::from_str::<SetupPortRequest>(r#"{"port":"COM7","image":"other.bin"}"#)
                .is_err()
        );
        let mut selected = FirmwareManagerState::default();
        selected.selected_port = Some("COM7".into());
        selected.selected_hardware_model = Some(MINI_MODEL.into());
        selected.board_confirmed = true;
        assert_eq!(validate_setup_selection(&selected, "COM7", true), Ok(()));
        assert_eq!(
            validate_setup_selection(&selected, "COM8", true),
            Err("setup_device_selection_changed")
        );
        selected.setup_target_port = Some("COM7".into());
        selected.setup_phase = Some("waitingForReconnect".into());
        assert_eq!(
            validate_setup_selection(&selected, "COM7", false),
            Ok(())
        );
        selected.board_confirmed = false;
        assert_eq!(
            validate_setup_selection(&selected, "COM7", true),
            Err("board_confirmation_required")
        );
    }

    #[test]
    fn setup_pin_rejects_a_different_usb_identity_on_the_same_port() {
        let candidate = aimonitor_serial::ports::PortCandidate {
            name: "COM7".into(),
            vid: Some(0x1a86),
            pid: Some(0x7523),
            serial_number: Some("MINI-123".into()),
            product: Some("USB-SERIAL CH340".into()),
            chip: Some("CH340"),
        };
        assert!(port_matches_pin(
            "COM7",
            Some(0x1a86),
            Some(0x7523),
            Some("MINI-123"),
            &candidate,
        ));
        assert!(!port_matches_pin(
            "COM7",
            Some(0x1a86),
            Some(0x7523),
            Some("OTHER"),
            &candidate,
        ));
        assert!(!port_matches_pin(
            "COM8",
            Some(0x1a86),
            Some(0x7523),
            None,
            &candidate,
        ));
    }

    #[test]
    fn manual_recovery_requires_esp32_but_not_reported_flash_size() {
        let esp32_4mb = DeviceProbe {
            chip: TargetChip::Esp32,
            flash_size_bytes: Some(MAX_FIRMWARE_BYTES),
        };
        let esp32_unknown_size = DeviceProbe {
            chip: TargetChip::Esp32,
            flash_size_bytes: None,
        };
        let esp32s3 = DeviceProbe {
            chip: TargetChip::Esp32S3,
            flash_size_bytes: Some(16 * 1024 * 1024),
        };

        assert_eq!(
            validate_probe_for_policy(InstallPolicy::Normal, Some(esp32_4mb)),
            Ok(())
        );
        assert_eq!(
            validate_probe_for_policy(InstallPolicy::Normal, Some(esp32_unknown_size)),
            Err("wrong_flash_size")
        );
        assert_eq!(
            validate_probe_for_policy(InstallPolicy::Esp32OnlyRecovery, Some(esp32_unknown_size)),
            Ok(())
        );
        assert_eq!(
            validate_probe_for_policy(InstallPolicy::Esp32OnlyRecovery, Some(esp32s3)),
            Err("wrong_chip")
        );
        assert_eq!(
            validate_probe_for_policy(InstallPolicy::Esp32OnlyRecovery, None),
            Err("unsupported_chip")
        );
    }

    #[test]
    fn manual_recovery_bypasses_version_gates() {
        assert_eq!(
            validate_desktop_version_for_policy(InstallPolicy::Normal, "0.2.0", "0.3.0"),
            Err("desktop_update_required")
        );
        assert_eq!(
            validate_desktop_version_for_policy(InstallPolicy::Esp32OnlyRecovery, "0.2.0", "0.3.0"),
            Ok(())
        );
        assert_eq!(
            validate_version_for_policy(
                InstallPolicy::Normal,
                InstallMode::Update,
                Some("2.28.0"),
                "2.28.0"
            ),
            Err("firmware_not_newer")
        );
        assert_eq!(
            validate_version_for_policy(
                InstallPolicy::Normal,
                InstallMode::Repair,
                Some("2.29.0"),
                "2.28.0"
            ),
            Err("firmware_downgrade_blocked")
        );
        assert_eq!(
            validate_version_for_policy(
                InstallPolicy::Esp32OnlyRecovery,
                InstallMode::Repair,
                Some("99.0.0"),
                "2.28.0"
            ),
            Ok(())
        );
    }

    #[test]
    fn release_selection_enforces_complete_mini_flash_layout() {
        let valid = FirmwareRelease {
            firmware_version: "2.28.0".into(),
            hardware_model: MINI_MODEL.into(),
            hardware_id: MINI_HARDWARE_ID.into(),
            supported_chip: "esp32".into(),
            image_type: "merged".into(),
            flash_address: 0,
            flash_mode: "dio".into(),
            flash_frequency: "40m".into(),
            flash_size: "4MB".into(),
            image_file: "image.bin".into(),
            download_url: None,
            sha256: "A".repeat(64),
            size: 1,
            release_notes: "notes".into(),
            minimum_desktop_version: "0.2.0".into(),
        };
        let catalogue = FirmwareManifest {
            schema_version: 1,
            generated_at: "now".into(),
            releases: vec![valid.clone()],
        };
        assert_eq!(mini_release(&catalogue).unwrap(), valid);
        let mut wrong = catalogue;
        wrong.releases[0].flash_address = 0x1000;
        assert_eq!(
            mini_release(&wrong).unwrap_err(),
            "no_supported_firmware_release"
        );
    }

    #[test]
    fn desktop_version_and_public_errors_are_stable() {
        assert_eq!(DESKTOP_VERSION, "0.2.7");
        assert!(aimonitor_core::semver::at_least(DESKTOP_VERSION, "0.2.0"));
        assert_eq!(public_error("wrong_flash_size").0, "wrong_hardware");
        assert_eq!(
            public_error("firmware_checksum_mismatch").0,
            "checksum_mismatch"
        );
        let (_, message) = public_error("flash_write_failed:C:\\secret\\firmware.bin");
        assert!(!message.contains("secret"));
    }

    #[test]
    fn discovery_never_treats_a_generic_serial_adapter_as_board_identity() {
        // CH340/other USB serial devices are selectable candidates, but still
        // require explicit board confirmation and a successful bootloader probe.
        assert_eq!(discovery_classification(false, true), (true, true));
        assert_eq!(discovery_classification(false, false), (false, true));
        assert_eq!(discovery_classification(true, true), (true, false));
    }

    #[test]
    fn lost_selection_returns_to_explicit_selection() {
        assert_eq!(discovery_status(0, false), "noDevice");
        assert_eq!(discovery_status(1, false), "selectionRequired");
        assert_eq!(discovery_status(2, true), "ready");
    }

    #[test]
    fn reconnect_requires_port_version_and_proven_model() {
        assert!(reconnect_verified(
            ConnectionState::Connected,
            Some("COM5"),
            Some("2.28.0"),
            "COM5",
            "2.28.0",
            true,
        ));
        assert!(!reconnect_verified(
            ConnectionState::Connected,
            Some("COM6"),
            Some("2.28.0"),
            "COM5",
            "2.28.0",
            true
        ));
        assert!(!reconnect_verified(
            ConnectionState::Connected,
            Some("COM5"),
            Some("2.27.0"),
            "COM5",
            "2.28.0",
            true
        ));
        assert!(!reconnect_verified(
            ConnectionState::Connected,
            Some("COM5"),
            Some("2.28.0"),
            "COM5",
            "2.28.0",
            false
        ));
    }

    #[test]
    fn settings_backup_preserves_preferences_and_default_app() {
        let source = json!({
            "revision": 7,
            "activeApp": "performance",
            "settings": {"brightness": 72, "theme": "light", "clock24Hour": true}
        });
        let backup = settings_backup_from_state(&source);
        assert_eq!(backup["revision"], 7);
        assert_eq!(backup["activeApp"], "performance");
        assert_eq!(backup["settings"]["brightness"], 72);
        assert_eq!(backup["settings"]["theme"], "light");
    }
}
