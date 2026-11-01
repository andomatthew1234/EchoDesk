//! Opt-in Wi-Fi setup for EchoDesk Mini.
//!
//! USB remains the normal transport. This module only discovers the fixed,
//! versioned firmware protocol and provisions a target returned by that scan;
//! callers cannot supply an address or port.

use crate::state::AppState;
use getrandom::fill as fill_random;
use serde::{Deserialize, Serialize};
use std::io::{Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, TcpStream, UdpSocket};
use std::sync::atomic::Ordering;
use std::time::{Duration, Instant};
use tauri::{AppHandle, Manager};

const DISCOVERY_PORT: u16 = 39_320;
const PROVISION_PORT: u16 = 39_321;
const PROTOCOL_VERSION: u8 = 1;
const MAX_WIRE_BYTES: usize = 1_024;
const MAX_DEVICES: usize = 16;
const DISCOVERY_WINDOW: Duration = Duration::from_millis(1_400);
const DISCOVERY_TTL: Duration = Duration::from_secs(5 * 60);
const IO_TIMEOUT: Duration = Duration::from_secs(10);
const EXPECTED_MODEL: &str = "Freenove FNK0114A_2P4";

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct WifiSetupDevice {
    pub device_id: String,
    pub display_name: String,
    pub model: String,
    pub setup_ssid: Option<String>,
    pub address: String,
    pub port: u16,
    pub setup_mode: bool,
    pub paired: bool,
    pub firmware_version: String,
    pub setup_state: String,
    #[serde(skip)]
    discovered_at: Instant,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WifiSetupState {
    pub available: bool,
    pub status: String,
    pub message: String,
    pub error_code: Option<String>,
    pub devices: Vec<WifiSetupDevice>,
    pub selected_device_id: Option<String>,
    pub provisioned: bool,
    pub connected_ssid: Option<String>,
    pub address: Option<String>,
    #[serde(skip)]
    operation_id: u64,
}

impl Default for WifiSetupState {
    fn default() -> Self {
        Self {
            available: true,
            status: "idle".into(),
            message: "Wi-Fi setup is optional. USB remains the primary connection.".into(),
            error_code: None,
            devices: Vec::new(),
            selected_device_id: None,
            provisioned: false,
            connected_ssid: None,
            address: None,
            operation_id: 0,
        }
    }
}

#[derive(Debug)]
pub struct WifiSetupError {
    pub code: &'static str,
    pub message: &'static str,
}

impl WifiSetupError {
    fn new(code: &'static str, message: &'static str) -> Self {
        Self { code, message }
    }
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DiscoverRequest {}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ProvisionRequest {
    pub device_id: String,
    pub setup_code: String,
    pub ssid: String,
    pub password: String,
}

#[derive(Serialize)]
struct DiscoveryRequest<'a> {
    #[serde(rename = "type")]
    kind: &'static str,
    version: u8,
    nonce: &'a str,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct DiscoveryResponse {
    #[serde(rename = "type")]
    kind: String,
    version: u8,
    nonce: String,
    device_id: String,
    name: String,
    model: String,
    firmware_version: String,
    setup_state: String,
    paired: bool,
    #[serde(default)]
    setup_mode: bool,
    #[serde(default)]
    authenticated: bool,
    ip: String,
    port: u16,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct IdentifyMessage<'a> {
    #[serde(rename = "type")]
    kind: &'static str,
    version: u8,
    client_token: &'a str,
    nonce: &'a str,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct ProvisionMessage<'a> {
    #[serde(rename = "type")]
    kind: &'static str,
    version: u8,
    pairing_code: &'a str,
    ssid: &'a str,
    password: &'a str,
    client_token: &'a str,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct ProvisionResponse {
    #[serde(rename = "type")]
    kind: String,
    version: u8,
    ok: bool,
    #[serde(default)]
    device_id: Option<String>,
    #[serde(default)]
    restarting: bool,
    #[serde(default)]
    #[serde(rename = "code")]
    error_code: Option<String>,
    #[serde(default)]
    message: Option<String>,
}

pub fn snapshot(app: &AppHandle) -> WifiSetupState {
    app.state::<AppState>().wifi_setup.lock().unwrap().clone()
}

fn bump(app: &AppHandle) {
    app.state::<AppState>()
        .desktop_broker_revision
        .fetch_add(1, Ordering::AcqRel);
}

fn random_hex(byte_count: usize) -> Result<String, WifiSetupError> {
    let mut bytes = vec![0u8; byte_count];
    fill_random(&mut bytes).map_err(|_| {
        WifiSetupError::new(
            "secure_random_unavailable",
            "Windows secure random generation is unavailable.",
        )
    })?;
    Ok(bytes.iter().map(|byte| format!("{byte:02x}")).collect())
}

fn valid_device_id(value: &str) -> bool {
    value.len() == 12
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn valid_client_token(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn valid_safe_text(value: &str, max_bytes: usize) -> bool {
    value.len() <= max_bytes && !value.chars().any(char::is_control)
}

fn validate_credentials(request: &ProvisionRequest) -> Result<(), WifiSetupError> {
    if !valid_device_id(&request.device_id) {
        return Err(WifiSetupError::new(
            "invalid_device",
            "Choose an EchoDesk found by the latest scan.",
        ));
    }
    if request.setup_code.len() != 8
        || !request.setup_code.bytes().all(|byte| byte.is_ascii_digit())
    {
        return Err(WifiSetupError::new(
            "invalid_setup_code",
            "The setup code must be the 8 digits shown on EchoDesk.",
        ));
    }
    if request.ssid.is_empty() || !valid_safe_text(&request.ssid, 32) {
        return Err(WifiSetupError::new(
            "invalid_wifi_name",
            "The Wi-Fi name must contain 1 to 32 bytes and no control characters.",
        ));
    }
    if !valid_safe_text(&request.password, 63)
        || (!request.password.is_empty() && request.password.len() < 8)
    {
        return Err(WifiSetupError::new(
            "invalid_wifi_password",
            "Use an empty password for an open network, or 8 to 63 bytes for WPA2.",
        ));
    }
    Ok(())
}

fn allowed_address(ip: Ipv4Addr) -> bool {
    ip.is_private() || ip.is_link_local()
}

fn parse_discovery(
    bytes: &[u8],
    source: SocketAddr,
    expected_nonce: &str,
) -> Option<WifiSetupDevice> {
    if bytes.is_empty() || bytes.len() > MAX_WIRE_BYTES {
        return None;
    }
    let response: DiscoveryResponse = serde_json::from_slice(bytes).ok()?;
    let declared: Ipv4Addr = response.ip.parse().ok()?;
    let IpAddr::V4(source_ip) = source.ip() else {
        return None;
    };
    if response.kind != "echodesk_device"
        || response.version != PROTOCOL_VERSION
        || response.nonce != expected_nonce
        || !valid_device_id(&response.device_id)
        || response.name != "EchoDesk"
        || response.model != EXPECTED_MODEL
        || response.port != PROVISION_PORT
        || source.port() != DISCOVERY_PORT
        || declared != source_ip
        || !allowed_address(declared)
        || !valid_safe_text(&response.firmware_version, 32)
        || response.firmware_version.is_empty()
        || !valid_safe_text(&response.setup_state, 24)
        || response.setup_state.is_empty()
        || (response.setup_mode && response.paired)
        || response.authenticated
        || (response.setup_mode && declared != Ipv4Addr::new(192, 168, 4, 1))
    {
        return None;
    }
    let setup_ssid = response
        .setup_mode
        .then(|| format!("EchoDesk-{}", response.device_id[8..].to_ascii_uppercase()));
    Some(WifiSetupDevice {
        device_id: response.device_id,
        display_name: response.name,
        model: response.model,
        setup_ssid,
        address: declared.to_string(),
        port: response.port,
        setup_mode: response.setup_mode,
        paired: response.paired,
        firmware_version: response.firmware_version,
        setup_state: response.setup_state,
        discovered_at: Instant::now(),
    })
}

fn discover_devices() -> Result<Vec<WifiSetupDevice>, WifiSetupError> {
    let nonce = random_hex(12)?;
    let payload = serde_json::to_vec(&DiscoveryRequest {
        kind: "echodesk_discover",
        version: PROTOCOL_VERSION,
        nonce: &nonce,
    })
    .map_err(|_| WifiSetupError::new("internal", "Could not create a discovery request."))?;
    let socket = UdpSocket::bind((Ipv4Addr::UNSPECIFIED, 0)).map_err(|_| {
        WifiSetupError::new(
            "network_unavailable",
            "Wi-Fi discovery could not open a local network socket.",
        )
    })?;
    socket.set_broadcast(true).map_err(|_| {
        WifiSetupError::new(
            "network_unavailable",
            "Wi-Fi discovery is unavailable on this network.",
        )
    })?;
    socket
        .set_read_timeout(Some(Duration::from_millis(120)))
        .ok();

    let broadcast = SocketAddr::from((Ipv4Addr::BROADCAST, DISCOVERY_PORT));
    let setup = SocketAddr::from(([192, 168, 4, 1], DISCOVERY_PORT));
    let broadcast_sent = socket.send_to(&payload, broadcast).is_ok();
    let setup_sent = socket.send_to(&payload, setup).is_ok();
    if !broadcast_sent && !setup_sent {
        return Err(WifiSetupError::new(
            "network_unavailable",
            "No active network could be used for EchoDesk discovery.",
        ));
    }

    let deadline = Instant::now() + DISCOVERY_WINDOW;
    let mut devices = Vec::new();
    let mut buffer = [0u8; MAX_WIRE_BYTES + 1];
    while Instant::now() < deadline && devices.len() < MAX_DEVICES {
        match socket.recv_from(&mut buffer) {
            Ok((length, source)) => {
                if let Some(device) = parse_discovery(&buffer[..length], source, &nonce) {
                    if !devices
                        .iter()
                        .any(|known: &WifiSetupDevice| known.device_id == device.device_id)
                    {
                        devices.push(device);
                    }
                }
            }
            Err(error)
                if matches!(
                    error.kind(),
                    std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut
                ) => {}
            Err(_) => break,
        }
    }
    Ok(devices)
}

fn pairing_path(app: &AppHandle, device_id: &str) -> std::io::Result<std::path::PathBuf> {
    if !valid_device_id(device_id) {
        return Err(std::io::Error::other("invalid device identity"));
    }
    Ok(app
        .path()
        .app_config_dir()
        .map_err(|_| std::io::Error::other("app config directory unavailable"))?
        .join("wifi-pairings")
        .join(format!("{device_id}.token")))
}

fn read_pairing(app: &AppHandle, device_id: &str) -> Option<String> {
    let token = std::fs::read_to_string(pairing_path(app, device_id).ok()?).ok()?;
    valid_client_token(&token).then_some(token)
}

fn authenticated_identity(bytes: &[u8], target: &WifiSetupDevice, expected_nonce: &str) -> bool {
    if bytes.is_empty() || bytes.len() > MAX_WIRE_BYTES {
        return false;
    }
    let Ok(response) = serde_json::from_slice::<DiscoveryResponse>(bytes) else {
        return false;
    };
    response.kind == "echodesk_device"
        && response.version == PROTOCOL_VERSION
        && response.nonce == expected_nonce
        && response.device_id == target.device_id
        && response.name == "EchoDesk"
        && response.model == EXPECTED_MODEL
        && response.firmware_version == target.firmware_version
        && response.setup_state == target.setup_state
        && !response.setup_mode
        && response.paired
        && response.authenticated
        && response.ip == target.address
        && response.port == PROVISION_PORT
}

fn authenticate_paired_device(app: &AppHandle, target: &WifiSetupDevice) -> bool {
    if target.setup_mode || !target.paired || target.port != PROVISION_PORT {
        return false;
    }
    let Some(token) = read_pairing(app, &target.device_id) else {
        return false;
    };
    let Ok(nonce) = random_hex(12) else {
        return false;
    };
    let Ok(ip) = target.address.parse::<Ipv4Addr>() else {
        return false;
    };
    if !allowed_address(ip) {
        return false;
    }
    let Ok(mut stream) = TcpStream::connect_timeout(
        &SocketAddr::from((ip, PROVISION_PORT)),
        Duration::from_millis(1500),
    ) else {
        return false;
    };
    stream.set_read_timeout(Some(Duration::from_secs(2))).ok();
    stream.set_write_timeout(Some(Duration::from_secs(2))).ok();
    let Ok(mut payload) = serde_json::to_vec(&IdentifyMessage {
        kind: "echodesk_identify",
        version: PROTOCOL_VERSION,
        client_token: &token,
        nonce: &nonce,
    }) else {
        return false;
    };
    payload.push(b'\n');
    if payload.len() > MAX_WIRE_BYTES || stream.write_all(&payload).is_err() {
        return false;
    }
    let mut response = Vec::with_capacity(384);
    let mut byte = [0u8; 1];
    loop {
        match stream.read(&mut byte) {
            Ok(0) => break,
            Ok(_) if byte[0] == b'\n' => break,
            Ok(_) if response.len() < MAX_WIRE_BYTES => response.push(byte[0]),
            _ => return false,
        }
    }
    while response.last() == Some(&b'\r') {
        response.pop();
    }
    authenticated_identity(&response, target, &nonce)
}

pub fn discover(app: &AppHandle, _request: DiscoverRequest) -> Result<(), WifiSetupError> {
    {
        let state = app.state::<AppState>();
        let mut wifi = state.wifi_setup.lock().unwrap();
        wifi.operation_id = wifi.operation_id.wrapping_add(1);
        wifi.status = "scanning".into();
        wifi.message = "Looking for EchoDesk devices on this network…".into();
        wifi.error_code = None;
        wifi.devices.clear();
        wifi.selected_device_id = None;
        wifi.provisioned = false;
    }
    bump(app);
    match discover_devices() {
        Ok(devices) => {
            let devices: Vec<_> = devices
                .into_iter()
                .filter(|device| device.setup_mode || authenticate_paired_device(app, device))
                .collect();
            let state = app.state::<AppState>();
            let mut wifi = state.wifi_setup.lock().unwrap();
            wifi.status = if devices.is_empty() {
                "notFound"
            } else {
                "found"
            }
            .into();
            wifi.message = if devices.is_empty() {
                "No EchoDesk was found. For setup, join the EchoDesk-XXXX Wi-Fi network in Windows, then scan again."
            } else {
                "Choose an EchoDesk to continue."
            }
            .into();
            wifi.devices = devices;
            bump(app);
            Ok(())
        }
        Err(failure) => {
            let state = app.state::<AppState>();
            let mut wifi = state.wifi_setup.lock().unwrap();
            wifi.status = "error".into();
            wifi.message = failure.message.into();
            wifi.error_code = Some(failure.code.into());
            bump(app);
            Err(failure)
        }
    }
}

fn save_pairing(app: &AppHandle, device_id: &str, token: &str) -> std::io::Result<()> {
    if !valid_client_token(token) {
        return Err(std::io::Error::other("invalid pairing token"));
    }
    let path = pairing_path(app, device_id)?;
    let root = path
        .parent()
        .ok_or_else(|| std::io::Error::other("pairing directory unavailable"))?;
    std::fs::create_dir_all(&root)?;
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .open(path)?;
    file.write_all(token.as_bytes())?;
    file.sync_all()
}

fn run_provision(
    target: &WifiSetupDevice,
    request: &ProvisionRequest,
    token: &str,
) -> Result<(), WifiSetupError> {
    let ip: Ipv4Addr = target.address.parse().map_err(|_| {
        WifiSetupError::new(
            "invalid_device",
            "The discovered device address is invalid.",
        )
    })?;
    // Provisioning credentials are only ever sent over the WPA2-protected,
    // fixed-address setup AP. A spoofed LAN discovery reply can therefore not
    // redirect the user's Wi-Fi password to an arbitrary private host.
    if ip != Ipv4Addr::new(192, 168, 4, 1) || target.port != PROVISION_PORT {
        return Err(WifiSetupError::new(
            "invalid_device",
            "The discovered device is not a supported local EchoDesk endpoint.",
        ));
    }
    let address = SocketAddr::from((ip, target.port));
    let mut stream =
        TcpStream::connect_timeout(&address, Duration::from_secs(3)).map_err(|_| {
            WifiSetupError::new(
                "device_unreachable",
                "EchoDesk could not be reached. Join its setup Wi-Fi network and try again.",
            )
        })?;
    stream.set_read_timeout(Some(IO_TIMEOUT)).ok();
    stream.set_write_timeout(Some(IO_TIMEOUT)).ok();
    let mut payload = serde_json::to_vec(&ProvisionMessage {
        kind: "echodesk_provision",
        version: PROTOCOL_VERSION,
        pairing_code: &request.setup_code,
        ssid: &request.ssid,
        password: &request.password,
        client_token: token,
    })
    .map_err(|_| WifiSetupError::new("internal", "Could not create the setup request."))?;
    payload.push(b'\n');
    if payload.len() > MAX_WIRE_BYTES {
        return Err(WifiSetupError::new(
            "invalid_request",
            "The Wi-Fi setup request is too large.",
        ));
    }
    stream.write_all(&payload).map_err(|_| {
        WifiSetupError::new(
            "device_unreachable",
            "EchoDesk did not accept the setup request.",
        )
    })?;

    let mut response_bytes = Vec::with_capacity(256);
    let mut byte = [0u8; 1];
    loop {
        match stream.read(&mut byte) {
            Ok(0) => break,
            Ok(_) if byte[0] == b'\n' => break,
            Ok(_) if response_bytes.len() < MAX_WIRE_BYTES => response_bytes.push(byte[0]),
            Ok(_) => {
                return Err(WifiSetupError::new(
                    "invalid_device_response",
                    "EchoDesk returned an invalid setup response.",
                ))
            }
            Err(_) => {
                return Err(WifiSetupError::new(
                    "setup_timeout",
                    "EchoDesk did not finish setup in time.",
                ))
            }
        }
    }
    while response_bytes.last().is_some_and(|byte| *byte == b'\r') {
        response_bytes.pop();
    }
    let response: ProvisionResponse = serde_json::from_slice(&response_bytes).map_err(|_| {
        WifiSetupError::new(
            "invalid_device_response",
            "EchoDesk returned an invalid setup response.",
        )
    })?;
    let device_matches = response.device_id.as_deref() == Some(request.device_id.as_str());
    if response.kind != "echodesk_provision_result"
        || response.version != PROTOCOL_VERSION
        || (response.ok && (!device_matches || !response.restarting))
    {
        return Err(WifiSetupError::new(
            "invalid_device_response",
            "EchoDesk returned an invalid setup response.",
        ));
    }
    if !response.ok {
        let _ = (response.error_code, response.message);
        return Err(WifiSetupError::new(
            "setup_rejected",
            "EchoDesk rejected setup. Check the 8-digit code and Wi-Fi password.",
        ));
    }
    Ok(())
}

pub fn provision(app: &AppHandle, request: ProvisionRequest) -> Result<(), WifiSetupError> {
    validate_credentials(&request)?;
    let token = random_hex(32)?;
    let (target, operation_id) = {
        let state = app.state::<AppState>();
        let mut wifi = state.wifi_setup.lock().unwrap();
        let target = wifi
            .devices
            .iter()
            .find(|device| device.device_id == request.device_id)
            .filter(|device| {
                device.setup_mode
                    && !device.paired
                    && device.discovered_at.elapsed() <= DISCOVERY_TTL
            })
            .cloned()
            .ok_or_else(|| {
                WifiSetupError::new(
                    "device_not_discovered",
                    "Scan again and choose an EchoDesk currently in Wi-Fi setup mode.",
                )
            })?;
        wifi.operation_id = wifi.operation_id.wrapping_add(1);
        let operation_id = wifi.operation_id;
        wifi.status = "provisioning".into();
        wifi.message = "Sending Wi-Fi settings securely to EchoDesk…".into();
        wifi.error_code = None;
        wifi.selected_device_id = Some(request.device_id.clone());
        wifi.provisioned = false;
        (target, operation_id)
    };
    let worker_app = app.clone();
    let spawn_result = std::thread::Builder::new()
        .name("echodesk-wifi-provision".into())
        .spawn(move || {
            let outcome = run_provision(&target, &request, &token);
            let cancelled = {
                let state = worker_app.state::<AppState>();
                let current_operation = state.wifi_setup.lock().unwrap().operation_id;
                current_operation != operation_id
            };
            if cancelled {
                return;
            }
            let pairing_saved = outcome
                .as_ref()
                .is_ok_and(|_| save_pairing(&worker_app, &request.device_id, &token).is_ok());
            let state = worker_app.state::<AppState>();
            let mut wifi = state.wifi_setup.lock().unwrap();
            match outcome {
                Ok(()) if pairing_saved => {
                    wifi.status = "provisioned".into();
                    wifi.message = "EchoDesk saved the network and is restarting. Rejoin your normal Wi-Fi, then find Wi-Fi devices again.".into();
                    wifi.error_code = None;
                    wifi.provisioned = true;
                    wifi.connected_ssid = Some(request.ssid);
                    wifi.address = Some(target.address);
                }
                Ok(()) => {
                    wifi.status = "error".into();
                    wifi.message = "EchoDesk joined Wi-Fi, but its pairing credential could not be saved on this PC.".into();
                    wifi.error_code = Some("pairing_store_failed".into());
                }
                Err(failure) => {
                    wifi.status = "error".into();
                    wifi.message = failure.message.into();
                    wifi.error_code = Some(failure.code.into());
                }
            }
            drop(wifi);
            bump(&worker_app);
        });
    if spawn_result.is_err() {
        let state = app.state::<AppState>();
        let mut wifi = state.wifi_setup.lock().unwrap();
        wifi.operation_id = wifi.operation_id.wrapping_add(1);
        wifi.status = "error".into();
        wifi.message = "Could not start Wi-Fi setup.".into();
        wifi.error_code = Some("internal".into());
        drop(wifi);
        bump(app);
        return Err(WifiSetupError::new(
            "internal",
            "Could not start Wi-Fi setup.",
        ));
    }
    bump(app);
    Ok(())
}

pub fn cancel(app: &AppHandle) {
    let state = app.state::<AppState>();
    let mut wifi = state.wifi_setup.lock().unwrap();
    wifi.operation_id = wifi.operation_id.wrapping_add(1);
    wifi.status = "cancelled".into();
    wifi.message = "Wi-Fi setup was cancelled. USB remains available.".into();
    wifi.error_code = None;
    wifi.selected_device_id = None;
    drop(wifi);
    bump(app);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn discovery_json(nonce: &str, ip: &str) -> Vec<u8> {
        serde_json::to_vec(&serde_json::json!({
            "type": "echodesk_device", "version": 1, "nonce": nonce,
            "deviceId": "aabbccddeeff", "name": "EchoDesk",
            "model": EXPECTED_MODEL, "firmwareVersion": "2.31.0",
            "setupState": "wifiSetup", "paired": false, "setupMode": true,
            "ip": ip, "port": PROVISION_PORT
        }))
        .unwrap()
    }

    #[test]
    fn credentials_are_strict_and_bounded() {
        let valid = ProvisionRequest {
            device_id: "aabbccddeeff".into(),
            setup_code: "12345678".into(),
            ssid: "Home".into(),
            password: "correct horse".into(),
        };
        assert!(validate_credentials(&valid).is_ok());
        let mut bad = valid.clone();
        bad.setup_code = "1234abcd".into();
        assert_eq!(
            validate_credentials(&bad).unwrap_err().code,
            "invalid_setup_code"
        );
        let mut bad = valid.clone();
        bad.ssid = "bad\nssid".into();
        assert_eq!(
            validate_credentials(&bad).unwrap_err().code,
            "invalid_wifi_name"
        );
        let mut bad = valid;
        bad.password = "short".into();
        assert_eq!(
            validate_credentials(&bad).unwrap_err().code,
            "invalid_wifi_password"
        );
    }

    #[test]
    fn discovery_requires_matching_nonce_source_model_and_local_address() {
        let source: SocketAddr = "192.168.4.1:39320".parse().unwrap();
        let valid = discovery_json("abc123", "192.168.4.1");
        let parsed = parse_discovery(&valid, source, "abc123").unwrap();
        assert_eq!(parsed.setup_ssid.as_deref(), Some("EchoDesk-EEFF"));
        assert!(parse_discovery(&valid, source, "wrong").is_none());
        assert!(parse_discovery(&valid, "192.168.4.2:39320".parse().unwrap(), "abc123").is_none());
        assert!(parse_discovery(
            &discovery_json("abc123", "8.8.8.8"),
            "8.8.8.8:39320".parse().unwrap(),
            "abc123"
        )
        .is_none());
        assert!(parse_discovery(
            &discovery_json("abc123", "10.0.0.2"),
            "10.0.0.2:39320".parse().unwrap(),
            "abc123"
        )
        .is_none());
    }

    #[test]
    fn discovery_rejects_extra_fields_and_oversize_packets() {
        let mut value: serde_json::Value =
            serde_json::from_slice(&discovery_json("n", "192.168.4.1")).unwrap();
        value["unexpected"] = serde_json::json!(true);
        let bytes = serde_json::to_vec(&value).unwrap();
        assert!(parse_discovery(&bytes, "192.168.4.1:39320".parse().unwrap(), "n").is_none());
        assert!(parse_discovery(
            &vec![b'x'; MAX_WIRE_BYTES + 1],
            "10.0.0.2:39320".parse().unwrap(),
            "n"
        )
        .is_none());
    }

    #[test]
    fn provisioning_error_matches_firmware_code_field() {
        let response: ProvisionResponse = serde_json::from_value(serde_json::json!({
            "type": "echodesk_provision_result",
            "version": 1,
            "ok": false,
            "code": "pairing_failed",
            "message": "pairing code is incorrect"
        }))
        .unwrap();
        assert_eq!(response.error_code.as_deref(), Some("pairing_failed"));
        assert!(!response.restarting);
    }

    #[test]
    fn lan_rediscovery_requires_authenticated_matching_identity() {
        let target = WifiSetupDevice {
            device_id: "aabbccddeeff".into(),
            display_name: "EchoDesk".into(),
            model: EXPECTED_MODEL.into(),
            setup_ssid: None,
            address: "10.0.0.24".into(),
            port: PROVISION_PORT,
            setup_mode: false,
            paired: true,
            firmware_version: "2.31.0".into(),
            setup_state: "completed".into(),
            discovered_at: Instant::now(),
        };
        let mut response = serde_json::json!({
            "type": "echodesk_device", "version": 1, "nonce": "fresh-nonce",
            "deviceId": "aabbccddeeff", "name": "EchoDesk",
            "model": EXPECTED_MODEL, "firmwareVersion": "2.31.0",
            "setupState": "completed", "paired": true, "setupMode": false,
            "authenticated": true, "ip": "10.0.0.24", "port": PROVISION_PORT
        });
        assert!(authenticated_identity(
            &serde_json::to_vec(&response).unwrap(),
            &target,
            "fresh-nonce"
        ));
        response["authenticated"] = serde_json::json!(false);
        assert!(!authenticated_identity(
            &serde_json::to_vec(&response).unwrap(),
            &target,
            "fresh-nonce"
        ));
        response["authenticated"] = serde_json::json!(true);
        response["ip"] = serde_json::json!("10.0.0.25");
        assert!(!authenticated_identity(
            &serde_json::to_vec(&response).unwrap(),
            &target,
            "fresh-nonce"
        ));
    }
}
