//! Serielle Verbindung zum Gerät als eigener Thread (Spec 6.1, 6.2, 6.5).
//!
//! Der Thread besitzt den `Link` allein; niemand sonst fasst den Port an.
//! Aufträge kommen über einen mpsc-Kanal, der Zustand geht als
//! `ConnectionSnapshot` über das Event `connection-changed` ans Frontend und
//! in `AppState::connection`. Die Sperren auf Registry, Einstellungen und
//! Quelle werden nur kurz gehalten, nie über einen blockierenden seriellen
//! Zugriff hinweg.

use crate::audio_visualizer::VisualizerSnapshot;
use crate::flash;
use crate::music::{
    art_chunk_payload, background_chunk_payload, Artwork, MusicAction, MusicBackground,
    MusicCommand, MusicSnapshot, MUSIC_ART_CHUNK_BYTES, MUSIC_BACKGROUND_CHUNK_BYTES,
};
use crate::performance::PerformanceSample;
use crate::poll;
use crate::registry;
use crate::settings::{ViewContent, ViewMode};
use crate::smart_switch::{usage_signals, SmartSwitch, UsageSignal};
use crate::state::{current_snapshot, AppState};
use crate::timezone;
use crate::tray;
use crate::updates;
use aimonitor_core::envelope::{
    diagnostic_envelope, notice_envelope, usage_envelope, FrameContext,
};
use aimonitor_core::plugin::{scene_envelope, status_scene_with_theme, status_text, SceneLayout};
use aimonitor_core::protocol::{
    Command, DisplayVariant, FrameIdCounter, Language, Orientation, ThemeSetting,
    DIAGNOSTIC_AFTER_CONNECT, DIAGNOSTIC_RESTORE, GET_INFO_TIMEOUT, HEARTBEAT_INTERVAL,
    LATE_INFO_WINDOW, RECONNECT_BLOCK_WINDOW, REPAIR_COOLDOWN, REPAIR_RECONNECT_DELAY,
    REPAIR_THRESHOLD, SCAN_INTERVAL, SEND_DEBOUNCE,
};
use aimonitor_core::protocol::{DeviceMessage, ViewState};
use aimonitor_core::{DeviceInfo, DeviceProfile, Snapshot, POLL_INTERVAL};
use aimonitor_serial::{list_ports, ports::choose_port, FrameReceipt, Link, LinkError};
use chrono::{DateTime, Local, Utc};
use serde::Serialize;
use serde_json::{json, Value};
use std::collections::{BTreeMap, VecDeque};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, Manager};

pub const CONNECTION_EVENT: &str = "connection-changed";
pub const SETTINGS_EVENT: &str = "settings-changed";
/// Zeilen im veröffentlichten Protokoll.
const LOG_LINES: usize = 50;
/// Wie lange der Thread höchstens blockiert, bevor er wieder Aufträge liest.
const IDLE_SLICE: Duration = Duration::from_millis(250);

fn recent_sample(fetched_at: Option<&DateTime<Utc>>, max_age: Duration) -> bool {
    fetched_at.is_some_and(|fetched| {
        let age = Utc::now()
            .signed_duration_since(fetched.to_owned())
            .num_seconds();
        age >= 0 && age <= max_age.as_secs() as i64
    })
}

fn echo_ack_is_terminal(method: &str) -> bool {
    matches!(
        method,
        "restart"
            | "setup_begin"
            | "setup_complete"
            | "factory_reset"
            | "prepare_update"
            | "update_complete"
    )
}

fn update_ack_matches(method: &str, value: &Value) -> bool {
    let expected = match method {
        "prepare_update" => "echodesk_prepare_update",
        "update_complete" => "echodesk_update_complete",
        _ => return true,
    };
    value.get("command").and_then(Value::as_str) == Some(expected)
}
/// Kurze Lesescheibe für die späte `info` nach `foreignFirmware`.
const LATE_INFO_SLICE: Duration = Duration::from_millis(200);

// ---------------------------------------------------------------------------
// Aufträge und veröffentlichter Zustand
// ---------------------------------------------------------------------------

/// Auftrag an den Thread. Alles, was das Frontend oder andere Threads auslösen.
#[derive(Debug)]
pub enum Job {
    SetManualPort(Option<String>),
    /// Datenframe über den Debounce anfordern (neue Daten, Provider, Einstellungen).
    Resend,
    ConfigureViews,
    SmartTouch,
    ResetSmart,
    ResetSmartPlugin(String),
    SmartUsage(aimonitor_core::Provider, Vec<UsageSignal>),
    SmartPlugin(String, BTreeMap<String, bool>),
    SmartClaudeCode(Vec<(String, aimonitor_core::claude_code::Waiting, i64)>),
    /// A coalesced helper sample is ready in `AppState::performance`.
    PerformanceReady,
    /// A coalesced Windows media-session snapshot is ready.
    MusicReady,
    /// A coalesced, PCM-free WASAPI band snapshot is ready.
    AudioVisualizerReady,
    /// Allowlisted EchoDesk Desktop request. The named-pipe broker never gets
    /// access to the raw serial link.
    EchoDeskControl {
        method: String,
        params: Value,
        reply: Sender<Result<Value, Value>>,
    },
    /// Close and immediately rescan without changing the saved port choice.
    EchoDeskReconnect,
    SendDiagnostic,
    Wifi {
        action: WifiAction,
        reply: Sender<Result<Value, String>>,
    },
    /// Nur die geänderten Werte sind `Some`; das Profil selbst liegt in der Registry.
    ApplyProfile {
        theme: Option<ThemeSetting>,
        orientation: Option<Orientation>,
        language: Option<Language>,
    },
    SetBrightness {
        value: i64,
        persist: bool,
    },
    /// Profil aus der Registry neu lesen und veröffentlichen (z. B. nach Umbenennen).
    RefreshProfile,
    /// Standby-Uhr zeigen, Port bleibt offen. Wird ab Phase 3 vor dem Flash benutzt.
    #[allow(dead_code)]
    Standby,
    /// `standby` senden, Port schließen, dann den Sender benachrichtigen.
    Shutdown(Sender<()>),
    /// Für den Flash (Spec 6.4): trennen, Scan stoppen, Port schließen,
    /// dann den Sender benachrichtigen. Bis `Resume` passiert nichts mehr.
    Pause(Sender<()>),
    /// Scan wieder an. Mit `diagnostic_after_connect` geht nach dem nächsten
    /// Connect verzögert der Diagnose-Frame raus, danach der echte Snapshot.
    Resume {
        diagnostic_after_connect: bool,
    },
    /// Zeile aus einem anderen Thread ins Ereignisprotokoll (z. B. Flash).
    Log(String),
}

#[derive(Debug)]
pub enum WifiAction {
    Status,
    Scan,
    Set { ssid: String, password: String },
    Forget,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub enum ConnectionState {
    #[default]
    Disconnected,
    Probing,
    Connected,
    ForeignFirmware,
}

#[derive(Debug, Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct ConnectionSnapshot {
    pub state: ConnectionState,
    /// Scan angehalten, weil gerade geflasht wird.
    pub paused: bool,
    pub port: Option<String>,
    pub manual_port: Option<String>,
    pub info: Option<DeviceInfo>,
    /// Profil des verbundenen, sonst des zuletzt bekannten Geräts.
    pub profile: Option<DeviceProfile>,
    pub last_receipt: Option<FrameReceipt>,
    pub last_frame_bytes: Option<usize>,
    pub last_frame_at: Option<DateTime<Utc>>,
    pub unacked_count: u32,
    pub frames_sent: u64,
    pub frames_acked: u64,
    /// Letzte Zeilen: Gerätezeilen mit `<-`, eigene Ereignisse ohne Präfix.
    pub log: Vec<String>,
}

// ---------------------------------------------------------------------------
// Öffentliche Helfer für Commands, Tray und Poll
// ---------------------------------------------------------------------------

pub fn send(app: &AppHandle, job: Job) {
    if let Err(e) = app.state::<AppState>().serial.send(job) {
        eprintln!("[aimonitor] Serial-Thread nimmt keine Aufträge mehr an: {e}");
    }
}

/// Zeile ins Ereignisprotokoll schreiben, das die Verbindungsseite zeigt.
pub fn log(app: &AppHandle, text: impl Into<String>) {
    send(app, Job::Log(text.into()));
}

pub fn request_resend(app: &AppHandle) {
    send(app, Job::Resend);
}

/// Beenden aus dem Tray: `standby` senden, Port schließen, dann die App beenden.
/// Läuft in einem eigenen Thread, damit der Menü-Handler nicht blockiert.
pub fn shutdown_and_exit(app: &AppHandle) {
    let app = app.clone();
    std::thread::spawn(move || {
        let (tx, rx) = mpsc::channel();
        send(&app, Job::Shutdown(tx));
        let _ = rx.recv_timeout(Duration::from_secs(3));
        app.exit(0);
    });
}

/// Thread starten. Der Sender gehört in den `AppState`.
pub fn start(app: AppHandle, rx: Receiver<Job>, manual_port: Option<String>) {
    std::thread::Builder::new()
        .name("aimonitor-serial".into())
        .spawn(move || Service::new(app, manual_port).run(rx))
        .expect("Serial-Thread");
}

// ---------------------------------------------------------------------------
// Hinweistexte fürs Display in der Sprache des Geräteprofils (dsp.notice.*)
// ---------------------------------------------------------------------------

fn notice_text(key: &str, language: Language) -> &'static str {
    match (language, key) {
        (Language::De, "dsp.notice.climissing") => "CodexBar-CLI fehlt",
        (Language::De, "dsp.notice.auth") => "Bei Codex anmelden",
        (Language::De, "dsp.notice.failed") => "Abruf fehlgeschlagen",
        (Language::De, "dsp.notice.loading") => "Lade Provider ...",
        (Language::De, "dsp.notice.parse") => "Datenfehler",
        (Language::De, "dsp.notice.stale") => "Daten veraltet",
        (Language::De, "dsp.notice.startapp") => "Bitte App starten",
        (Language::En, "dsp.notice.climissing") => "CodexBar CLI missing",
        (Language::En, "dsp.notice.auth") => "Sign in to Codex",
        (Language::En, "dsp.notice.failed") => "Fetch failed",
        (Language::En, "dsp.notice.loading") => "Loading provider ...",
        (Language::En, "dsp.notice.parse") => "Data error",
        (Language::En, "dsp.notice.stale") => "Data outdated",
        (Language::En, "dsp.notice.startapp") => "Please start the app",
        _ => "",
    }
}

/// Systemerscheinungsbild für `ThemeSetting::System`; unbekannt gilt als hell.
fn system_is_dark() -> bool {
    matches!(dark_light::detect(), Ok(dark_light::Mode::Dark))
}

// ---------------------------------------------------------------------------
// Der Thread
// ---------------------------------------------------------------------------

enum LinkState {
    Disconnected,
    Probing,
    Connected(DeviceInfo),
    ForeignFirmware,
}

struct PendingMusicArt {
    art: Artwork,
    next_chunk: usize,
}

struct PendingMusicBackground {
    background: MusicBackground,
    next_chunk: usize,
}

struct Service {
    app: AppHandle,
    link: Option<Link>,
    state: LinkState,
    manual_port: Option<String>,
    profile: Option<DeviceProfile>,
    frame_ids: FrameIdCounter,
    /// Während des Flashs: kein Scan, kein Port.
    paused: bool,
    /// Nach dem nächsten Connect den Diagnose-Frame einplanen (nach Flash).
    diagnostic_after_connect: bool,
    /// Fälligkeit des Diagnose-Frames nach dem Connect.
    diagnostic_due: Option<Instant>,

    next_scan: Instant,
    last_disconnect: Option<Instant>,
    /// Fenster für eine späte `info` nach `foreignFirmware`.
    late_info_until: Option<Instant>,
    /// Fälligkeit des gebündelten Datenframes.
    send_due: Option<Instant>,
    heartbeat_due: Option<Instant>,
    /// Bis dahin bleibt der Diagnose-Testframe auf dem Display.
    diagnostic_until: Option<Instant>,
    last_repair: Option<Instant>,

    last_receipt: Option<FrameReceipt>,
    last_frame_bytes: Option<usize>,
    last_frame_at: Option<DateTime<Utc>>,
    unacked: u32,
    frames_sent: u64,
    frames_acked: u64,
    log: VecDeque<String>,
    smart: SmartSwitch,
    pending_music_art: Option<PendingMusicArt>,
    last_music_art_id: Option<String>,
    pending_music_background: Option<PendingMusicBackground>,
    last_music_background_id: Option<String>,
    music_background_requested: bool,
    music_visualizer_requested: bool,
    desktop_request_id: u32,
}

impl Service {
    fn new(app: AppHandle, manual_port: Option<String>) -> Self {
        Self {
            app,
            link: None,
            state: LinkState::Disconnected,
            manual_port,
            profile: None,
            frame_ids: FrameIdCounter::new(),
            paused: false,
            diagnostic_after_connect: false,
            diagnostic_due: None,
            next_scan: Instant::now(),
            last_disconnect: None,
            late_info_until: None,
            send_due: None,
            heartbeat_due: None,
            diagnostic_until: None,
            last_repair: None,
            last_receipt: None,
            last_frame_bytes: None,
            last_frame_at: None,
            unacked: 0,
            frames_sent: 0,
            frames_acked: 0,
            log: VecDeque::new(),
            smart: {
                let mut smart = SmartSwitch::default();
                smart.reset(Instant::now());
                smart
            },
            pending_music_art: None,
            last_music_art_id: None,
            pending_music_background: None,
            last_music_background_id: None,
            music_background_requested: false,
            music_visualizer_requested: false,
            desktop_request_id: 0,
        }
    }

    fn run(mut self, rx: Receiver<Job>) {
        self.profile = self.registry_profile();
        self.publish();
        loop {
            let wait = self
                .next_deadline()
                .map(|d| d.saturating_duration_since(Instant::now()))
                .unwrap_or(IDLE_SLICE)
                .min(IDLE_SLICE);
            match rx.recv_timeout(wait) {
                Ok(job) => {
                    if !self.handle(job) {
                        break;
                    }
                }
                Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => break,
            }
            self.tick();
        }
        self.close_port();
    }

    // -- Protokoll und Veröffentlichung ------------------------------------

    fn log_event(&mut self, text: impl AsRef<str>) {
        let line = format!("{} {}", Local::now().format("%H:%M:%S"), text.as_ref());
        println!("[serial] {}", text.as_ref());
        if self.log.len() >= 200 {
            self.log.pop_front();
        }
        self.log.push_back(line);
    }

    /// Zeilen, die der Link nebenbei gelesen hat (ok, error, Fremdes), übernehmen.
    fn absorb_link_log(&mut self) {
        let lines: Vec<String> = match self.link.as_mut() {
            Some(link) => link.log.drain(..).collect(),
            None => return,
        };
        for l in lines {
            self.log_event(format!("<- {l}"));
        }
    }

    fn registry_profile(&self) -> Option<DeviceProfile> {
        self.app
            .state::<AppState>()
            .registry
            .lock()
            .unwrap()
            .current_profile()
            .cloned()
    }

    fn snapshot(&self) -> ConnectionSnapshot {
        let (state, info) = match &self.state {
            LinkState::Disconnected => (ConnectionState::Disconnected, None),
            LinkState::Probing => (ConnectionState::Probing, None),
            LinkState::Connected(info) => (ConnectionState::Connected, Some(info.clone())),
            LinkState::ForeignFirmware => (ConnectionState::ForeignFirmware, None),
        };
        let skip = self.log.len().saturating_sub(LOG_LINES);
        ConnectionSnapshot {
            state,
            paused: self.paused,
            port: self.link.as_ref().map(|l| l.name().to_string()),
            manual_port: self.manual_port.clone(),
            info,
            profile: self.profile.clone(),
            last_receipt: self.last_receipt.clone(),
            last_frame_bytes: self.last_frame_bytes,
            last_frame_at: self.last_frame_at,
            unacked_count: self.unacked,
            frames_sent: self.frames_sent,
            frames_acked: self.frames_acked,
            log: self.log.iter().skip(skip).cloned().collect(),
        }
    }

    fn publish(&mut self) {
        self.absorb_link_log();
        let snap = self.snapshot();
        let app_state = self.app.state::<AppState>();
        let broker_changed = {
            let mut previous = app_state.connection.lock().unwrap();
            let changed = previous.state != snap.state
                || previous.port != snap.port
                || previous.info != snap.info;
            *previous = snap.clone();
            changed
        };
        if broker_changed {
            app_state
                .desktop_broker_revision
                .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
        }
        if let Err(e) = self.app.emit(CONNECTION_EVENT, &snap) {
            eprintln!("[aimonitor] Verbindungs-Event nicht gesendet: {e}");
        }
        tray::refresh(&self.app);
    }

    // -- Zeitsteuerung -----------------------------------------------------

    fn next_deadline(&self) -> Option<Instant> {
        [
            Some(self.next_scan),
            self.send_due,
            self.heartbeat_due,
            self.diagnostic_until,
            self.diagnostic_due,
            self.late_info_until,
        ]
        .into_iter()
        .flatten()
        .min()
    }

    fn tick(&mut self) {
        let now = Instant::now();

        if self.paused {
            return;
        }
        if now >= self.next_scan {
            self.next_scan = now + SCAN_INTERVAL;
            match self.state {
                LinkState::Disconnected => {
                    let blocked = self
                        .last_disconnect
                        .map(|t| now.duration_since(t) < RECONNECT_BLOCK_WINDOW)
                        .unwrap_or(false);
                    if !blocked {
                        self.scan_and_connect();
                    }
                }
                _ => self.check_port_still_present(),
            }
        }

        if let (LinkState::ForeignFirmware, Some(until)) = (&self.state, self.late_info_until) {
            if now < until {
                self.poll_late_info();
            } else {
                self.late_info_until = None;
                self.log_event("Keine späte info, Gerät bleibt als fremde Firmware markiert");
                self.publish();
            }
        }

        if !matches!(self.state, LinkState::Connected(_)) {
            return;
        }
        self.poll_view_events();
        self.apply_smart_switch();
        let now = Instant::now();
        if let Some(due) = self.diagnostic_due {
            if now >= due {
                self.diagnostic_due = None;
                self.log_event("Diagnose-Frame nach Flash und Reconnect");
                self.send_diagnostic_frame();
            }
        }
        if let Some(until) = self.diagnostic_until {
            if now >= until {
                self.diagnostic_until = None;
                self.log_event("Diagnose beendet, echter Snapshot folgt");
                self.send_due = None;
                // The unindexed diagnostic frame activates the firmware legacy
                // single-provider view. Restore current views (including clock
                // and plugins) before sending their real data.
                self.configure_views();
                self.send_data_frame("Rueckkehr zum Snapshot");
            }
        }
        if let Some(due) = self.send_due {
            if now >= due {
                self.send_due = None;
                if self.diagnostic_until.is_none() {
                    self.send_data_frame("Snapshot");
                }
            }
        }
        if let Some(due) = self.heartbeat_due {
            if now >= due && matches!(self.state, LinkState::Connected(_)) {
                self.heartbeat_due = Some(now + HEARTBEAT_INTERVAL);
                if self.diagnostic_until.is_none() {
                    self.send_data_frame("Heartbeat");
                }
            }
        }
        if self.pending_music_art.is_some() {
            self.send_pending_music_art();
        } else {
            self.send_pending_music_background();
        }
    }

    fn apply_smart_switch(&mut self) {
        let state = self.app.state::<AppState>();
        let mut settings = state.settings.lock().unwrap();
        self.smart.sync_revision(
            settings.smart_revision,
            settings.smart_manual,
            Instant::now(),
        );
        let (mode, active) = (settings.view_mode, settings.active_view);
        if mode != ViewMode::Intelligent
            || !matches!(&self.state, LinkState::Connected(info) if info.supports_views())
        {
            return;
        }
        let lacks_plugin_scenes =
            matches!(&self.state, LinkState::Connected(info) if !info.supports_plugin_scenes());
        if lacks_plugin_scenes
            && settings
                .views
                .iter()
                .any(|view| matches!(view, ViewContent::Plugin(_)))
        {
            return;
        }
        let Some(next) = self.smart.choose(active, Instant::now()) else {
            return;
        };
        let (updated, provider) = {
            if settings.view_mode != ViewMode::Intelligent || next >= settings.views.len() {
                return;
            }
            settings.active_view = next;
            let provider = match &settings.views[next] {
                ViewContent::Provider(provider) => Some(*provider),
                _ => None,
            };
            if let Some(provider) = provider {
                settings.provider = provider;
            }
            // Persist the automatic selection so reconnecting cannot mistake it for a touch.
            settings.save(&self.app);
            (settings.clone(), provider)
        };
        drop(settings);
        let _ = self.app.emit(SETTINGS_EVENT, updated);
        if let Some(provider) = provider {
            state
                .source
                .lock()
                .unwrap()
                .set_provider(provider, Utc::now());
            poll::start_fetch(&self.app);
            poll::refresh_views(&self.app);
        }
        self.log_event(format!("Intelligenter Wechsel: Fenster {}", next + 1));
        self.configure_views();
        self.publish();
    }

    /// Datenframe über den Debounce anfordern; mehrere Auslöser ergeben einen Frame.
    fn schedule_send(&mut self) {
        if matches!(self.state, LinkState::Connected(_)) && self.send_due.is_none() {
            self.send_due = Some(Instant::now() + SEND_DEBOUNCE);
        }
    }

    // -- Verbindung --------------------------------------------------------

    fn scan_and_connect(&mut self) {
        let ports = list_ports();
        let Some(candidate) = choose_port(&ports, self.manual_port.as_deref()).cloned() else {
            return;
        };
        self.state = LinkState::Probing;
        self.log_event(format!(
            "Öffne {} ({})",
            candidate.name,
            candidate.chip.unwrap_or("unbekannter Chip")
        ));
        let mut link = match Link::open(&candidate.name) {
            Ok(l) => l,
            Err(e) => {
                self.log_event(format!("{e}"));
                self.state = LinkState::Disconnected;
                self.publish();
                return;
            }
        };
        self.publish();
        match link.handshake(GET_INFO_TIMEOUT) {
            Ok(info) => {
                self.link = Some(link);
                self.on_connected(info);
            }
            Err(LinkError::HandshakeTimeout(_)) => {
                self.link = Some(link);
                self.state = LinkState::ForeignFirmware;
                self.late_info_until = Some(Instant::now() + LATE_INFO_WINDOW);
                self.log_event(format!("Keine info innerhalb von {GET_INFO_TIMEOUT:?}, warte {LATE_INFO_WINDOW:?} auf eine späte Antwort"));
                self.publish();
            }
            Err(e) => {
                self.log_event(format!("Handshake fehlgeschlagen: {e}"));
                self.link = Some(link);
                self.disconnect("Handshake-Fehler");
            }
        }
    }

    fn poll_late_info(&mut self) {
        let Some(link) = self.link.as_mut() else {
            return;
        };
        match link.wait_for_info(LATE_INFO_SLICE) {
            Ok(info) => {
                self.late_info_until = None;
                self.log_event("Späte info empfangen");
                self.on_connected(info);
            }
            Err(LinkError::HandshakeTimeout(_)) => {}
            Err(e) => {
                self.log_event(format!("{e}"));
                self.disconnect("Lesefehler");
            }
        }
    }

    /// Verbindung trennen, wenn der Port aus der Liste verschwunden ist oder
    /// ein Lesefehler aufgetreten ist. Ein Gerät, das sich kurz am USB
    /// abmeldet, kommt unter demselben COM-Namen zurück; der alte Handle
    /// bleibt aber tot und muss neu geöffnet werden.
    fn check_port_still_present(&mut self) {
        let Some((name, lost)) = self
            .link
            .as_ref()
            .map(|l| (l.name().to_string(), l.is_lost()))
        else {
            return;
        };
        if lost {
            self.disconnect("Lesefehler, Port wird neu geöffnet");
        } else if !list_ports().iter().any(|p| p.name == name) {
            self.disconnect("Port nicht mehr vorhanden");
        }
    }

    fn on_connected(&mut self, info: DeviceInfo) {
        self.log_event(format!(
            "Verbunden: Firmware {} MAC {} transport {} max {} B",
            info.version,
            info.mac,
            info.serial_transport.as_deref().unwrap_or("-"),
            info.max_frame_bytes()
        ));
        if let Some(panel) = &info.panel {
            self.log_event(format!(
                "Panel {panel} ({}), Firmware-Variante {}",
                info.panel_id.as_deref().unwrap_or("-"),
                info.display.map_or("-", |d| d.wire())
            ));
        }
        let (profile, outcome) = {
            let state = self.app.state::<AppState>();
            let mut registry = state.registry.lock().unwrap();
            let resolved = registry.resolve(&info, Utc::now());
            registry::save(&self.app, &registry);
            resolved
        };
        self.log_event(format!(
            "Profil {:?}: {} ({})",
            outcome, profile.friendly_name, profile.mac
        ));
        let connected_info = info.clone();
        self.state = LinkState::Connected(info);
        self.profile = Some(profile.clone());
        self.unacked = 0;
        self.pending_music_art = None;
        self.last_music_art_id = None;
        self.pending_music_background = None;
        self.last_music_background_id = None;
        self.music_background_requested = false;
        self.music_visualizer_requested = false;
        self.late_info_until = None;
        self.diagnostic_until = None;
        if self.diagnostic_after_connect {
            self.diagnostic_after_connect = false;
            self.diagnostic_due = Some(Instant::now() + DIAGNOSTIC_AFTER_CONNECT);
            self.log_event(format!(
                "Diagnose-Frame in {DIAGNOSTIC_AFTER_CONNECT:?} eingeplant"
            ));
        }
        self.publish();
        // Neue Geräteversion: Update-Hinweis neu bewerten, sonst bliebe er
        // nach einem Flash bis zur nächsten Release-Abfrage stehen.
        updates::emit_status(&self.app);
        flash::after_connect(&self.app, &connected_info);

        // Spec 6.1: vier set_*-Kommandos ohne Antwortauswertung, dann ein Frame.
        let theme = profile.theme.resolve(system_is_dark());
        // EchoDesk-control firmware is the source of truth for its persistent
        // theme/brightness. Replaying an older AI Monitor profile here would
        // overwrite a newer touchscreen change before the v1 state handshake.
        let mut commands = vec![
            Command::set_language(profile.language),
            Command::set_orientation(profile.orientation),
        ];
        if !connected_info.supports_echo_desk_control() {
            commands.insert(0, Command::set_theme(theme));
            commands.push(Command::set_brightness(profile.brightness, true));
        }
        for cmd in commands {
            if !self.send_command(&cmd) {
                return;
            }
        }
        if connected_info.supports_echo_desk_control() {
            if let Err(error) = self.refresh_echo_desk_state() {
                self.log_event(format!(
                    "EchoDesk Desktop state unavailable: {}",
                    error
                        .get("message")
                        .and_then(Value::as_str)
                        .unwrap_or("unknown error")
                ));
            } else {
                self.sync_profile_from_echo_state(false);
            }
        }
        self.restore_touch_selection();
        self.configure_views();
        self.heartbeat_due = Some(Instant::now() + HEARTBEAT_INTERVAL);
        self.schedule_send();
        self.publish();
    }

    /// Kommando schreiben; Schreibfehler trennen die Verbindung. Gibt `false`
    /// zurück, wenn danach keine Verbindung mehr besteht.
    fn send_command(&mut self, line: &str) -> bool {
        let Some(link) = self.link.as_mut() else {
            return false;
        };
        let result = link.send_command(line);
        self.log_event(format!("-> {}", line.trim_end()));
        match result {
            Ok(()) => true,
            Err(e) => {
                self.log_event(format!("Schreibfehler: {e}"));
                self.disconnect("Schreibfehler");
                false
            }
        }
    }

    fn close_port(&mut self) {
        // Capture is opt-in for a currently connected device. A disconnect
        // revokes that request until firmware explicitly sends it again.
        self.app
            .state::<AppState>()
            .audio_visualizer
            .set_enabled(false);
        self.absorb_link_log();
        self.link = None;
    }

    fn disconnect(&mut self, reason: &str) {
        let was_open = self.link.is_some();
        self.close_port();
        if was_open || !matches!(self.state, LinkState::Disconnected) {
            self.log_event(format!("Getrennt: {reason}"));
        }
        self.state = LinkState::Disconnected;
        self.last_disconnect = Some(Instant::now());
        self.late_info_until = None;
        self.send_due = None;
        self.heartbeat_due = None;
        self.diagnostic_until = None;
        self.diagnostic_due = None;
        self.unacked = 0;
        self.pending_music_art = None;
        self.last_music_art_id = None;
        self.pending_music_background = None;
        self.last_music_background_id = None;
        self.music_background_requested = false;
        self.music_visualizer_requested = false;
        let app_state = self.app.state::<AppState>();
        let had_echo_desk_state = app_state.echo_desk_state.lock().unwrap().take().is_some();
        let had_clock_state = app_state
            .echo_desk_clock_state
            .lock()
            .unwrap()
            .take()
            .is_some();
        let had_clock_request = app_state
            .echo_desk_clock_config_request
            .lock()
            .unwrap()
            .take()
            .is_some();
        let had_notification_state = app_state
            .echo_desk_notification_state
            .lock()
            .unwrap()
            .take()
            .is_some();
        if had_echo_desk_state || had_clock_state || had_clock_request || had_notification_state {
            app_state
                .desktop_broker_revision
                .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
        }
        {
            let state = self.app.state::<AppState>();
            let mut registry = state.registry.lock().unwrap();
            registry.disconnected();
            registry::save(&self.app, &registry);
        }
        self.profile = self.registry_profile();
        self.publish();
    }

    // -- Datenframes -------------------------------------------------------

    fn frame_context(&self, snap: &Snapshot) -> FrameContext {
        let now = Utc::now();
        let tz = self
            .app
            .state::<AppState>()
            .settings
            .lock()
            .unwrap()
            .timezone
            .clone();
        FrameContext::new(now, timezone::offset_minutes(&tz, now), snap.fetching)
    }

    /// Usage- oder Notice-Frame aus dem aktuellen Snapshot; `None`, wenn es
    /// nichts zu zeigen gibt (Spec 5.3).
    fn build_payload(
        &mut self,
        info: &DeviceInfo,
        snap: Snapshot,
        view_index: Option<usize>,
    ) -> Option<(String, &'static str, i64)> {
        let ctx = self.frame_context(&snap);
        let frame_id = self.frame_ids.next();
        let (payload, kind) = if snap.status.is_ok() {
            let entry = snap.entry.as_ref()?;
            (
                usage_envelope(entry, snap.percent_mode, &ctx, frame_id),
                "usage",
            )
        } else {
            let key = snap
                .status
                .display_notice_key()
                .or_else(|| snap.fetching.then_some("dsp.notice.loading"))?;
            if !info.supports_notice() {
                self.log_event(format!(
                    "Hinweis {key} nicht gesendet, Firmware ohne notice-Unterstützung"
                ));
                return None;
            }
            let language = self
                .profile
                .as_ref()
                .map(|p| p.language)
                .unwrap_or_default();
            (
                notice_envelope(snap.provider, notice_text(key, language), &ctx, frame_id),
                "notice",
            )
        };
        if let Some(index) = view_index {
            let mut value: serde_json::Value = serde_json::from_str(&payload).ok()?;
            value["data"][0]["viewIndex"] = json!(index);
            return Some((value.to_string(), kind, frame_id));
        }
        Some((payload, kind, frame_id))
    }

    fn configure_views(&mut self) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !info.supports_views() {
            return;
        }
        let settings = self
            .app
            .state::<AppState>()
            .settings
            .lock()
            .unwrap()
            .clone();
        if settings
            .views
            .iter()
            .any(|v| matches!(v, ViewContent::Plugin(_)))
            && !info.supports_plugin_scenes()
        {
            self.log_event("Plugin-Fenster brauchen eine Firmware mit sceneProtocol 1");
            self.publish();
            return;
        }
        let contents: Vec<String> = settings
            .views
            .iter()
            .map(|v| match v {
                ViewContent::Clock => "clock".into(),
                ViewContent::Provider(p) => p.key().into(),
                ViewContent::Plugin(id) => format!("plugin:{id}"),
            })
            .collect();
        let line = json!({
            "cmd": "set_views", "views": contents,
            "mode": match settings.view_mode { ViewMode::Manual => "manual", ViewMode::Automatic => "automatic", ViewMode::Intelligent => "manual" },
            "interval": settings.view_interval_seconds,
            "active": settings.active_view,
        }).to_string() + "\n";
        self.send_command(&line);
    }

    /// Nur eine passende Gerätekonfiguration darf die lokale Auswahl übernehmen.
    /// So überschreibt ein Reconnect keinen inzwischen am ESP gewählten Tab.
    fn restore_touch_selection(&mut self) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !info.supports_views() {
            return;
        }
        let result = self.link.as_mut().unwrap().command_with_response(
            "{\"cmd\":\"get_views\"}\n",
            "view_state",
            Duration::from_millis(500),
        );
        match result {
            Ok(Some(DeviceMessage::ViewState(view))) => self.apply_device_selection(view),
            Ok(_) => self.log_event("Keine Fensterantwort vom Gerät"),
            Err(e) => self.log_event(format!("Fensterabfrage fehlgeschlagen: {e}")),
        }
        self.absorb_link_log();
    }

    fn poll_view_events(&mut self) {
        let Some(link) = self.link.as_mut() else {
            return;
        };
        link.poll_input();
        let events = link.take_view_events();
        let music_controls = link.take_music_controls();
        let echo_desk_events = link.take_echo_desk_events();
        self.absorb_link_log();
        for view in events {
            self.apply_device_selection(view);
        }
        for message in music_controls {
            match message {
                DeviceMessage::MusicControl {
                    generation, action, ..
                } => {
                    let action = match action.as_str() {
                        "toggle" => MusicAction::Toggle,
                        "previous" => MusicAction::Previous,
                        "next" => MusicAction::Next,
                        _ => continue,
                    };
                    self.app
                        .state::<AppState>()
                        .music
                        .command(MusicCommand { generation, action });
                }
                DeviceMessage::MusicVisualizer { enabled, .. } => {
                    let supported = matches!(
                        &self.state,
                        LinkState::Connected(info) if info.supports_music_visualizer()
                    );
                    let capture_enabled = enabled && supported;
                    let changed = self.music_visualizer_requested != capture_enabled;
                    self.music_visualizer_requested = capture_enabled;
                    self.app
                        .state::<AppState>()
                        .audio_visualizer
                        .set_enabled(capture_enabled);
                    if changed || (enabled && !supported) {
                        self.log_event(if capture_enabled {
                            "Audio Bars enabled by device (local output loopback may include all system audio)"
                        } else if enabled {
                            "Audio Bars request ignored: firmware capability was not negotiated"
                        } else {
                            "Audio Bars disabled by device"
                        });
                    }
                }
                DeviceMessage::MusicBackgroundRequest { enabled, .. } => {
                    let supported = matches!(
                        &self.state,
                        LinkState::Connected(info) if info.supports_music_background()
                    );
                    let requested = enabled && supported;
                    let changed = self.music_background_requested != requested;
                    self.music_background_requested = requested;
                    if !self.music_background_requested {
                        self.pending_music_background = None;
                    }
                    if changed || (enabled && !supported) {
                        self.log_event(if self.music_background_requested {
                            "Now Playing display background requested by device"
                        } else if enabled {
                            "Display background request ignored: firmware capability was not negotiated"
                        } else {
                            "Now Playing display background disabled by device"
                        });
                    }
                }
                _ => {}
            }
        }
        let mut echo_desk_changed = false;
        for message in echo_desk_events {
            match message {
                DeviceMessage::EchoDeskState(value) => {
                    if self.store_echo_desk_state(value).is_ok() {
                        // Device-originated events include both persistent touch
                        // changes and host brightness previews. Update the live
                        // profile but never write devices.json from an event;
                        // confirmed persistent broker requests save explicitly.
                        self.sync_profile_from_echo_state(false);
                        echo_desk_changed = true;
                    }
                }
                DeviceMessage::EchoDeskClockState(value) => {
                    if self.store_clock_state(value).is_ok() {
                        echo_desk_changed = true;
                    }
                }
                DeviceMessage::EchoDeskClockConfigRequest { mode } => {
                    let state = self.app.state::<AppState>();
                    let mut request = state.echo_desk_clock_config_request.lock().unwrap();
                    *request = Some(mode);
                    state
                        .echo_desk_clock_config_sequence
                        .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
                    state
                        .desktop_broker_revision
                        .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
                    echo_desk_changed = true;
                }
                DeviceMessage::EchoDeskNotificationsState(value) => {
                    if self.store_notification_state(value).is_ok() {
                        echo_desk_changed = true;
                    }
                }
                DeviceMessage::EchoDeskPcDashboardState(value) => {
                    if self.store_pc_dashboard_state(value).is_ok() {
                        echo_desk_changed = true;
                    }
                }
                _ => {}
            }
        }
        if echo_desk_changed {
            self.publish();
        }
        if self.link.as_ref().is_some_and(Link::is_lost) {
            self.disconnect("Lesefehler beim Fensterereignis");
        }
    }

    fn apply_device_selection(&mut self, view: ViewState) {
        let app_state = self.app.state::<AppState>();
        let (changed, provider) = {
            let mut settings = app_state.settings.lock().unwrap();
            let expected: Vec<String> = settings
                .views
                .iter()
                .map(|v| match v {
                    ViewContent::Clock => "clock".into(),
                    ViewContent::Provider(p) => p.key().into(),
                    ViewContent::Plugin(id) => format!("plugin:{id}"),
                })
                .collect();
            let mode = match settings.view_mode {
                ViewMode::Manual => "manual",
                ViewMode::Automatic => "automatic",
                ViewMode::Intelligent => "manual",
            };
            if view.views != expected
                || view.mode != mode
                || view.interval != settings.view_interval_seconds
                || view.active >= settings.views.len()
                || view.active == settings.active_view
            {
                return;
            }
            settings.active_view = view.active;
            let provider = match &settings.views[view.active] {
                ViewContent::Clock => None,
                ViewContent::Provider(p) => Some(*p),
                ViewContent::Plugin(_) => None,
            };
            if let Some(p) = provider {
                settings.provider = p;
            }
            settings.save(&self.app);
            (true, provider)
        };
        if changed {
            if self
                .app
                .state::<AppState>()
                .settings
                .lock()
                .unwrap()
                .view_mode
                == ViewMode::Intelligent
            {
                self.smart.touch(Instant::now());
            }
            if let Some(provider) = provider {
                app_state
                    .source
                    .lock()
                    .unwrap()
                    .set_provider(provider, Utc::now());
                poll::start_fetch(&self.app);
                poll::refresh_views(&self.app);
            }
            let settings = app_state.settings.lock().unwrap().clone();
            let _ = self.app.emit(SETTINGS_EVENT, settings);
            self.log_event(format!(
                "Touch-Auswahl übernommen: Fenster {}",
                view.active + 1
            ));
            self.publish();
        }
    }

    fn desktop_error(code: &str, message: impl Into<String>) -> Value {
        json!({"code": code, "message": message.into()})
    }

    fn next_desktop_request_id(&mut self) -> u32 {
        self.desktop_request_id = self.desktop_request_id.wrapping_add(1);
        if self.desktop_request_id == 0 {
            self.desktop_request_id = 1;
        }
        self.desktop_request_id
    }

    fn validate_echo_desk_settings(settings: &Value, partial: bool) -> Result<(), Value> {
        let Some(object) = settings.as_object() else {
            return Err(Self::desktop_error(
                "invalid_request",
                "settings must be an object",
            ));
        };
        const REQUIRED: [&str; 5] = [
            "theme",
            "brightness",
            "nowPlayingLayout",
            "nowPlayingBackground",
            "nowPlayingBackgroundCustomized",
        ];
        const OPTIONAL: [&str; 6] = [
            "defaultApp",
            "clockFormat",
            "timezone",
            "codexPercentageMode",
            "codexPreferredDisplay",
            "performanceDefaultView",
        ];
        let supported = |key: &str| REQUIRED.contains(&key) || OPTIONAL.contains(&key);
        if object.keys().any(|key| !supported(key)) {
            return Err(Self::desktop_error(
                "unsupported",
                "settings contains an unsupported field",
            ));
        }
        // The five original fields remain mandatory so Desktop can continue to
        // manage older EchoDesk OS releases. Newer firmware advertises the six
        // optional controls and may return all eleven fields.
        if !partial && REQUIRED.iter().any(|key| !object.contains_key(*key)) {
            return Err(Self::desktop_error(
                "invalid_state",
                "firmware settings are incomplete",
            ));
        }
        if let Some(value) = object.get("theme") {
            if !matches!(value.as_str(), Some("light" | "dark")) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "theme must be light or dark",
                ));
            }
        }
        if let Some(value) = object.get("brightness") {
            if !value
                .as_u64()
                .is_some_and(|value| (10..=100).contains(&value))
            {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "brightness must be 10..100",
                ));
            }
        }
        if let Some(value) = object.get("nowPlayingLayout") {
            if !matches!(value.as_str(), Some("albumCover" | "carPlay" | "echoDesk")) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "invalid Now Playing layout",
                ));
            }
        }
        if let Some(value) = object.get("nowPlayingBackground") {
            if !matches!(
                value.as_str(),
                Some("blurredAlbumCover" | "echoDesk" | "audioBars")
            ) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "invalid Now Playing background",
                ));
            }
        }
        if object
            .get("nowPlayingBackgroundCustomized")
            .is_some_and(|value| !value.is_boolean())
        {
            return Err(Self::desktop_error(
                "invalid_request",
                "customized must be boolean",
            ));
        }
        if let Some(value) = object.get("defaultApp") {
            if !matches!(
                value.as_str(),
                Some(
                    "home"
                        | "codex"
                        | "clock"
                        | "performance"
                        | "nowPlaying"
                        | "myDevice"
                        | "notificationStack"
                        | "settings"
                )
            ) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "default app is invalid",
                ));
            }
        }
        if let Some(value) = object.get("clockFormat") {
            if !matches!(value.as_str(), Some("12h" | "24h")) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "clock format is invalid",
                ));
            }
        }
        if let Some(value) = object.get("timezone") {
            let Some(identifier) = value.as_str() else {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "timezone must be an IANA identifier",
                ));
            };
            if !timezone::is_valid(identifier) || identifier == timezone::AUTO {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "timezone is invalid",
                ));
            }
        }
        if let Some(value) = object.get("codexPercentageMode") {
            if !matches!(value.as_str(), Some("used" | "remaining")) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "Codex percentage mode is invalid",
                ));
            }
        }
        if let Some(value) = object.get("codexPreferredDisplay") {
            if !matches!(value.as_str(), Some("both" | "session" | "weekly")) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "preferred Codex display is invalid",
                ));
            }
        }
        if let Some(value) = object.get("performanceDefaultView") {
            if !matches!(value.as_str(), Some("overview" | "cpu" | "gpu" | "memory")) {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "performance default view is invalid",
                ));
            }
        }
        Ok(())
    }

    fn store_echo_desk_state(&mut self, mut value: Value) -> Result<Value, Value> {
        if value.to_string().len() > 8192 || value.get("version").and_then(Value::as_u64) != Some(1)
        {
            return Err(Self::desktop_error(
                "invalid_state",
                "invalid EchoDesk state version or size",
            ));
        }
        let revision = value
            .get("revision")
            .and_then(Value::as_u64)
            .ok_or_else(|| Self::desktop_error("invalid_state", "state revision is missing"))?;
        let active = value
            .get("activeApp")
            .and_then(Value::as_str)
            .ok_or_else(|| Self::desktop_error("invalid_state", "active app is missing"))?;
        const APPS: [&str; 9] = [
            "home",
            "codex",
            "clock",
            "performance",
            "nowPlaying",
            "myDevice",
            "notificationStack",
            "pcDashboard",
            "settings",
        ];
        if !APPS.contains(&active) {
            return Err(Self::desktop_error(
                "invalid_state",
                "active app is invalid",
            ));
        }
        let supported = value
            .get("supportedApps")
            .and_then(Value::as_array)
            .filter(|apps| {
                !apps.is_empty()
                    && apps.len() <= APPS.len()
                    && apps
                        .iter()
                        .all(|app| app.as_str().is_some_and(|app| APPS.contains(&app)))
            })
            .ok_or_else(|| Self::desktop_error("invalid_state", "supported apps are invalid"))?;
        let _ = supported;
        for field in ["deviceName", "hardwareModel", "firmwareVersion"] {
            if !value
                .get(field)
                .and_then(Value::as_str)
                .is_some_and(|text| !text.is_empty() && text.len() <= 80)
            {
                return Err(Self::desktop_error(
                    "invalid_state",
                    format!("{field} is invalid"),
                ));
            }
        }
        Self::validate_setup_state(&value)?;
        Self::validate_echo_desk_settings(value.get("settings").unwrap_or(&Value::Null), false)?;
        if !value
            .get("unsupportedSettings")
            .and_then(Value::as_array)
            .is_some_and(|items| {
                items.len() <= 16
                    && items.iter().all(|item| {
                        item.as_str()
                            .is_some_and(|text| !text.is_empty() && text.len() <= 64)
                    })
            })
        {
            return Err(Self::desktop_error(
                "invalid_state",
                "unsupported settings list is invalid",
            ));
        }

        let state = self.app.state::<AppState>();
        let mut cached = state.echo_desk_state.lock().unwrap();
        if cached
            .as_ref()
            .and_then(|current| current.get("revision"))
            .and_then(Value::as_u64)
            .is_some_and(|current| revision < current)
        {
            return Err(Self::desktop_error(
                "stale_state",
                "older device state ignored",
            ));
        }
        if let Some(object) = value.as_object_mut() {
            object.remove("type");
            object.remove("version");
            object.remove("requestId");
        }
        let changed = cached.as_ref() != Some(&value);
        *cached = Some(value.clone());
        if changed {
            state
                .desktop_broker_revision
                .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
        }
        Ok(value)
    }

    fn validate_setup_state(value: &Value) -> Result<(), Value> {
        let setup_state = value.get("setupState");
        let setup_version = value.get("setupVersion");
        match (setup_state, setup_version) {
            (None, None) => return Ok(()), // EchoDesk OS before guided setup v1.
            (Some(state), Some(version)) => {
                if !matches!(
                    state.as_str(),
                    Some("notConfigured" | "waitingForDesktop" | "inProgress" | "completed")
                ) || version.as_u64() != Some(1)
                {
                    return Err(Self::desktop_error(
                        "invalid_state",
                        "setup state/version is invalid",
                    ));
                }
            }
            _ => {
                return Err(Self::desktop_error(
                    "invalid_state",
                    "setup state and version must be reported together",
                ));
            }
        }
        if let Some(url) = value.get("setupUrl") {
            if !(url.is_null()
                || url
                    .as_str()
                    .is_some_and(|url| url.len() <= 256 && url.starts_with("https://")))
            {
                return Err(Self::desktop_error(
                    "invalid_state",
                    "setup URL must be null or HTTPS",
                ));
            }
        }
        Ok(())
    }

    fn sync_profile_from_echo_state(&mut self, persist: bool) {
        let app_state = self.app.state::<AppState>();
        let state = app_state.echo_desk_state.lock().unwrap().clone();
        let Some(settings) = state.as_ref().and_then(|state| state.get("settings")) else {
            return;
        };
        let Some(theme) = settings.get("theme").and_then(Value::as_str) else {
            return;
        };
        let Some(brightness) = settings.get("brightness").and_then(Value::as_i64) else {
            return;
        };
        let theme = match theme {
            "dark" => ThemeSetting::Dark,
            "light" => ThemeSetting::Light,
            _ => return,
        };
        let profile = {
            let mut registry = app_state.registry.lock().unwrap();
            let Some(profile) = registry.current_profile_mut() else {
                return;
            };
            profile.theme = theme;
            profile.brightness = brightness;
            let profile = profile.clone();
            if persist {
                registry::save(&self.app, &registry);
            }
            profile
        };
        self.profile = Some(profile);

        // The host owns displayTime/tzOffsetMinutes, so keep its IANA zone in
        // lockstep with the setting confirmed by the device. chrono-tz applies
        // daylight-saving changes for Australia/Melbourne and other IANA zones.
        if let Some(identifier) = settings.get("timezone").and_then(Value::as_str) {
            if timezone::is_valid(identifier) && identifier != timezone::AUTO {
                let mut host_settings = app_state.settings.lock().unwrap();
                if host_settings.timezone != identifier {
                    host_settings.timezone = identifier.to_string();
                    if persist {
                        host_settings.save(&self.app);
                    }
                }
            }
        }
    }

    fn exchange_echo_desk(
        &mut self,
        command: &str,
        mut fields: serde_json::Map<String, Value>,
        expected: &[&str],
    ) -> Result<DeviceMessage, Value> {
        let supported = matches!(
            &self.state,
            LinkState::Connected(info) if info.supports_echo_desk_control()
        );
        if !supported {
            return Err(Self::desktop_error(
                "unsupported",
                "Connected firmware does not support EchoDesk Desktop control",
            ));
        }
        let request_id = self.next_desktop_request_id();
        fields.insert("cmd".into(), Value::String(command.into()));
        fields.insert("version".into(), json!(1));
        fields.insert("requestId".into(), json!(request_id));
        let mut line = Value::Object(fields).to_string();
        if line.len() > 2048 {
            return Err(Self::desktop_error(
                "invalid_request",
                "device command is too large",
            ));
        }
        line.push('\n');
        self.log_event(format!("-> {command} requestId={request_id}"));
        let result = self
            .link
            .as_mut()
            .ok_or_else(|| Self::desktop_error("disconnected", "EchoDesk is not connected"))?
            .command_with_responses(&line, expected, Duration::from_secs(2));
        self.absorb_link_log();
        let response = result
            .map_err(|error| Self::desktop_error("serial_error", error.to_string()))?
            .ok_or_else(|| Self::desktop_error("timeout", "EchoDesk command timed out"))?;
        let value = match &response {
            DeviceMessage::EchoDeskState(value)
            | DeviceMessage::EchoDeskClockState(value)
            | DeviceMessage::EchoDeskNotificationsState(value)
            | DeviceMessage::EchoDeskPcDashboardState(value)
            | DeviceMessage::EchoDeskAck(value)
            | DeviceMessage::EchoDeskError(value) => value,
            _ => {
                return Err(Self::desktop_error(
                    "invalid_response",
                    "unexpected device response",
                ))
            }
        };
        if value.get("version").and_then(Value::as_u64) != Some(1)
            || value.get("requestId").and_then(Value::as_u64) != Some(request_id as u64)
        {
            return Err(Self::desktop_error(
                "invalid_response",
                "device response id/version mismatch",
            ));
        }
        Ok(response)
    }

    fn refresh_echo_desk_state(&mut self) -> Result<Value, Value> {
        let response = self.exchange_echo_desk(
            "echodesk_get_state",
            serde_json::Map::new(),
            &["echodesk_state", "echodesk_error"],
        )?;
        match response {
            DeviceMessage::EchoDeskState(value) => self.store_echo_desk_state(value),
            DeviceMessage::EchoDeskError(value) => Err(value),
            _ => Err(Self::desktop_error(
                "invalid_response",
                "state response expected",
            )),
        }
    }

    fn validate_clock_state(value: &Value) -> bool {
        let status = |node: &Value| {
            node.get("status")
                .and_then(Value::as_str)
                .is_some_and(|status| matches!(status, "idle" | "running" | "paused" | "complete"))
        };
        let bounded = |node: &Value, key: &str, maximum: u64| {
            node.get(key)
                .and_then(Value::as_u64)
                .is_some_and(|value| value <= maximum)
        };
        let stopwatch = value.get("stopwatch").unwrap_or(&Value::Null);
        let countdown = value.get("countdown").unwrap_or(&Value::Null);
        let focus = value.get("focus").unwrap_or(&Value::Null);
        matches!(
            value.get("activeMode").and_then(Value::as_str),
            Some("time" | "stopwatch" | "countdown" | "focus")
        ) && status(stopwatch)
            && bounded(stopwatch, "elapsedMs", 7 * 24 * 60 * 60 * 1000)
            && status(countdown)
            && bounded(countdown, "durationSeconds", 86400)
            && bounded(countdown, "remainingSeconds", 86400)
            && bounded(countdown, "progressPermille", 1000)
            && status(focus)
            && matches!(
                focus.get("phase").and_then(Value::as_str),
                Some("focus" | "break" | "complete")
            )
            && bounded(focus, "focusMinutes", 180)
            && bounded(focus, "breakMinutes", 60)
            && bounded(focus, "rounds", 12)
            && bounded(focus, "currentRound", 12)
            && bounded(focus, "remainingSeconds", 180 * 60)
            && bounded(focus, "progressPermille", 1000)
    }

    fn store_clock_state(&mut self, mut value: Value) -> Result<Value, Value> {
        if value.to_string().len() > 4096
            || value.get("version").and_then(Value::as_u64) != Some(1)
            || value.get("revision").and_then(Value::as_u64).is_none()
            || !Self::validate_clock_state(&value)
        {
            return Err(Self::desktop_error(
                "invalid_state",
                "invalid EchoDesk clock state",
            ));
        }
        if let Some(object) = value.as_object_mut() {
            object.remove("type");
            object.remove("version");
            object.remove("requestId");
        }
        let state = self.app.state::<AppState>();
        let mut cached = state.echo_desk_clock_state.lock().unwrap();
        let incoming_revision = value.get("revision").and_then(Value::as_u64).unwrap_or(0);
        if cached
            .as_ref()
            .and_then(|current| current.get("revision"))
            .and_then(Value::as_u64)
            .is_some_and(|current| incoming_revision < current)
        {
            return Err(Self::desktop_error(
                "stale_state",
                "older clock state ignored",
            ));
        }
        let changed = cached.as_ref() != Some(&value);
        *cached = Some(value.clone());
        if changed {
            state
                .desktop_broker_revision
                .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
        }
        Ok(value)
    }

    fn refresh_clock_state(&mut self) -> Result<Value, Value> {
        if !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_clock()) {
            return Err(Self::desktop_error(
                "unsupported",
                "Connected firmware does not support Clock controls",
            ));
        }
        let response = self.exchange_echo_desk(
            "echodesk_clock_get_state",
            serde_json::Map::new(),
            &["echodesk_clock_state", "echodesk_error"],
        )?;
        match response {
            DeviceMessage::EchoDeskClockState(value) => self.store_clock_state(value),
            DeviceMessage::EchoDeskError(value) => Err(value),
            _ => Err(Self::desktop_error(
                "invalid_response",
                "clock state response expected",
            )),
        }
    }

    fn valid_pc_dashboard_metrics(value: &Value) -> bool {
        const METRICS: [&str; 11] = [
            "battery",
            "microphone",
            "storage",
            "ram",
            "cpu",
            "gpu",
            "networkDown",
            "networkUp",
            "cpuTemperature",
            "gpuTemperature",
            "motherboardTemperature",
        ];
        let Some(metrics) = value.as_array() else {
            return false;
        };
        if !(1..=4).contains(&metrics.len()) {
            return false;
        }
        let mut seen = std::collections::HashSet::new();
        metrics.iter().all(|metric| {
            metric
                .as_str()
                .is_some_and(|id| METRICS.contains(&id) && seen.insert(id))
        })
    }

    fn store_pc_dashboard_state(&mut self, mut value: Value) -> Result<Value, Value> {
        if value.to_string().len() > 1024
            || value.get("version").and_then(Value::as_u64) != Some(1)
            || value.get("revision").and_then(Value::as_u64).is_none()
            || !value
                .get("metrics")
                .is_some_and(Self::valid_pc_dashboard_metrics)
        {
            return Err(Self::desktop_error(
                "invalid_state",
                "invalid PC Dashboard state",
            ));
        }
        if let Some(object) = value.as_object_mut() {
            object.remove("type");
            object.remove("version");
            object.remove("requestId");
        }
        let state = self.app.state::<AppState>();
        let mut cached = state.echo_desk_pc_dashboard_state.lock().unwrap();
        let incoming = value.get("revision").and_then(Value::as_u64).unwrap_or(0);
        if cached
            .as_ref()
            .and_then(|v| v.get("revision"))
            .and_then(Value::as_u64)
            .is_some_and(|v| incoming < v)
        {
            return Err(Self::desktop_error(
                "stale_state",
                "older PC Dashboard state ignored",
            ));
        }
        let changed = cached.as_ref() != Some(&value);
        *cached = Some(value.clone());
        if changed {
            state
                .desktop_broker_revision
                .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
        }
        Ok(value)
    }

    fn refresh_pc_dashboard_state(&mut self) -> Result<Value, Value> {
        if !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_pc_dashboard())
        {
            return Err(Self::desktop_error(
                "unsupported",
                "Connected firmware does not support PC Dashboard",
            ));
        }
        let response = self.exchange_echo_desk(
            "echodesk_pc_dashboard_get_state",
            serde_json::Map::new(),
            &["echodesk_pc_dashboard_state", "echodesk_error"],
        )?;
        match response {
            DeviceMessage::EchoDeskPcDashboardState(value) => self.store_pc_dashboard_state(value),
            DeviceMessage::EchoDeskError(value) => Err(value),
            _ => Err(Self::desktop_error(
                "invalid_response",
                "PC Dashboard state expected",
            )),
        }
    }

    fn validate_notification_state(value: &Value) -> bool {
        let Some(items) = value.get("notifications").and_then(Value::as_array) else {
            return false;
        };
        if items.len() > 12 || value.get("enabled").and_then(Value::as_bool).is_none() {
            return false;
        }
        items.iter().all(|item| {
            let bounded = |key: &str, max: usize| {
                item.get(key)
                    .and_then(Value::as_str)
                    .is_some_and(|s| s.len() <= max)
            };
            item.get("id")
                .and_then(Value::as_u64)
                .is_some_and(|id| id > 0 && id <= u32::MAX as u64)
                && item.get("timestamp").and_then(Value::as_u64).is_some()
                && bounded("source", 32)
                && bounded("title", 64)
                && bounded("body", 160)
        })
    }

    fn store_notification_state(&mut self, mut value: Value) -> Result<Value, Value> {
        if value.to_string().len() > 4096
            || value.get("version").and_then(Value::as_u64) != Some(1)
            || value.get("revision").and_then(Value::as_u64).is_none()
            || !Self::validate_notification_state(&value)
        {
            return Err(Self::desktop_error(
                "invalid_state",
                "invalid EchoDesk notification state",
            ));
        }
        if let Some(object) = value.as_object_mut() {
            object.remove("type");
            object.remove("version");
            object.remove("requestId");
        }
        let state = self.app.state::<AppState>();
        let mut cached = state.echo_desk_notification_state.lock().unwrap();
        let incoming = value.get("revision").and_then(Value::as_u64).unwrap_or(0);
        if cached
            .as_ref()
            .and_then(|v| v.get("revision"))
            .and_then(Value::as_u64)
            .is_some_and(|current| incoming < current)
        {
            return Err(Self::desktop_error(
                "stale_state",
                "older notification state ignored",
            ));
        }
        let changed = cached.as_ref() != Some(&value);
        *cached = Some(value.clone());
        if changed {
            state
                .desktop_broker_revision
                .fetch_add(1, std::sync::atomic::Ordering::AcqRel);
        }
        Ok(value)
    }

    fn refresh_notification_state(&mut self) -> Result<Value, Value> {
        if !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_notifications())
        {
            return Err(Self::desktop_error(
                "unsupported",
                "Connected firmware does not support Notification Stack",
            ));
        }
        match self.exchange_echo_desk(
            "echodesk_notifications_get_state",
            serde_json::Map::new(),
            &["echodesk_notifications_state", "echodesk_error"],
        )? {
            DeviceMessage::EchoDeskNotificationsState(value) => {
                self.store_notification_state(value)
            }
            DeviceMessage::EchoDeskError(value) => Err(value),
            _ => Err(Self::desktop_error(
                "invalid_response",
                "notification state response expected",
            )),
        }
    }

    fn echo_desk_control(&mut self, method: &str, params: Value) -> Result<Value, Value> {
        if method == "notifications_get_state" {
            return self.refresh_notification_state();
        }
        if matches!(
            method,
            "notifications_set_enabled"
                | "notifications_push"
                | "notifications_dismiss"
                | "notifications_clear"
        ) {
            if !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_notifications())
            {
                return Err(Self::desktop_error(
                    "unsupported",
                    "Connected firmware does not support Notification Stack",
                ));
            }
            let object = params.as_object().ok_or_else(|| {
                Self::desktop_error("invalid_request", "notification params must be an object")
            })?;
            let valid = match method {
                "notifications_set_enabled" => {
                    object.len() == 1 && object.get("enabled").and_then(Value::as_bool).is_some()
                }
                "notifications_push" => {
                    object.len() == 5
                        && object
                            .get("id")
                            .and_then(Value::as_u64)
                            .is_some_and(|v| v > 0 && v <= u32::MAX as u64)
                        && object.get("timestamp").and_then(Value::as_u64).is_some()
                        && object
                            .get("source")
                            .and_then(Value::as_str)
                            .is_some_and(|v| !v.is_empty() && v.len() <= 32)
                        && object
                            .get("title")
                            .and_then(Value::as_str)
                            .is_some_and(|v| !v.is_empty() && v.len() <= 64)
                        && object
                            .get("body")
                            .and_then(Value::as_str)
                            .is_some_and(|v| v.len() <= 160)
                }
                "notifications_dismiss" => {
                    object.len() == 1
                        && object
                            .get("id")
                            .and_then(Value::as_u64)
                            .is_some_and(|v| v > 0 && v <= u32::MAX as u64)
                }
                "notifications_clear" => object.is_empty(),
                _ => false,
            };
            if !valid {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "notification command fields are invalid",
                ));
            }
            let command = match method {
                "notifications_set_enabled" => "echodesk_notifications_set_enabled",
                "notifications_push" => "echodesk_notifications_push",
                "notifications_dismiss" => "echodesk_notifications_dismiss",
                _ => "echodesk_notifications_clear",
            };
            let response = self.exchange_echo_desk(
                command,
                object.clone(),
                &["echodesk_ack", "echodesk_error"],
            )?;
            match response {
                DeviceMessage::EchoDeskError(value) => return Err(value),
                DeviceMessage::EchoDeskAck(value)
                    if value.get("command").and_then(Value::as_str) == Some(command) => {}
                _ => {
                    return Err(Self::desktop_error(
                        "invalid_response",
                        "notification acknowledgement expected",
                    ))
                }
            }
            return self.refresh_notification_state();
        }
        if method == "clock_get_state" {
            return self.refresh_clock_state();
        }
        if method == "pc_dashboard_get_state" {
            return self.refresh_pc_dashboard_state();
        }
        if method == "pc_dashboard_set_metrics" {
            if !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_pc_dashboard())
            {
                return Err(Self::desktop_error(
                    "unsupported",
                    "Connected firmware does not support PC Dashboard",
                ));
            }
            let object = params.as_object().ok_or_else(|| {
                Self::desktop_error("invalid_request", "PC Dashboard params must be an object")
            })?;
            if object.len() != 2
                || object.get("baseRevision").and_then(Value::as_u64).is_none()
                || !object
                    .get("metrics")
                    .is_some_and(Self::valid_pc_dashboard_metrics)
            {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "PC Dashboard requires baseRevision and 1-4 unique metrics",
                ));
            }
            let mut fields = serde_json::Map::new();
            fields.insert("baseRevision".into(), object["baseRevision"].clone());
            fields.insert("metrics".into(), object["metrics"].clone());
            let response = self.exchange_echo_desk(
                "echodesk_pc_dashboard_set_config",
                fields,
                &["echodesk_ack", "echodesk_error"],
            )?;
            match response {
                DeviceMessage::EchoDeskError(value) => return Err(value),
                DeviceMessage::EchoDeskAck(value)
                    if value.get("command").and_then(Value::as_str)
                        == Some("echodesk_pc_dashboard_set_config") => {}
                _ => {
                    return Err(Self::desktop_error(
                        "invalid_response",
                        "PC Dashboard acknowledgement expected",
                    ))
                }
            }
            return self.refresh_pc_dashboard_state();
        }
        if method == "clock_control" {
            if !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_clock())
            {
                return Err(Self::desktop_error(
                    "unsupported",
                    "Connected firmware does not support Clock controls",
                ));
            }
            let object = params.as_object().ok_or_else(|| {
                Self::desktop_error("invalid_request", "clock params must be an object")
            })?;
            let action = object
                .get("action")
                .and_then(Value::as_str)
                .filter(|value| {
                    matches!(
                        *value,
                        "select" | "start" | "pause" | "reset" | "skip" | "configure"
                    )
                })
                .ok_or_else(|| Self::desktop_error("invalid_request", "clock action is invalid"))?;
            let mode = object
                .get("mode")
                .and_then(Value::as_str)
                .filter(|value| matches!(*value, "time" | "stopwatch" | "countdown" | "focus"))
                .ok_or_else(|| Self::desktop_error("invalid_request", "clock mode is invalid"))?;
            let allowed = match action {
                "configure" if mode == "countdown" => {
                    object.len() == 3
                        && object
                            .get("durationSeconds")
                            .and_then(Value::as_u64)
                            .is_some_and(|value| (1..=86400).contains(&value))
                }
                "configure" if mode == "focus" => {
                    object.len() == 5
                        && object
                            .get("focusMinutes")
                            .and_then(Value::as_u64)
                            .is_some_and(|value| (1..=180).contains(&value))
                        && object
                            .get("breakMinutes")
                            .and_then(Value::as_u64)
                            .is_some_and(|value| (1..=60).contains(&value))
                        && object
                            .get("rounds")
                            .and_then(Value::as_u64)
                            .is_some_and(|value| (1..=12).contains(&value))
                }
                "configure" => false,
                _ => object.len() == 2,
            };
            if !allowed {
                return Err(Self::desktop_error(
                    "invalid_request",
                    "clock control fields are invalid",
                ));
            }
            let mut fields = serde_json::Map::new();
            for (key, value) in object {
                fields.insert(key.clone(), value.clone());
            }
            let response = self.exchange_echo_desk(
                "echodesk_clock_control",
                fields,
                &["echodesk_ack", "echodesk_error"],
            )?;
            match response {
                DeviceMessage::EchoDeskError(value) => return Err(value),
                DeviceMessage::EchoDeskAck(value)
                    if value.get("command").and_then(Value::as_str)
                        == Some("echodesk_clock_control") => {}
                _ => {
                    return Err(Self::desktop_error(
                        "invalid_response",
                        "clock acknowledgement expected",
                    ))
                }
            }
            self.app
                .state::<AppState>()
                .echo_desk_clock_config_request
                .lock()
                .unwrap()
                .take();
            return self.refresh_clock_state();
        }
        let mut fields = serde_json::Map::new();
        let mut persist_profile = false;
        if matches!(method, "prepare_update" | "update_complete")
            && !matches!(&self.state, LinkState::Connected(info) if info.supports_echo_desk_update())
        {
            return Err(Self::desktop_error(
                "unsupported",
                "Connected firmware does not support EchoDesk OS update screens",
            ));
        }
        let command = match method {
            "get_state" => return self.refresh_echo_desk_state(),
            "open_app" => {
                let object = params.as_object().ok_or_else(|| {
                    Self::desktop_error("invalid_request", "params must be an object")
                })?;
                if object.len() != 1 {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "open_app accepts only app",
                    ));
                }
                let app = object
                    .get("app")
                    .and_then(Value::as_str)
                    .filter(|app| {
                        matches!(
                            *app,
                            "home"
                                | "codex"
                                | "clock"
                                | "performance"
                                | "nowPlaying"
                                | "myDevice"
                                | "notificationStack"
                                | "pcDashboard"
                                | "settings"
                        )
                    })
                    .ok_or_else(|| Self::desktop_error("invalid_request", "app is invalid"))?;
                fields.insert("app".into(), Value::String(app.into()));
                "echodesk_open_app"
            }
            "set_settings" => {
                let object = params.as_object().ok_or_else(|| {
                    Self::desktop_error("invalid_request", "params must be an object")
                })?;
                if object
                    .keys()
                    .any(|key| !matches!(key.as_str(), "baseRevision" | "persist" | "settings"))
                    || object.len() != 3
                {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "set_settings fields are invalid",
                    ));
                }
                let base_revision = object
                    .get("baseRevision")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| {
                        Self::desktop_error("invalid_request", "baseRevision is required")
                    })?;
                let persist = object
                    .get("persist")
                    .and_then(Value::as_bool)
                    .ok_or_else(|| Self::desktop_error("invalid_request", "persist is required"))?;
                persist_profile = persist;
                let settings = object.get("settings").cloned().unwrap_or(Value::Null);
                Self::validate_echo_desk_settings(&settings, true)?;
                if settings
                    .as_object()
                    .is_some_and(|settings| settings.is_empty())
                {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "settings cannot be empty",
                    ));
                }
                if !persist
                    && settings.as_object().is_some_and(|settings| {
                        settings.len() != 1 || !settings.contains_key("brightness")
                    })
                {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "non-persistent preview is brightness-only",
                    ));
                }
                fields.insert("baseRevision".into(), json!(base_revision));
                fields.insert("persist".into(), json!(persist));
                fields.insert("settings".into(), settings);
                "echodesk_set_settings"
            }
            "restart" => {
                if !(params.is_null() || params.as_object().is_some_and(|params| params.is_empty()))
                {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "restart takes no parameters",
                    ));
                }
                "echodesk_restart"
            }
            "setup_begin" | "setup_complete" | "factory_reset" => {
                if !(params.is_null() || params.as_object().is_some_and(|params| params.is_empty()))
                {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "setup commands take no device parameters",
                    ));
                }
                match method {
                    "setup_begin" => "echodesk_setup_begin",
                    "setup_complete" => "echodesk_setup_complete",
                    _ => "echodesk_factory_reset",
                }
            }
            "prepare_update" | "update_complete" => {
                if !(params.is_null() || params.as_object().is_some_and(|params| params.is_empty()))
                {
                    return Err(Self::desktop_error(
                        "invalid_request",
                        "update display commands take no device parameters",
                    ));
                }
                if method == "prepare_update" {
                    "echodesk_prepare_update"
                } else {
                    "echodesk_update_complete"
                }
            }
            _ => {
                return Err(Self::desktop_error(
                    "unsupported",
                    "desktop method is not allowlisted",
                ))
            }
        };

        let response =
            self.exchange_echo_desk(command, fields, &["echodesk_ack", "echodesk_error"])?;
        match response {
            DeviceMessage::EchoDeskError(value) => Err(value),
            DeviceMessage::EchoDeskAck(value) if echo_ack_is_terminal(method) => {
                if !update_ack_matches(method, &value) {
                    Err(Self::desktop_error(
                        "invalid_response",
                        "EchoDesk update acknowledgement command mismatch",
                    ))
                } else {
                    Ok(value)
                }
            }
            DeviceMessage::EchoDeskAck(_) => {
                let state = self.refresh_echo_desk_state()?;
                self.sync_profile_from_echo_state(persist_profile);
                Ok(state)
            }
            _ => Err(Self::desktop_error(
                "invalid_response",
                "acknowledgement expected",
            )),
        }
    }

    fn send_data_frame(&mut self, trigger: &str) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        let info = info.clone();
        let selected = current_snapshot(&self.app);
        let settings = self
            .app
            .state::<AppState>()
            .settings
            .lock()
            .unwrap()
            .clone();
        if !info.supports_views() {
            if !settings.ai_usage_enabled {
                return;
            }
            if let Some((payload, kind, frame_id)) = self.build_payload(&info, selected, None) {
                self.transmit(&info, payload, frame_id, kind, trigger);
            }
            return;
        }
        let views = settings.views.clone();
        if views.iter().any(|v| matches!(v, ViewContent::Plugin(_)))
            && !info.supports_plugin_scenes()
        {
            return;
        }
        let cached = self
            .app
            .state::<AppState>()
            .view_sources
            .lock()
            .unwrap()
            .clone();
        for (index, view) in views.iter().enumerate() {
            if let ViewContent::Plugin(id) = view {
                if !info.supports_plugin_scenes() {
                    continue;
                }
                let orientation = self
                    .profile
                    .as_ref()
                    .map(|profile| profile.orientation)
                    .or(info.orientation);
                let language = self
                    .profile
                    .as_ref()
                    .map(|p| p.language)
                    .unwrap_or_default();
                let theme = self
                    .profile
                    .as_ref()
                    .map(|profile| profile.theme)
                    .unwrap_or_default()
                    .resolve(system_is_dark());
                let layout = if info.display == Some(DisplayVariant::St7701) {
                    SceneLayout::Square
                } else if matches!(
                    orientation,
                    Some(Orientation::LandscapeLeft | Orientation::LandscapeRight)
                ) {
                    SceneLayout::Landscape
                } else {
                    SceneLayout::Portrait
                };
                let scene = if id == aimonitor_core::claude_code::VIEW_ID {
                    crate::claude_code::scene(&self.app, language, theme, layout)
                } else {
                    self.app
                        .state::<AppState>()
                        .plugins
                        .lock()
                        .unwrap()
                        .records
                        .get(id)
                        .map(|record| record.scene(layout, language, theme))
                        .unwrap_or_else(|| {
                            status_scene_with_theme(
                                status_text("missing", language),
                                status_text("missing.hint", language),
                                theme,
                            )
                        })
                };
                let frame_id = self.frame_ids.next();
                match scene_envelope(id, index, scene, frame_id) {
                    Ok(payload) => self.transmit(&info, payload, frame_id, "plugin", trigger),
                    Err(e) => self.log_event(format!("Plugin-Szene {id} nicht gesendet: {e}")),
                }
                if !matches!(self.state, LinkState::Connected(_)) {
                    break;
                }
                continue;
            }
            let ViewContent::Provider(provider) = view else {
                continue;
            };
            if !settings.ai_usage_enabled {
                continue;
            }
            // Nach einem Wechsel lädt die Haupt-Source noch; bis dahin den
            // Fensterstand senden, damit die angezeigten Werte stehen bleiben.
            let loading =
                selected.entry.is_none() && selected.fetching && cached.contains_key(provider);
            let snap = if *provider == selected.provider && !loading {
                Some(selected.clone())
            } else {
                cached.get(provider).cloned()
            };
            let Some(snap) = snap else { continue };
            if let Some((payload, kind, frame_id)) = self.build_payload(&info, snap, Some(index)) {
                self.transmit(&info, payload, frame_id, kind, trigger);
            }
            if !matches!(self.state, LinkState::Connected(_)) {
                break;
            }
        }
    }

    fn send_diagnostic_frame(&mut self) {
        let LinkState::Connected(info) = &self.state else {
            self.log_event("Testframe nicht gesendet, kein Gerät verbunden");
            self.publish();
            return;
        };
        let info = info.clone();
        let snap = current_snapshot(&self.app);
        let ctx = self.frame_context(&snap);
        let frame_id = self.frame_ids.next();
        let payload = diagnostic_envelope(snap.provider, &ctx, frame_id);
        self.send_due = None;
        self.diagnostic_until = Some(Instant::now() + DIAGNOSTIC_RESTORE);
        self.transmit(&info, payload, frame_id, "diagnostic", "Testframe");
    }

    fn send_performance_frame(&mut self, sample: PerformanceSample) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !info.supports_performance() || !info.supports_framed() {
            return;
        }
        let info = info.clone();
        let frame_id = self.frame_ids.next();
        match sample.payload(frame_id) {
            Ok(payload) => {
                self.transmit(&info, payload, frame_id, "performance", "Hardware helper")
            }
            Err(e) => self.log_event(format!("Performance frame rejected: {e}")),
        }
        if info.supports_echo_desk_pc_dashboard() {
            let dashboard_frame_id = self.frame_ids.next();
            match sample.pc_dashboard_payload(dashboard_frame_id) {
                Ok(Some(payload)) => self.transmit(
                    &info,
                    payload,
                    dashboard_frame_id,
                    "pc-dashboard",
                    "Windows system APIs",
                ),
                Ok(None) => {}
                Err(e) => self.log_event(format!("PC Dashboard frame rejected: {e}")),
            }
        }
    }

    fn send_music_frame(&mut self, snapshot: MusicSnapshot) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !info.supports_music() || !info.supports_framed() {
            return;
        }
        let info = info.clone();
        let frame_id = self.frame_ids.next();
        match snapshot.payload(frame_id) {
            Ok(payload) => {
                self.transmit(&info, payload, frame_id, "music", "Windows GSMTC");
                match snapshot.artwork {
                    Some(art)
                        if self.last_music_art_id.as_deref() != Some(art.id.as_str())
                            && self
                                .pending_music_art
                                .as_ref()
                                .is_none_or(|pending| pending.art.id != art.id) =>
                    {
                        self.pending_music_art = Some(PendingMusicArt { art, next_chunk: 0 });
                    }
                    None => self.pending_music_art = None,
                    _ => {}
                }
                if info.supports_music_background() && self.music_background_requested {
                    match snapshot.background {
                        Some(background)
                            if self.last_music_background_id.as_deref()
                                != Some(background.id.as_str())
                                && self.pending_music_background.as_ref().is_none_or(
                                    |pending| pending.background.id != background.id,
                                ) =>
                        {
                            self.pending_music_background = Some(PendingMusicBackground {
                                background,
                                next_chunk: 0,
                            });
                        }
                        None => self.pending_music_background = None,
                        _ => {}
                    }
                } else {
                    self.pending_music_background = None;
                }
            }
            Err(error) => self.log_event(format!("Music frame rejected: {error}")),
        }
    }

    fn send_audio_visualizer_frame(&mut self, snapshot: VisualizerSnapshot) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !info.supports_music_visualizer() || !info.supports_framed() {
            self.app
                .state::<AppState>()
                .audio_visualizer
                .set_enabled(false);
            return;
        }
        let info = info.clone();
        let frame_id = self.frame_ids.next();
        match snapshot.payload(frame_id) {
            Ok(payload) => self.transmit(
                &info,
                payload,
                frame_id,
                "music-visualizer",
                "WASAPI loopback",
            ),
            Err(error) => self.log_event(format!("Audio visualizer frame rejected: {error}")),
        }
    }

    /// Transmit at most one chunk per service iteration so normal usage and
    /// performance jobs remain responsive while artwork is in flight.
    fn send_pending_music_art(&mut self) {
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !info.supports_music() || !info.supports_framed() {
            self.pending_music_art = None;
            return;
        }
        let Some(pending) = self.pending_music_art.as_ref() else {
            return;
        };
        let chunks = pending.art.bytes.len().div_ceil(MUSIC_ART_CHUNK_BYTES);
        let art_id = pending.art.id.clone();
        let chunk_index = pending.next_chunk;
        let art = pending.art.clone();
        let info = info.clone();
        let frame_id = self.frame_ids.next();
        let payload = match art_chunk_payload(&art, frame_id, chunk_index) {
            Ok(payload) => payload,
            Err(error) => {
                self.log_event(format!("Music artwork rejected: {error}"));
                self.pending_music_art = None;
                return;
            }
        };
        self.transmit(&info, payload, frame_id, "music-art", "Windows GSMTC");
        match &self.last_receipt {
            Some(FrameReceipt::Ack { frame_id: id, .. }) if *id == frame_id => {}
            Some(FrameReceipt::Error { frame_id: id, .. }) if *id == frame_id => {
                self.pending_music_art = None;
                return;
            }
            _ => return,
        }
        if let Some(pending) = self.pending_music_art.as_mut() {
            if pending.art.id != art_id {
                return;
            }
            pending.next_chunk += 1;
            if pending.next_chunk >= chunks {
                self.last_music_art_id = Some(art_id);
                self.pending_music_art = None;
            }
        }
    }

    /// Backgrounds are already cropped, resized and blurred by the companion.
    /// They follow foreground artwork and still send at most one bounded chunk
    /// per service iteration, so higher-priority telemetry can interleave.
    fn send_pending_music_background(&mut self) {
        if self.pending_music_art.is_some() {
            return;
        }
        let LinkState::Connected(info) = &self.state else {
            return;
        };
        if !self.music_background_requested
            || !info.supports_music_background()
            || !info.supports_framed()
        {
            self.pending_music_background = None;
            return;
        }
        let Some(pending) = self.pending_music_background.as_ref() else {
            return;
        };
        let chunks = pending
            .background
            .bytes
            .len()
            .div_ceil(MUSIC_BACKGROUND_CHUNK_BYTES);
        let background_id = pending.background.id.clone();
        let chunk_index = pending.next_chunk;
        let background = pending.background.clone();
        let info = info.clone();
        let frame_id = self.frame_ids.next();
        let payload = match background_chunk_payload(&background, frame_id, chunk_index) {
            Ok(payload) => payload,
            Err(error) => {
                self.log_event(format!("Music background rejected: {error}"));
                self.pending_music_background = None;
                return;
            }
        };
        self.transmit(
            &info,
            payload,
            frame_id,
            "music-background",
            "Windows GSMTC artwork",
        );
        match &self.last_receipt {
            Some(FrameReceipt::Ack { frame_id: id, .. }) if *id == frame_id => {}
            Some(FrameReceipt::Error { frame_id: id, .. }) if *id == frame_id => {
                self.pending_music_background = None;
                return;
            }
            _ => return,
        }
        if let Some(pending) = self.pending_music_background.as_mut() {
            if pending.background.id != background_id {
                return;
            }
            pending.next_chunk += 1;
            if pending.next_chunk >= chunks {
                self.last_music_background_id = Some(background_id);
                self.pending_music_background = None;
            }
        }
    }

    fn transmit(
        &mut self,
        info: &DeviceInfo,
        payload: String,
        frame_id: i64,
        kind: &str,
        trigger: &str,
    ) {
        let Some(link) = self.link.as_mut() else {
            return;
        };
        let result = link.send_frame(
            &payload,
            frame_id,
            info.supports_framed(),
            info.max_frame_bytes(),
        );
        self.absorb_link_log();
        match result {
            Ok(receipt) => {
                self.frames_sent += 1;
                self.last_frame_bytes = Some(payload.len());
                self.last_frame_at = Some(Utc::now());
                let summary = match &receipt {
                    FrameReceipt::Ack { bytes, rows, .. } => {
                        format!("ack bytes={bytes} rows={rows}")
                    }
                    FrameReceipt::Error { message, .. } => format!("error: {message}"),
                    FrameReceipt::Timeout { .. } => "timeout, kein ACK".into(),
                };
                self.log_event(format!(
                    "Frame {frame_id} {kind} ({trigger}, {} B) -> {summary}",
                    payload.len()
                ));
                self.last_receipt = Some(receipt.clone());
                self.account_receipt(info, &receipt);
                self.publish();
            }
            Err(LinkError::Frame(e)) => {
                self.log_event(format!("Frame {frame_id} nicht gesendet: {e}"));
                self.publish();
            }
            Err(e) => {
                self.log_event(format!("Frame {frame_id} Schreibfehler: {e}"));
                self.disconnect("Schreibfehler");
            }
        }
    }

    /// ACK-Buchführung nach Spec 6.5.
    fn account_receipt(&mut self, info: &DeviceInfo, receipt: &FrameReceipt) {
        match receipt {
            FrameReceipt::Ack { .. } => {
                self.unacked = 0;
                self.frames_acked += 1;
            }
            FrameReceipt::Error { .. } => self.unacked = 0,
            FrameReceipt::Timeout { .. } => {
                if !info.supports_ack() {
                    return;
                }
                self.unacked += 1;
                if self.unacked < REPAIR_THRESHOLD {
                    return;
                }
                let cooled = self
                    .last_repair
                    .map(|t| t.elapsed() >= REPAIR_COOLDOWN)
                    .unwrap_or(true);
                if !cooled {
                    self.log_event(format!(
                        "{} unbestätigte Frames, Reparatur noch in Abkühlung",
                        self.unacked
                    ));
                    return;
                }
                self.log_event(format!(
                    "{} unbestätigte Frames in Folge, Verbindung wird neu aufgebaut",
                    self.unacked
                ));
                self.last_repair = Some(Instant::now());
                self.disconnect("Auto-Reparatur");
                self.next_scan = Instant::now() + REPAIR_RECONNECT_DELAY;
            }
        }
    }

    // -- Aufträge ----------------------------------------------------------

    /// Gibt `false` zurück, wenn der Thread enden soll.
    fn handle(&mut self, job: Job) -> bool {
        {
            let state = self.app.state::<AppState>();
            let settings = state.settings.lock().unwrap();
            self.smart.sync_revision(
                settings.smart_revision,
                settings.smart_manual,
                Instant::now(),
            );
        }
        match job {
            Job::Wifi { action, reply } => {
                let result = self.wifi_command(action);
                let _ = reply.send(result);
            }
            Job::SetManualPort(port) => {
                if self.manual_port != port {
                    self.log_event(match &port {
                        Some(p) => format!("Manueller Port: {p}"),
                        None => "Port: automatisch".into(),
                    });
                    self.manual_port = port;
                    let current = self.link.as_ref().map(|l| l.name().to_string());
                    let keep = match (&self.manual_port, &current) {
                        (Some(m), Some(c)) => m == c,
                        (None, Some(_)) => true,
                        _ => false,
                    };
                    if !keep {
                        self.disconnect("Portwahl geändert");
                    }
                    self.next_scan = Instant::now() + RECONNECT_BLOCK_WINDOW;
                    self.publish();
                }
            }
            Job::Resend => self.schedule_send(),
            Job::ConfigureViews => {
                self.configure_views();
                self.schedule_send();
            }
            Job::SmartTouch => self.smart.touch(Instant::now()),
            Job::ResetSmart => {
                let now = Instant::now();
                self.smart.reset(now);
                let state = self.app.state::<AppState>();
                let settings = state.settings.lock().unwrap().clone();
                let providers: Vec<_> = settings
                    .views
                    .iter()
                    .map(|view| match view {
                        ViewContent::Provider(p) => Some(*p),
                        _ => None,
                    })
                    .collect();
                let recent_usage = |snapshot: &Snapshot| {
                    !snapshot.fetching
                        && recent_sample(snapshot.fetched_at.as_ref(), POLL_INTERVAL * 2)
                };
                let selected = current_snapshot(&self.app);
                if settings.ai_usage_enabled
                    && providers.contains(&Some(selected.provider))
                    && selected.status.is_ok()
                    && recent_usage(&selected)
                {
                    if let Some(entry) = &selected.entry {
                        self.smart.observe_usage(
                            selected.provider,
                            usage_signals(entry),
                            &providers,
                            now,
                        );
                    }
                }
                if settings.ai_usage_enabled {
                    for (provider, snap) in state.view_sources.lock().unwrap().iter() {
                        if *provider == selected.provider || !providers.contains(&Some(*provider)) {
                            continue;
                        }
                        if snap.status.is_ok() && recent_usage(snap) {
                            if let Some(entry) = &snap.entry {
                                self.smart.observe_usage(
                                    *provider,
                                    usage_signals(entry),
                                    &providers,
                                    now,
                                );
                            }
                        }
                    }
                }
                let plugin_views: Vec<_> = settings
                    .views
                    .iter()
                    .map(|view| match view {
                        ViewContent::Plugin(id) => Some(id.as_str()),
                        _ => None,
                    })
                    .collect();
                for (id, record) in state.plugins.lock().unwrap().records.iter() {
                    let fresh = recent_sample(
                        record.fetched_at.as_ref(),
                        Duration::from_secs(3 * record.manifest.source.interval_seconds as u64),
                    );
                    if plugin_views.contains(&Some(id.as_str()))
                        && record.last_error.is_none()
                        && fresh
                    {
                        if let Some(data) = &record.data {
                            let max_age = Duration::from_secs(
                                3 * record.manifest.source.interval_seconds as u64,
                            );
                            let age = record
                                .fetched_at
                                .as_ref()
                                .and_then(|fetched| {
                                    Utc::now()
                                        .signed_duration_since(fetched.to_owned())
                                        .to_std()
                                        .ok()
                                })
                                .unwrap_or_default();
                            let sampled_at = now.checked_sub(age).unwrap_or(now);
                            self.smart.observe_plugin(
                                id,
                                record.manifest.attention_states(data),
                                &plugin_views,
                                sampled_at,
                                max_age,
                            );
                        }
                    }
                }
                let waiting = state
                    .claude_code
                    .lock()
                    .unwrap()
                    .sessions
                    .waiting(Utc::now().timestamp())
                    .iter()
                    .filter_map(|s| s.waiting.map(|kind| (s.id.clone(), kind, s.since)))
                    .collect();
                self.smart.observe_claude_code(waiting, &plugin_views, now);
            }
            Job::ResetSmartPlugin(id) => {
                let settings = self
                    .app
                    .state::<AppState>()
                    .settings
                    .lock()
                    .unwrap()
                    .clone();
                let views: Vec<_> = settings
                    .views
                    .iter()
                    .map(|view| match view {
                        ViewContent::Plugin(name) => Some(name.as_str()),
                        _ => None,
                    })
                    .collect();
                self.smart.reset_plugin(&id, &views);
            }
            Job::SmartUsage(provider, sample) => {
                let settings = self
                    .app
                    .state::<AppState>()
                    .settings
                    .lock()
                    .unwrap()
                    .clone();
                if settings.ai_usage_enabled && settings.view_mode == ViewMode::Intelligent {
                    let views: Vec<_> = settings
                        .views
                        .iter()
                        .map(|view| match view {
                            ViewContent::Provider(p) => Some(*p),
                            _ => None,
                        })
                        .collect();
                    self.smart
                        .observe_usage(provider, sample, &views, Instant::now());
                }
            }
            Job::SmartPlugin(id, states) => {
                let settings = self
                    .app
                    .state::<AppState>()
                    .settings
                    .lock()
                    .unwrap()
                    .clone();
                if settings.view_mode == ViewMode::Intelligent {
                    let language = self
                        .profile
                        .as_ref()
                        .map(|profile| profile.language)
                        .unwrap_or_default();
                    let theme = self
                        .profile
                        .as_ref()
                        .map(|profile| profile.theme)
                        .unwrap_or_default()
                        .resolve(system_is_dark());
                    let max_age = self
                        .app
                        .state::<AppState>()
                        .plugins
                        .lock()
                        .unwrap()
                        .records
                        .get(&id)
                        .and_then(|record| {
                            let renderable = record.last_error.is_none()
                                && record.data.as_ref().is_some_and(|data| {
                                    [
                                        SceneLayout::Portrait,
                                        SceneLayout::Landscape,
                                        SceneLayout::Square,
                                    ]
                                    .into_iter()
                                    .all(|layout| {
                                        record
                                            .manifest
                                            .scene_with_theme_and_locale(
                                                layout,
                                                data,
                                                &record.settings,
                                                theme,
                                                language.wire(),
                                            )
                                            .is_ok()
                                    })
                                });
                            renderable.then(|| {
                                Duration::from_secs(
                                    3 * record.manifest.source.interval_seconds as u64,
                                )
                            })
                        });
                    if let Some(max_age) = max_age {
                        let views: Vec<_> = settings
                            .views
                            .iter()
                            .map(|view| match view {
                                ViewContent::Plugin(name) => Some(name.as_str()),
                                _ => None,
                            })
                            .collect();
                        self.smart
                            .observe_plugin(&id, states, &views, Instant::now(), max_age);
                    }
                }
            }
            Job::SmartClaudeCode(waiting) => {
                let settings = self
                    .app
                    .state::<AppState>()
                    .settings
                    .lock()
                    .unwrap()
                    .clone();
                if settings.view_mode == ViewMode::Intelligent {
                    let views: Vec<_> = settings
                        .views
                        .iter()
                        .map(|view| match view {
                            ViewContent::Plugin(id) => Some(id.as_str()),
                            _ => None,
                        })
                        .collect();
                    self.smart
                        .observe_claude_code(waiting, &views, Instant::now());
                }
            }
            Job::PerformanceReady => {
                let sample = self.app.state::<AppState>().performance.take();
                if let Some(sample) = sample {
                    self.send_performance_frame(sample);
                }
            }
            Job::MusicReady => {
                let snapshot = self.app.state::<AppState>().music.take();
                if let Some(snapshot) = snapshot {
                    self.send_music_frame(snapshot);
                }
            }
            Job::AudioVisualizerReady => {
                let snapshot = self.app.state::<AppState>().audio_visualizer.take();
                if let Some(snapshot) = snapshot {
                    self.send_audio_visualizer_frame(snapshot);
                }
            }
            Job::EchoDeskControl {
                method,
                params,
                reply,
            } => {
                let result = self.echo_desk_control(&method, params);
                if result.is_ok() {
                    self.publish();
                }
                let _ = reply.send(result);
            }
            Job::EchoDeskReconnect => {
                self.disconnect("EchoDesk Desktop requested reconnect");
                self.last_disconnect = None;
                self.next_scan = Instant::now();
                self.publish();
            }
            Job::SendDiagnostic => self.send_diagnostic_frame(),
            Job::ApplyProfile {
                theme,
                orientation,
                language,
            } => {
                self.profile = self.registry_profile();
                if matches!(self.state, LinkState::Connected(_)) {
                    let mut ok = true;
                    if let Some(t) = theme {
                        ok = self.send_command(&Command::set_theme(t.resolve(system_is_dark())));
                    }
                    if ok {
                        if let Some(o) = orientation {
                            ok = self.send_command(&Command::set_orientation(o));
                        }
                    }
                    if ok {
                        if let Some(l) = language {
                            self.send_command(&Command::set_language(l));
                        }
                    }
                    self.schedule_send();
                }
                self.publish();
            }
            Job::SetBrightness { value, persist } => {
                self.profile = self.registry_profile();
                if let LinkState::Connected(info) = &self.state {
                    if persist || info.supports_brightness_preview() {
                        self.send_command(&Command::set_brightness(value, persist));
                    }
                }
                if persist {
                    self.publish();
                }
            }
            Job::RefreshProfile => {
                self.profile = self.registry_profile();
                self.publish();
            }
            Job::Standby => {
                if self.link.is_some() {
                    self.send_command(&Command::standby());
                    self.publish();
                }
            }
            Job::Pause(done) => {
                self.paused = true;
                self.disconnect("Pause für Flash");
                self.log_event("Angehalten: Scan gestoppt, Port frei");
                self.publish();
                let _ = done.send(());
            }
            Job::Resume {
                diagnostic_after_connect,
            } => {
                self.paused = false;
                self.diagnostic_after_connect = diagnostic_after_connect;
                self.last_disconnect = None;
                self.next_scan = Instant::now();
                self.log_event(format!(
                    "Fortgesetzt: Scan läuft wieder{}",
                    if diagnostic_after_connect {
                        ", Diagnose-Frame nach dem nächsten Connect"
                    } else {
                        ""
                    }
                ));
                self.publish();
            }
            Job::Log(text) => {
                self.log_event(text);
                self.publish();
            }
            Job::Shutdown(done) => {
                if self.link.is_some() {
                    self.send_command(&Command::standby());
                }
                self.close_port();
                self.log_event("Beendet, Port geschlossen");
                let _ = done.send(());
                return false;
            }
        }
        true
    }

    fn wifi_command(&mut self, action: WifiAction) -> Result<Value, String> {
        if !matches!(self.state, LinkState::Connected(_)) {
            return Err("Kein Display verbunden".into());
        }
        let (line, expected, timeout, label) = match action {
            WifiAction::Status => (
                Command::wifi_status(),
                "wifi_status",
                Duration::from_secs(4),
                "WLAN-Status",
            ),
            WifiAction::Scan => (
                Command::wifi_scan(),
                "wifi_scan",
                Duration::from_secs(18),
                "WLAN-Scan",
            ),
            WifiAction::Set { ssid, password } => {
                if ssid.is_empty() {
                    return Err("SSID fehlt".into());
                }
                (
                    Command::wifi_set(&ssid, &password),
                    "wifi_status",
                    Duration::from_secs(15),
                    "WLAN verbinden",
                )
            }
            WifiAction::Forget => (
                Command::wifi_forget(),
                "wifi_status",
                Duration::from_secs(5),
                "WLAN vergessen",
            ),
        };
        self.log_event(format!("-> {label}"));
        let result = self
            .link
            .as_mut()
            .ok_or("Kein Port offen")?
            .command_with_response(&line, expected, timeout);
        self.absorb_link_log();
        match result {
            Ok(Some(aimonitor_core::protocol::DeviceMessage::WifiStatus(value)))
            | Ok(Some(aimonitor_core::protocol::DeviceMessage::WifiScan(value))) => {
                self.publish();
                Ok(value)
            }
            Ok(_) => {
                self.publish();
                Err(format!("Keine Antwort auf {label}"))
            }
            Err(e) => {
                let message = e.to_string();
                self.disconnect("WLAN-Kommando: Portfehler");
                Err(message)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reset_baselines_ignore_missing_or_old_samples() {
        let now = Utc::now();
        assert!(!recent_sample(None, POLL_INTERVAL * 2));
        assert!(recent_sample(
            Some(&(now - chrono::Duration::seconds(30))),
            POLL_INTERVAL * 2,
        ));
        assert!(!recent_sample(
            Some(&(now - chrono::Duration::hours(2))),
            POLL_INTERVAL * 2,
        ));
        assert!(!recent_sample(
            Some(&(now + chrono::Duration::hours(2))),
            POLL_INTERVAL * 2,
        ));
    }

    #[test]
    fn notice_texts_exist_in_both_languages() {
        for key in [
            "dsp.notice.auth",
            "dsp.notice.climissing",
            "dsp.notice.failed",
            "dsp.notice.loading",
            "dsp.notice.parse",
            "dsp.notice.stale",
            "dsp.notice.startapp",
        ] {
            assert!(!notice_text(key, Language::De).is_empty(), "{key} de");
            assert!(!notice_text(key, Language::En).is_empty(), "{key} en");
            assert!(
                aimonitor_core::protocol::display_safe_text(notice_text(key, Language::De)).len()
                    <= 63
            );
        }
        assert_eq!(notice_text("dsp.notice.nope", Language::De), "");
    }

    #[test]
    fn desktop_setting_validation_is_allowlisted() {
        assert!(Service::validate_echo_desk_settings(&json!({"brightness": 55}), true).is_ok());
        assert!(Service::validate_echo_desk_settings(
            &json!({
                "theme": "dark",
                "brightness": 80,
                "nowPlayingLayout": "carPlay",
                "nowPlayingBackground": "audioBars",
                "nowPlayingBackgroundCustomized": true,
                "defaultApp": "myDevice",
                "clockFormat": "24h",
                "timezone": "Australia/Melbourne",
                "codexPercentageMode": "remaining",
                "codexPreferredDisplay": "weekly",
                "performanceDefaultView": "memory"
            }),
            false
        )
        .is_ok());
        assert!(Service::validate_echo_desk_settings(&json!({"brightness": 101}), true).is_err());
        assert!(
            Service::validate_echo_desk_settings(&json!({"rawSerial": "restart"}), true).is_err()
        );
        assert!(Service::validate_echo_desk_settings(
            &json!({"timezone": "Australia/Melbourne", "performanceDefaultView": "cpu"}),
            true
        )
        .is_ok());
        assert!(
            Service::validate_echo_desk_settings(&json!({"timezone": "Mars/Olympus"}), true)
                .is_err()
        );
        assert!(Service::validate_echo_desk_settings(
            &json!({"theme": "dark", "brightness": 80}),
            false
        )
        .is_err());
    }

    #[test]
    fn guided_setup_state_is_versioned_and_old_firmware_remains_compatible() {
        assert!(Service::validate_setup_state(&json!({})).is_ok());
        for state in [
            "notConfigured",
            "waitingForDesktop",
            "inProgress",
            "completed",
        ] {
            assert!(Service::validate_setup_state(&json!({
                "setupState": state,
                "setupVersion": 1,
                "setupUrl": null
            }))
            .is_ok());
        }
        assert!(Service::validate_setup_state(&json!({
            "setupState": "inProgress",
            "setupVersion": 1,
            "setupUrl": "https://example.invalid/echodesk"
        }))
        .is_ok());
        assert!(Service::validate_setup_state(&json!({
            "setupState": "completed"
        }))
        .is_err());
        assert!(Service::validate_setup_state(&json!({
            "setupState": "completed",
            "setupVersion": 2
        }))
        .is_err());
        assert!(Service::validate_setup_state(&json!({
            "setupState": "unknown",
            "setupVersion": 1
        }))
        .is_err());
        assert!(Service::validate_setup_state(&json!({
            "setupState": "waitingForDesktop",
            "setupVersion": 1,
            "setupUrl": "http://unsafe.invalid"
        }))
        .is_err());
    }

    #[test]
    fn setup_ack_is_terminal_across_a_device_ui_restart() {
        assert!(echo_ack_is_terminal("setup_begin"));
        assert!(echo_ack_is_terminal("setup_complete"));
        assert!(echo_ack_is_terminal("factory_reset"));
        assert!(echo_ack_is_terminal("prepare_update"));
        assert!(echo_ack_is_terminal("update_complete"));
        assert!(!echo_ack_is_terminal("set_settings"));
        assert!(update_ack_matches(
            "prepare_update",
            &json!({"command":"echodesk_prepare_update"})
        ));
        assert!(!update_ack_matches(
            "prepare_update",
            &json!({"command":"echodesk_update_complete"})
        ));
    }
}
