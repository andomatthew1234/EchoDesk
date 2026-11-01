//! Geteilter Zustand der App. Die Mutexe werden nur kurz gehalten, nie über
//! den blockierenden CLI-Aufruf oder einen seriellen Zugriff hinweg (siehe
//! `poll` und `serial_service`).

use crate::audio_visualizer::VisualizerFeed;
use crate::claude_code::ClaudeCode;
use crate::firmware_manager::FirmwareManagerState;
use crate::flash::FlashRecord;
use crate::music::MusicFeed;
use crate::notifications::NotificationStackState;
use crate::performance::PerformanceFeed;
use crate::plugins::PluginStore;
use crate::serial_service::{ConnectionSnapshot, Job};
use crate::settings::Settings;
use crate::updates::ReleaseCache;
use crate::wifi_setup::WifiSetupState;
use aimonitor_core::Provider;
use aimonitor_core::{DeviceRegistry, Snapshot, Source};
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicU64};
use std::sync::mpsc::Sender;
use std::sync::{Condvar, Mutex};
use tauri::{tray::TrayIcon, AppHandle, Manager};

pub struct AppState {
    pub source: Mutex<Source>,
    pub view_sources: Mutex<HashMap<Provider, Snapshot>>,
    /// Dauerhafte Sources der Zusatzfenster, damit nicht jeder Abruf die CLI neu sucht.
    pub view_clients: Mutex<HashMap<Provider, Source>>,
    pub view_fetching: AtomicBool,
    pub view_refresh_pending: AtomicBool,
    /// Invalidates provider workers when AI Usage is disabled or re-enabled.
    /// Results are only accepted when their captured generation still matches.
    pub ai_usage_generation: AtomicU64,
    pub settings: Mutex<Settings>,
    pub tray: Mutex<Option<TrayIcon>>,
    pub registry: Mutex<DeviceRegistry>,
    pub plugins: Mutex<PluginStore>,
    /// Fenster „Claude Code wartet“: Sessions, Empfang, Token.
    pub claude_code: Mutex<ClaudeCode>,
    /// Zuletzt veröffentlichter Verbindungszustand, geschrieben vom Serial-Thread.
    pub connection: Mutex<ConnectionSnapshot>,
    /// Aufträge an den Serial-Thread.
    pub serial: Sender<Job>,
    /// Latest-value mailbox for the hardware helper. It cannot access COM.
    pub performance: PerformanceFeed,
    /// Latest GSMTC snapshot and bounded control queue. It never owns COM.
    pub music: MusicFeed,
    /// Opt-in WASAPI band snapshots. Raw PCM never leaves the worker.
    pub audio_visualizer: VisualizerFeed,
    /// Last validated EchoDesk control state reported by firmware. The local
    /// desktop broker reads this cache; it never opens the serial port.
    pub echo_desk_state: Mutex<Option<serde_json::Value>>,
    /// Latest validated clock utility state and the latest unconsumed
    /// device-originated configuration request.
    pub echo_desk_clock_state: Mutex<Option<serde_json::Value>>,
    pub echo_desk_clock_config_request: Mutex<Option<String>>,
    /// Increments for every physical Clock configuration gesture, including
    /// repeated requests for the same mode.
    pub echo_desk_clock_config_sequence: AtomicU64,
    /// Permission/capture health for Notification Stack. Notification bodies
    /// are never persisted here or to settings.json.
    pub notification_stack: Mutex<NotificationStackState>,
    pub echo_desk_notification_state: Mutex<Option<serde_json::Value>>,
    /// Validated PC Dashboard metric selection. Samples are intentionally not persisted.
    pub echo_desk_pc_dashboard_state: Mutex<Option<serde_json::Value>>,
    /// Monotonic status/event revision for stale-update protection in the WPF
    /// client. Device settings additionally carry their firmware revision.
    pub desktop_broker_revision: AtomicU64,
    /// Zuletzt geladene GitHub-Releases mit Zeitstempel (Phase 3).
    pub releases: Mutex<ReleaseCache>,
    /// `true`, solange eine Release-Abfrage läuft; parallele Aufrufe warten
    /// über die Condvar auf das Ergebnis, statt selbst zu laden.
    pub release_check: (Mutex<bool>, Condvar),
    /// Ein Flash-Vorgang läuft; ein zweiter wird abgewiesen.
    pub flashing: AtomicBool,
    /// Letzter Flash-Versuch, für den Panel-Abgleich nach dem Neustart.
    pub last_flash: Mutex<Option<FlashRecord>>,
    /// Ein App-Update wird gerade geladen oder installiert.
    pub installing: AtomicBool,
    /// Consumer-facing install/update/recovery workflow. It contains no raw
    /// flash command or caller-controlled file path.
    pub firmware_manager: Mutex<FirmwareManagerState>,
    /// Optional Wi-Fi onboarding. It never replaces or owns the USB serial
    /// transport and only targets devices returned by bounded discovery.
    pub wifi_setup: Mutex<WifiSetupState>,
}

impl AppState {
    pub fn new(
        source: Source,
        settings: Settings,
        registry: DeviceRegistry,
        plugins: PluginStore,
        serial: Sender<Job>,
    ) -> Self {
        Self {
            source: Mutex::new(source),
            view_sources: Mutex::new(HashMap::new()),
            view_clients: Mutex::new(HashMap::new()),
            view_fetching: AtomicBool::new(false),
            view_refresh_pending: AtomicBool::new(false),
            ai_usage_generation: AtomicU64::new(1),
            settings: Mutex::new(settings),
            tray: Mutex::new(None),
            registry: Mutex::new(registry),
            plugins: Mutex::new(plugins),
            claude_code: Mutex::new(ClaudeCode::default()),
            connection: Mutex::new(ConnectionSnapshot::default()),
            serial,
            performance: PerformanceFeed::default(),
            music: MusicFeed::default(),
            audio_visualizer: VisualizerFeed::default(),
            echo_desk_state: Mutex::new(None),
            echo_desk_clock_state: Mutex::new(None),
            echo_desk_clock_config_request: Mutex::new(None),
            echo_desk_clock_config_sequence: AtomicU64::new(0),
            notification_stack: Mutex::new(NotificationStackState::default()),
            echo_desk_notification_state: Mutex::new(None),
            echo_desk_pc_dashboard_state: Mutex::new(None),
            desktop_broker_revision: AtomicU64::new(1),
            releases: Mutex::new(ReleaseCache::default()),
            release_check: (Mutex::new(false), Condvar::new()),
            flashing: AtomicBool::new(false),
            last_flash: Mutex::new(None),
            installing: AtomicBool::new(false),
            firmware_manager: Mutex::new(FirmwareManagerState::default()),
            wifi_setup: Mutex::new(WifiSetupState::default()),
        }
    }
}

/// Aktueller Snapshot mit dem Prozentmodus aus den Einstellungen.
/// Sperrreihenfolge: erst Einstellungen, dann Quelle, nie beide gleichzeitig.
pub fn current_snapshot(app: &AppHandle) -> Snapshot {
    let state = app.state::<AppState>();
    let mode = state.settings.lock().unwrap().percent_mode;
    let snap = state.source.lock().unwrap().snapshot(mode);
    snap
}
