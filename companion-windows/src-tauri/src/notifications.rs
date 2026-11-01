//! Public-API Notification Stack adapter.
//!
//! Notification content is held only long enough to forward a bounded frame to
//! EchoDesk OS. Settings persist the opt-in and source exclusions, never titles
//! or bodies. Windows uses `UserNotificationListener`; other Rust targets expose
//! an explicit unsupported state instead of reading private OS databases.

use crate::serial_service::{self, Job};
use crate::state::AppState;
use serde::Serialize;
use serde_json::{json, Value};
use std::collections::{HashSet, VecDeque};
use std::sync::mpsc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tauri::{AppHandle, Manager};

const MAX_SEEN: usize = 256;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NotificationStackState {
    pub enabled: bool,
    pub status: String,
    pub permission: String,
    pub message: Option<String>,
    pub forwarded_count: u64,
    pub available_sources: Vec<String>,
}

impl Default for NotificationStackState {
    fn default() -> Self {
        Self {
            enabled: false,
            status: "notConfigured".into(),
            permission: "notRequested".into(),
            message: Some("Set up notifications to mirror them to EchoDesk.".into()),
            forwarded_count: 0,
            available_sources: Vec::new(),
        }
    }
}

#[derive(Debug, Clone)]
struct CapturedNotification {
    id: u32,
    source: String,
    title: String,
    body: String,
    timestamp: u64,
}

fn bounded(value: &str, maximum: usize) -> String {
    let cleaned: String = value
        .chars()
        .filter(|c| !c.is_control() || *c == ' ')
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ");
    let mut out = String::new();
    for ch in cleaned.chars() {
        if out.len() + ch.len_utf8() > maximum {
            break;
        }
        out.push(ch);
    }
    out
}

fn request_device(app: &AppHandle, method: &str, params: Value) -> Result<Value, String> {
    let (reply, receive) = mpsc::channel();
    serial_service::send(
        app,
        Job::EchoDeskControl {
            method: method.into(),
            params,
            reply,
        },
    );
    match receive.recv_timeout(Duration::from_secs(4)) {
        Ok(Ok(value)) => Ok(value),
        Ok(Err(value)) => Err(value
            .get("message")
            .and_then(Value::as_str)
            .unwrap_or("EchoDesk rejected the notification command")
            .to_string()),
        Err(_) => Err("EchoDesk did not confirm the notification command in time".into()),
    }
}

fn set_state(app: &AppHandle, update: impl FnOnce(&mut NotificationStackState)) {
    let state = app.state::<AppState>();
    let mut health = state.notification_stack.lock().unwrap();
    let before = serde_json::to_string(&*health).unwrap_or_default();
    update(&mut health);
    if serde_json::to_string(&*health).unwrap_or_default() != before {
        state
            .desktop_broker_revision
            .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
    }
}

fn stable_id(source: &str, os_id: u32) -> u32 {
    // FNV-1a over source + OS id prevents collisions between apps that both
    // use a small per-app notification id. Zero is reserved by firmware.
    let mut hash = 2_166_136_261u32;
    for byte in source.as_bytes().iter().copied().chain(os_id.to_le_bytes()) {
        hash ^= byte as u32;
        hash = hash.wrapping_mul(16_777_619);
    }
    if hash == 0 {
        1
    } else {
        hash
    }
}

#[cfg(windows)]
fn request_windows_access(app: &AppHandle) -> Result<String, String> {
    use windows::ApplicationModel::Package;
    use windows::UI::Notifications::Management::{
        UserNotificationListener, UserNotificationListenerAccessStatus,
    };
    // UserNotificationListener is available only to an identity-bearing app
    // whose AppX manifest declares userNotificationListener. Current NSIS
    // builds have no package identity, so fail closed with precise guidance.
    if Package::Current().is_err() {
        return Ok("requiresPackagedBuild".into());
    }
    let (send, receive) = mpsc::channel();
    app.run_on_main_thread(move || {
        let result = (|| {
            let listener = UserNotificationListener::Current()?;
            let access = listener.RequestAccessAsync()?.get()?;
            Ok::<_, windows::core::Error>(
                if access == UserNotificationListenerAccessStatus::Allowed {
                    "allowed"
                } else {
                    "denied"
                },
            )
        })();
        let _ = send.send(
            result
                .map(str::to_string)
                .map_err(|error| error.to_string()),
        );
    })
    .map_err(|error| format!("Could not request notification access on the UI thread: {error}"))?;
    receive
        .recv_timeout(Duration::from_secs(15))
        .map_err(|_| "Windows notification permission request timed out".to_string())?
}

#[cfg(windows)]
fn capture_windows(app: &AppHandle) -> Result<(String, Vec<CapturedNotification>), String> {
    use windows::Win32::System::Com::{CoInitializeEx, CoUninitialize, COINIT_MULTITHREADED};
    use windows::UI::Notifications::Management::UserNotificationListener;
    use windows::UI::Notifications::{KnownNotificationBindings, NotificationKinds};

    struct ComApartment;
    impl Drop for ComApartment {
        fn drop(&mut self) {
            unsafe { CoUninitialize() };
        }
    }

    let permission = request_windows_access(app)?;
    if permission != "allowed" {
        return Ok((permission, Vec::new()));
    }
    unsafe { CoInitializeEx(None, COINIT_MULTITHREADED) }
        .ok()
        .map_err(|error| format!("Windows notification apartment failed: {error}"))?;
    let _apartment = ComApartment;
    let listener = UserNotificationListener::Current()
        .map_err(|error| format!("Windows notification listener unavailable: {error}"))?;
    let notifications = listener
        .GetNotificationsAsync(NotificationKinds::Toast)
        .and_then(|operation| operation.get())
        .map_err(|error| format!("Windows notification history unavailable: {error}"))?;
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let mut out = Vec::new();
    for item in notifications {
        let source = item
            .AppInfo()
            .and_then(|app| app.DisplayInfo())
            .and_then(|display| display.DisplayName())
            .map(|name| name.to_string())
            .unwrap_or_else(|_| "Windows".into());
        let binding = item
            .Notification()
            .and_then(|notification| notification.Visual())
            .and_then(|visual| {
                KnownNotificationBindings::ToastGeneric()
                    .and_then(|known| visual.GetBinding(&known))
            });
        let mut text = Vec::new();
        if let Ok(binding) = binding {
            if let Ok(elements) = binding.GetTextElements() {
                for element in elements {
                    if let Ok(value) = element.Text() {
                        let value = value.to_string();
                        if !value.trim().is_empty() {
                            text.push(value);
                        }
                    }
                }
            }
        }
        let title = text
            .first()
            .cloned()
            .unwrap_or_else(|| "Notification".into());
        let body = text.iter().skip(1).cloned().collect::<Vec<_>>().join(" — ");
        let os_id = item.Id().unwrap_or(0);
        if os_id != 0 {
            out.push(CapturedNotification {
                id: stable_id(&source, os_id),
                source: bounded(&source, 32),
                title: bounded(&title, 64),
                body: bounded(&body, 160),
                timestamp: now,
            });
        }
    }
    Ok(("allowed".into(), out))
}

#[cfg(not(windows))]
fn capture_windows(_app: &AppHandle) -> Result<(String, Vec<CapturedNotification>), String> {
    Ok(("unsupported".into(), Vec::new()))
}

pub fn start(app: AppHandle) {
    std::thread::Builder::new()
        .name("echodesk-notification-stack".into())
        .spawn(move || {
            let mut seen = HashSet::new();
            let mut order = VecDeque::new();
            let mut device_enabled = false;
            loop {
                let (enabled, exclusions) = {
                    let state = app.state::<AppState>();
                    let settings = state.settings.lock().unwrap();
                    (
                        settings.notification_stack_enabled,
                        settings
                            .notification_stack_exclusions
                            .iter()
                            .map(|value| value.to_lowercase())
                            .collect::<HashSet<_>>(),
                    )
                };
                if !enabled {
                    if device_enabled {
                        let _ = request_device(&app, "notifications_set_enabled", json!({"enabled": false}));
                        device_enabled = false;
                    }
                    set_state(&app, |state| {
                        state.enabled = false;
                        state.status = "notConfigured".into();
                        state.permission = "notRequested".into();
                        state.message = Some("Set up notifications to mirror them to EchoDesk.".into());
                    });
                    std::thread::sleep(Duration::from_secs(2));
                    continue;
                }
                let supports = app
                    .state::<AppState>()
                    .connection
                    .lock()
                    .unwrap()
                    .info
                    .as_ref()
                    .is_some_and(|info| info.supports_echo_desk_notifications());
                if !supports {
                    device_enabled = false;
                    set_state(&app, |state| {
                        state.enabled = true;
                        state.status = "waitingForDevice".into();
                        state.message = Some("Connect an EchoDesk with Notification Stack support.".into());
                    });
                    std::thread::sleep(Duration::from_secs(2));
                    continue;
                }
                if !device_enabled {
                    match request_device(&app, "notifications_set_enabled", json!({"enabled": true})) {
                        Ok(_) => device_enabled = true,
                        Err(message) => {
                            set_state(&app, |state| {
                                state.status = "error".into();
                                state.message = Some(message);
                            });
                            std::thread::sleep(Duration::from_secs(3));
                            continue;
                        }
                    }
                }
                match capture_windows(&app) {
                    Ok((permission, notifications)) => {
                        let mut sources = notifications
                            .iter()
                            .map(|item| item.source.clone())
                            .collect::<Vec<_>>();
                        sources.sort_by_key(|value| value.to_lowercase());
                        sources.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
                        set_state(&app, |state| {
                            state.enabled = true;
                            state.permission = permission.clone();
                            state.available_sources = sources;
                            state.status = match permission.as_str() {
                                "allowed" => "connected",
                                "requiresPackagedBuild" => "unsupportedPackaging",
                                _ => "permissionDenied",
                            }.into();
                            state.message = if permission == "allowed" {
                                Some("Notifications are mirrored while EchoDesk Desktop is running.".into())
                            } else if permission == "requiresPackagedBuild" {
                                Some("This Windows installer has no package identity, so Windows cannot grant all-app notification access. The USB test relay remains available.".into())
                            } else {
                                Some("Allow notification access in Windows Settings, then try setup again.".into())
                            };
                        });
                        if permission == "allowed" {
                            for item in notifications {
                                if seen.contains(&item.id)
                                    || exclusions.contains(&item.source.to_lowercase())
                                {
                                    continue;
                                }
                                if request_device(
                                    &app,
                                    "notifications_push",
                                    json!({
                                        "id": item.id,
                                        "source": item.source,
                                        "title": item.title,
                                        "body": item.body,
                                        "timestamp": item.timestamp,
                                    }),
                                )
                                .is_ok()
                                {
                                    seen.insert(item.id);
                                    order.push_back(item.id);
                                    while order.len() > MAX_SEEN {
                                        if let Some(old) = order.pop_front() { seen.remove(&old); }
                                    }
                                    set_state(&app, |state| state.forwarded_count += 1);
                                }
                            }
                        }
                    }
                    Err(message) => set_state(&app, |state| {
                        state.enabled = true;
                        state.status = "unavailable".into();
                        state.permission = "error".into();
                        state.message = Some(message);
                    }),
                }
                std::thread::sleep(Duration::from_secs(3));
            }
        })
        .expect("Notification Stack worker");
}
