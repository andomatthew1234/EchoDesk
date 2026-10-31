//! Local named-pipe API for EchoDesk Desktop.
//!
//! EchoDesk Host remains the only serial-port owner. The WPF application talks
//! only to this allowlisted, bounded IPC surface and can therefore coexist
//! with Codex, Performance and music telemetry without competing for COM.

use crate::firmware_manager;
use crate::poll;
use crate::serial_service::{self, ConnectionState, Job};
use crate::state::{current_snapshot, AppState};
use aimonitor_core::Status;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::sync::atomic::Ordering;
use std::sync::mpsc;
use std::time::Duration;
use tauri::{AppHandle, Manager};

pub const PIPE_NAME: &str = r"\\.\pipe\EchoDesk.Companion.v1";
pub const PROTOCOL_VERSION: u8 = 1;
pub const MAX_LINE_BYTES: usize = 65_536;
const REQUEST_TIMEOUT: Duration = Duration::from_secs(4);

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct BrokerRequest {
    version: u8,
    id: String,
    method: String,
    #[serde(default)]
    params: Value,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct SetAiUsageEnabledParams {
    enabled: bool,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct SetUpdateNotificationsParams {
    enabled: bool,
}

#[derive(Debug, Serialize)]
struct BrokerError {
    code: String,
    message: String,
}

fn error(code: &str, message: impl Into<String>) -> BrokerError {
    BrokerError {
        code: code.into(),
        message: message.into(),
    }
}

fn firmware_error(internal: &str) -> BrokerError {
    let (code, message) = firmware_manager::public_error(internal);
    error(code, message)
}

pub fn status(app: &AppHandle) -> Value {
    let state = app.state::<AppState>();
    let connection = state.connection.lock().unwrap().clone();
    let device_state = state.echo_desk_state.lock().unwrap().clone();
    let connection_name = match connection.state {
        ConnectionState::Disconnected => "disconnected",
        ConnectionState::Probing => "probing",
        ConnectionState::Connected => "connected",
        ConnectionState::ForeignFirmware => "foreignFirmware",
    };
    let firmware = connection.info.as_ref().map(|info| info.version.clone());
    let capabilities = connection.info.as_ref().map(|info| {
        json!({
            "echoDeskControl": info.supports_echo_desk_control(),
            "performance": info.supports_performance(),
            "music": info.supports_music(),
            "musicBackground": info.supports_music_background(),
            "musicVisualizer": info.supports_music_visualizer(),
        })
    });
    let enabled = state.settings.lock().unwrap().ai_usage_enabled;
    let usage = current_snapshot(app);
    let (component_status, component_error) = if !enabled {
        ("disabled", None)
    } else if usage.fetching {
        ("checking", None)
    } else {
        match &usage.status {
            Status::Ok => ("connected", None),
            Status::NotYet => ("waiting", None),
            Status::CliMissing => (
                "dependencyMissing",
                Some("The bundled Codex usage provider is unavailable."),
            ),
            Status::AuthenticationRequired => (
                "authenticationRequired",
                Some("Sign in to Codex in EchoDesk Desktop."),
            ),
            Status::NetworkUnavailable => (
                "unavailable",
                Some("Codex usage is unavailable while the network is offline."),
            ),
            Status::RateLimited => (
                "unavailable",
                Some("Codex usage is temporarily rate limited."),
            ),
            Status::Stale { .. } => ("stale", Some("Codex usage data is out of date.")),
            Status::ProviderUnavailable { .. } => (
                "unavailable",
                Some("Codex usage is temporarily unavailable."),
            ),
            Status::CliFailed { .. } => (
                "error",
                Some("The Codex usage provider could not be reached."),
            ),
            Status::ParseError { .. } => {
                ("error", Some("EchoDesk could not read Codex usage data."))
            }
        }
    };
    json!({
        "brokerAvailable": true,
        "brokerRevision": state.desktop_broker_revision.load(Ordering::Acquire),
        "connection": connection_name,
        "port": connection.port,
        "firmware": firmware,
        "capabilities": capabilities,
        "deviceState": device_state,
        "components": {
            "aiUsage": {
                "enabled": enabled,
                "status": component_status,
                "lastSuccessfulUpdate": usage.last_successful_at,
                "error": component_error,
                "provider": usage.provider,
                "fetching": enabled && usage.fetching,
                "source": usage.source,
                "cliVersion": usage.cli_version,
            }
        },
        "firmwareManager": firmware_manager::snapshot(app),
    })
}

fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && id
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
}

fn request_device(app: &AppHandle, method: &str, params: Value) -> Result<Value, BrokerError> {
    let (reply, receive) = mpsc::channel();
    serial_service::send(
        app,
        Job::EchoDeskControl {
            method: method.to_string(),
            params,
            reply,
        },
    );
    match receive.recv_timeout(REQUEST_TIMEOUT) {
        Ok(Ok(value)) => Ok(value),
        Ok(Err(value)) => Err(error(
            value
                .get("code")
                .and_then(Value::as_str)
                .unwrap_or("device_error"),
            value
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("EchoDesk rejected the command"),
        )),
        Err(_) => Err(error(
            "timeout",
            "EchoDesk did not confirm the command in time",
        )),
    }
}

fn dispatch(app: &AppHandle, request: &BrokerRequest) -> Result<Value, BrokerError> {
    if request.version != PROTOCOL_VERSION {
        return Err(error(
            "unsupported_version",
            "Only broker protocol v1 is supported",
        ));
    }
    if !valid_id(&request.id) {
        return Err(error("invalid_request", "Request id is missing or invalid"));
    }
    match request.method.as_str() {
        "hello" => Ok(status(app)),
        "get_state" => {
            request_device(app, "get_state", json!({}))?;
            Ok(status(app))
        }
        "open_app" | "set_settings" | "restart" => {
            request_device(app, &request.method, request.params.clone())?;
            Ok(status(app))
        }
        "reconnect" => {
            serial_service::send(app, Job::EchoDeskReconnect);
            Ok(status(app))
        }
        "set_ai_usage_enabled" => {
            let params: SetAiUsageEnabledParams = serde_json::from_value(request.params.clone())
                .map_err(|_| {
                    error(
                        "invalid_request",
                        "set_ai_usage_enabled requires exactly {enabled: boolean}",
                    )
                })?;
            let changed = {
                let state = app.state::<AppState>();
                let mut settings = state.settings.lock().unwrap();
                let changed = settings.ai_usage_enabled != params.enabled;
                if changed {
                    settings.ai_usage_enabled = params.enabled;
                    settings.save(app);
                }
                changed
            };
            if changed {
                poll::ai_usage_enabled_changed(app, params.enabled);
            }
            Ok(status(app))
        }
        "firmware_scan" => {
            firmware_manager::scan(app);
            Ok(status(app))
        }
        "firmware_check_updates" => {
            firmware_manager::check_updates(app).map_err(|message| firmware_error(&message))?;
            Ok(status(app))
        }
        "firmware_select_device" => {
            let params: firmware_manager::SelectDeviceRequest =
                serde_json::from_value(request.params.clone()).map_err(|_| {
                    error(
                        "invalid_request",
                        "firmware_select_device requires port, hardwareModel and boardConfirmed",
                    )
                })?;
            firmware_manager::select_device(app, params)
                .map_err(|message| firmware_error(&message))?;
            Ok(status(app))
        }
        "firmware_install" => {
            let params: firmware_manager::InstallRequest =
                serde_json::from_value(request.params.clone()).map_err(|_| {
                    error(
                        "invalid_request",
                        "firmware_install accepts only install, update, reinstall or repair mode",
                    )
                })?;
            firmware_manager::install(app, params).map_err(|message| firmware_error(&message))?;
            Ok(status(app))
        }
        "firmware_cancel" => {
            firmware_manager::cancel(app).map_err(|message| firmware_error(&message))?;
            Ok(status(app))
        }
        "set_update_notifications" => {
            let params: SetUpdateNotificationsParams =
                serde_json::from_value(request.params.clone()).map_err(|_| {
                    error(
                        "invalid_request",
                        "set_update_notifications requires exactly {enabled: boolean}",
                    )
                })?;
            firmware_manager::set_update_notifications(app, params.enabled);
            Ok(status(app))
        }
        "shutdown" => {
            if !(request.params.is_null()
                || request
                    .params
                    .as_object()
                    .is_some_and(|params| params.is_empty()))
            {
                return Err(error("invalid_request", "shutdown takes no parameters"));
            }
            let result = status(app);
            serial_service::shutdown_and_exit(app);
            Ok(result)
        }
        _ => Err(error(
            "unknown_method",
            "The requested broker method is not supported",
        )),
    }
}

fn response(app: &AppHandle, line: &[u8]) -> Vec<u8> {
    let parsed = if line.len() > MAX_LINE_BYTES {
        Err(error(
            "request_too_large",
            "IPC request exceeds 65536 bytes",
        ))
    } else {
        serde_json::from_slice::<BrokerRequest>(line)
            .map_err(|_| error("invalid_json", "IPC request is not valid v1 JSON"))
    };
    let value = match parsed {
        Ok(request) => match dispatch(app, &request) {
            Ok(result) => json!({
                "version": PROTOCOL_VERSION,
                "id": request.id,
                "ok": true,
                "result": result,
            }),
            Err(failure) => json!({
                "version": PROTOCOL_VERSION,
                "id": request.id,
                "ok": false,
                "error": failure,
            }),
        },
        Err(failure) => json!({
            "version": PROTOCOL_VERSION,
            "id": Value::Null,
            "ok": false,
            "error": failure,
        }),
    };
    let mut bytes = serde_json::to_vec(&value).unwrap_or_else(|_| {
        br#"{"version":1,"id":null,"ok":false,"error":{"code":"internal","message":"Serialization failed"}}"#.to_vec()
    });
    bytes.push(b'\n');
    bytes
}

fn event(app: &AppHandle) -> Vec<u8> {
    let mut bytes = serde_json::to_vec(&json!({
        "version": PROTOCOL_VERSION,
        "event": "state",
        "data": status(app),
    }))
    .unwrap_or_default();
    bytes.push(b'\n');
    bytes
}

pub fn start(app: AppHandle) {
    #[cfg(windows)]
    std::thread::Builder::new()
        .name("echodesk-desktop-broker".into())
        .spawn(move || windows_pipe::run(app))
        .expect("EchoDesk Desktop named-pipe broker");

    #[cfg(not(windows))]
    let _ = app;
}

#[cfg(windows)]
mod windows_pipe {
    use super::*;
    use std::thread;
    use windows::core::{s, HRESULT};
    use windows::Win32::Foundation::{
        CloseHandle, LocalFree, ERROR_PIPE_CONNECTED, HANDLE, HLOCAL,
    };
    use windows::Win32::Security::Authorization::{
        ConvertStringSecurityDescriptorToSecurityDescriptorA, SDDL_REVISION_1,
    };
    use windows::Win32::Security::{PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES};
    use windows::Win32::Storage::FileSystem::{
        FlushFileBuffers, ReadFile, WriteFile, FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_ACCESS_DUPLEX,
    };
    use windows::Win32::System::Pipes::{
        ConnectNamedPipe, CreateNamedPipeA, DisconnectNamedPipe, PeekNamedPipe, PIPE_READMODE_BYTE,
        PIPE_REJECT_REMOTE_CLIENTS, PIPE_TYPE_BYTE, PIPE_WAIT,
    };

    struct OwnedPipe(HANDLE);

    struct OwnedSecurityDescriptor(PSECURITY_DESCRIPTOR);

    impl Drop for OwnedPipe {
        fn drop(&mut self) {
            unsafe {
                let _ = DisconnectNamedPipe(self.0);
                let _ = CloseHandle(self.0);
            }
        }
    }

    impl Drop for OwnedSecurityDescriptor {
        fn drop(&mut self) {
            unsafe {
                let _ = LocalFree(Some(HLOCAL(self.0 .0)));
            }
        }
    }

    fn local_user_security() -> windows::core::Result<(OwnedSecurityDescriptor, SECURITY_ATTRIBUTES)>
    {
        let mut descriptor = PSECURITY_DESCRIPTOR::default();
        unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorA(
                // Protected DACL: creator/current user, LocalSystem and local
                // administrators only. OW grants access to the pipe object's
                // owner, which is the current process user.
                s!("D:P(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)"),
                SDDL_REVISION_1,
                &mut descriptor,
                None,
            )?;
        }
        let attributes = SECURITY_ATTRIBUTES {
            nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: descriptor.0,
            bInheritHandle: false.into(),
        };
        Ok((OwnedSecurityDescriptor(descriptor), attributes))
    }

    fn write_all(pipe: HANDLE, bytes: &[u8]) -> bool {
        let mut offset = 0;
        while offset < bytes.len() {
            let mut written = 0u32;
            let result =
                unsafe { WriteFile(pipe, Some(&bytes[offset..]), Some(&mut written), None) };
            if result.is_err() || written == 0 {
                return false;
            }
            offset += written as usize;
        }
        true
    }

    fn serve(app: &AppHandle, pipe: HANDLE) {
        let mut pending = Vec::new();
        let mut last_revision = 0u64;
        loop {
            let revision = app
                .state::<AppState>()
                .desktop_broker_revision
                .load(Ordering::Acquire);
            if revision != last_revision {
                if !write_all(pipe, &event(app)) {
                    return;
                }
                last_revision = revision;
            }

            let mut available = 0u32;
            if unsafe { PeekNamedPipe(pipe, None, 0, None, Some(&mut available), None) }.is_err() {
                return;
            }
            if available == 0 {
                thread::sleep(Duration::from_millis(50));
                continue;
            }

            let mut chunk = vec![0u8; (available as usize).min(4096)];
            let mut read = 0u32;
            if unsafe { ReadFile(pipe, Some(&mut chunk), Some(&mut read), None) }.is_err()
                || read == 0
            {
                return;
            }
            pending.extend_from_slice(&chunk[..read as usize]);
            if pending.len() > MAX_LINE_BYTES && !pending.contains(&b'\n') {
                let oversized = response(app, &vec![b' '; MAX_LINE_BYTES + 1]);
                let _ = write_all(pipe, &oversized);
                return;
            }
            while let Some(newline) = pending.iter().position(|byte| *byte == b'\n') {
                let mut line: Vec<u8> = pending.drain(..=newline).collect();
                while line
                    .last()
                    .is_some_and(|byte| matches!(*byte, b'\n' | b'\r'))
                {
                    line.pop();
                }
                if line.is_empty() {
                    continue;
                }
                if !write_all(pipe, &response(app, &line)) {
                    return;
                }
            }
        }
    }

    pub fn run(app: AppHandle) {
        loop {
            // FILE_FLAG_FIRST_PIPE_INSTANCE prevents a lower-integrity process
            // from pre-creating the public name. The process-token default
            // An explicit protected DACL limits the pipe to the current user,
            // LocalSystem and administrators. PIPE_REJECT_REMOTE_CLIENTS also
            // excludes network clients entirely.
            let (_descriptor, attributes) = match local_user_security() {
                Ok(security) => security,
                Err(error) => {
                    eprintln!("[desktop-broker] pipe security unavailable: {error}");
                    thread::sleep(Duration::from_secs(2));
                    continue;
                }
            };
            let pipe = unsafe {
                CreateNamedPipeA(
                    s!(r"\\.\pipe\EchoDesk.Companion.v1"),
                    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    1,
                    MAX_LINE_BYTES as u32,
                    MAX_LINE_BYTES as u32,
                    1000,
                    Some(&attributes),
                )
            };
            let pipe = match pipe {
                Ok(pipe) => pipe,
                Err(error) => {
                    eprintln!("[desktop-broker] named pipe unavailable: {error}");
                    thread::sleep(Duration::from_secs(2));
                    continue;
                }
            };
            let pipe = OwnedPipe(pipe);
            let connected = unsafe { ConnectNamedPipe(pipe.0, None) };
            if let Err(failure) = connected {
                if failure.code() != HRESULT::from_win32(ERROR_PIPE_CONNECTED.0) {
                    thread::sleep(Duration::from_millis(250));
                    continue;
                }
            }
            println!("[desktop-broker] EchoDesk Desktop connected");
            serve(&app, pipe.0);
            unsafe {
                let _ = FlushFileBuffers(pipe.0);
            }
            println!("[desktop-broker] EchoDesk Desktop disconnected");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn request_ids_are_short_and_safe() {
        assert!(valid_id("desktop-42"));
        assert!(!valid_id(""));
        assert!(!valid_id("bad id"));
        assert!(!valid_id(&"x".repeat(65)));
    }

    #[test]
    fn envelope_is_strict_and_bounded() {
        let request: BrokerRequest =
            serde_json::from_str(r#"{"version":1,"id":"a","method":"hello","params":{}}"#).unwrap();
        assert_eq!(request.method, "hello");
        assert!(serde_json::from_str::<BrokerRequest>(
            r#"{"version":1,"id":"a","method":"hello","extra":1}"#
        )
        .is_err());
    }

    #[test]
    fn ai_usage_toggle_params_are_strict() {
        let valid: SetAiUsageEnabledParams =
            serde_json::from_value(json!({"enabled": false})).unwrap();
        assert!(!valid.enabled);
        assert!(serde_json::from_value::<SetAiUsageEnabledParams>(json!({})).is_err());
        assert!(serde_json::from_value::<SetAiUsageEnabledParams>(
            json!({"enabled": true, "extra": 1})
        )
        .is_err());
        assert!(
            serde_json::from_value::<SetAiUsageEnabledParams>(json!({"enabled": "true"})).is_err()
        );
    }
}
