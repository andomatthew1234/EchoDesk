//! Windows Global System Media Transport Controls (GSMTC) integration.
//!
//! This worker never opens a COM port. It publishes a latest-value snapshot to
//! the existing serial owner, which capability-gates and frames it for EchoDesk.

use crate::serial_service::Job;
use crate::state::AppState;
use base64::Engine;
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::collections::VecDeque;
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;
use tauri::{AppHandle, Manager};

pub const MUSIC_VERSION: u8 = 1;
pub const MUSIC_MAX_PAYLOAD_BYTES: usize = 1536;
pub const MUSIC_ART_MAX_BYTES: usize = 12_288;
pub const MUSIC_ART_CHUNK_BYTES: usize = 1536;
pub const MUSIC_BACKGROUND_MAX_BYTES: usize = 32_768;
pub const MUSIC_BACKGROUND_CHUNK_BYTES: usize = 1536;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum PlaybackState {
    Stopped,
    Paused,
    Playing,
}

#[derive(Debug, Clone)]
pub struct Artwork {
    pub id: String,
    pub bytes: Arc<Vec<u8>>,
    pub crc32: u32,
}

#[derive(Debug, Clone)]
pub struct MusicBackground {
    /// The current artwork ID. Firmware rejects backgrounds for stale tracks.
    pub id: String,
    pub bytes: Arc<Vec<u8>>,
    pub crc32: u32,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MusicSnapshot {
    #[serde(rename = "type")]
    pub kind: &'static str,
    pub version: u8,
    pub timestamp: u64,
    pub generation: u64,
    pub state: PlaybackState,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub application: String,
    pub position_ms: u32,
    pub duration_ms: u32,
    pub can_play_pause: bool,
    pub can_previous: bool,
    pub can_next: bool,
    pub art_id: String,
    pub background_id: String,
    #[serde(skip)]
    pub artwork: Option<Artwork>,
    #[serde(skip)]
    pub background: Option<MusicBackground>,
}

impl MusicSnapshot {
    fn empty(generation: u64) -> Self {
        Self {
            kind: "music",
            version: MUSIC_VERSION,
            timestamp: chrono::Utc::now().timestamp().max(1) as u64,
            generation,
            state: PlaybackState::Stopped,
            title: String::new(),
            artist: String::new(),
            album: String::new(),
            application: String::new(),
            position_ms: 0,
            duration_ms: 0,
            can_play_pause: false,
            can_previous: false,
            can_next: false,
            art_id: String::new(),
            background_id: String::new(),
            artwork: None,
            background: None,
        }
    }

    pub fn payload(&self, frame_id: i64) -> Result<String, String> {
        let mut value = serde_json::to_value(self).map_err(|error| error.to_string())?;
        value["frameId"] = serde_json::json!(frame_id);
        let payload = value.to_string();
        if payload.len() >= MUSIC_MAX_PAYLOAD_BYTES {
            return Err(format!("music payload is {} bytes", payload.len()));
        }
        Ok(payload)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MusicAction {
    Toggle,
    Previous,
    Next,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MusicCommand {
    pub generation: u64,
    pub action: MusicAction,
}

#[derive(Debug, Default)]
struct FeedState {
    latest: Option<MusicSnapshot>,
    notification_pending: bool,
    commands: VecDeque<MusicCommand>,
}

#[derive(Debug, Clone, Default)]
pub struct MusicFeed {
    inner: Arc<(Mutex<FeedState>, Condvar)>,
}

impl MusicFeed {
    pub fn publish(&self, snapshot: MusicSnapshot) -> bool {
        let (lock, _) = &*self.inner;
        let mut state = lock.lock().unwrap();
        state.latest = Some(snapshot);
        if state.notification_pending {
            false
        } else {
            state.notification_pending = true;
            true
        }
    }

    pub fn take(&self) -> Option<MusicSnapshot> {
        let (lock, _) = &*self.inner;
        let mut state = lock.lock().unwrap();
        let snapshot = state.latest.take();
        state.notification_pending = false;
        snapshot
    }

    pub fn command(&self, command: MusicCommand) {
        let (lock, wake) = &*self.inner;
        let mut state = lock.lock().unwrap();
        if state.commands.len() >= 16 {
            state.commands.pop_front();
        }
        state.commands.push_back(command);
        wake.notify_one();
    }

    fn take_commands(&self) -> Vec<MusicCommand> {
        let (lock, _) = &*self.inner;
        lock.lock().unwrap().commands.drain(..).collect()
    }

    fn wait(&self, duration: Duration) {
        let (lock, wake) = &*self.inner;
        let guard = lock.lock().unwrap();
        drop(wake.wait_timeout(guard, duration));
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CandidateState {
    Stopped,
    Paused,
    Playing,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct Candidate {
    source: String,
    state: CandidateState,
    is_previous: bool,
    is_current: bool,
    index: usize,
}

fn choose_candidate(candidates: &[Candidate]) -> Option<usize> {
    let mut ranked: Vec<(i32, &Candidate)> = candidates
        .iter()
        .map(|candidate| {
            let state = match candidate.state {
                CandidateState::Playing => 300,
                CandidateState::Paused => 100,
                CandidateState::Stopped => 0,
            };
            let sticky = if candidate.is_previous { 30 } else { 0 };
            let current = if candidate.is_current { 10 } else { 0 };
            (state + sticky + current, candidate)
        })
        .collect();
    ranked.sort_by(|left, right| {
        right
            .0
            .cmp(&left.0)
            .then_with(|| left.1.source.cmp(&right.1.source))
            .then_with(|| left.1.index.cmp(&right.1.index))
    });
    ranked.first().and_then(|(_, candidate)| {
        (candidate.state != CandidateState::Stopped).then_some(candidate.index)
    })
}

fn truncate_utf8(value: &str, maximum: usize) -> String {
    if value.len() <= maximum {
        return value.to_string();
    }
    let mut end = maximum;
    while end > 0 && !value.is_char_boundary(end) {
        end -= 1;
    }
    value[..end].to_string()
}

pub fn art_chunk_payload(art: &Artwork, frame_id: i64, index: usize) -> Result<String, String> {
    let chunks = art.bytes.len().div_ceil(MUSIC_ART_CHUNK_BYTES);
    if index >= chunks {
        return Err("art chunk index outside transfer".into());
    }
    let offset = index * MUSIC_ART_CHUNK_BYTES;
    let end = (offset + MUSIC_ART_CHUNK_BYTES).min(art.bytes.len());
    let data = base64::engine::general_purpose::STANDARD.encode(&art.bytes[offset..end]);
    let payload = serde_json::json!({
        "type": "music_art",
        "version": MUSIC_VERSION,
        "frameId": frame_id,
        "id": art.id,
        "width": 96,
        "height": 96,
        "index": index,
        "chunks": chunks,
        "offset": offset,
        "totalBytes": art.bytes.len(),
        "crc32": art.crc32,
        "data": data,
    })
    .to_string();
    if payload.len() >= 4095 {
        return Err(format!("music art payload is {} bytes", payload.len()));
    }
    Ok(payload)
}

pub fn background_chunk_payload(
    background: &MusicBackground,
    frame_id: i64,
    index: usize,
) -> Result<String, String> {
    let chunks = background
        .bytes
        .len()
        .div_ceil(MUSIC_BACKGROUND_CHUNK_BYTES);
    if index >= chunks {
        return Err("background chunk index outside transfer".into());
    }
    let offset = index * MUSIC_BACKGROUND_CHUNK_BYTES;
    let end = (offset + MUSIC_BACKGROUND_CHUNK_BYTES).min(background.bytes.len());
    let data = base64::engine::general_purpose::STANDARD.encode(&background.bytes[offset..end]);
    let payload = serde_json::json!({
        "type": "music_background",
        "version": MUSIC_VERSION,
        "frameId": frame_id,
        "id": background.id,
        "width": 320,
        "height": 240,
        "index": index,
        "chunks": chunks,
        "offset": offset,
        "totalBytes": background.bytes.len(),
        "crc32": background.crc32,
        "data": data,
    })
    .to_string();
    if payload.len() >= 4095 {
        return Err(format!(
            "music background payload is {} bytes",
            payload.len()
        ));
    }
    Ok(payload)
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = 0xffff_ffffu32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320u32 & (0u32.wrapping_sub(crc & 1)));
        }
    }
    !crc
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
        .name("aimonitor-music".into())
        .spawn(move || run_windows(app))
        .expect("music worker thread");
}

#[cfg(windows)]
fn run_windows(app: AppHandle) {
    use windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;

    let feed = app.state::<AppState>().music.clone();
    let mut retry = Duration::from_secs(1);
    loop {
        match GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
            .and_then(|operation| operation.get())
        {
            Ok(manager) => {
                crate::serial_service::log(&app, "Windows media sessions available");
                if let Err(error) = run_manager(&app, &feed, manager) {
                    crate::serial_service::log(
                        &app,
                        format!("Windows media session restart: {error}"),
                    );
                }
                retry = Duration::from_secs(1);
            }
            Err(error) => {
                publish_snapshot(&app, &feed, MusicSnapshot::empty(1));
                crate::serial_service::log(
                    &app,
                    format!("Windows media sessions unavailable: {error}"),
                );
            }
        }
        std::thread::sleep(retry);
        retry = (retry * 2).min(Duration::from_secs(30));
    }
}

#[cfg(windows)]
fn run_manager(
    app: &AppHandle,
    feed: &MusicFeed,
    manager: windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager,
) -> Result<(), String> {
    use windows::Media::Control::{
        GlobalSystemMediaTransportControlsSession,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status,
    };

    let mut previous_source = String::new();
    let mut generation = 1u64;
    let mut track_key = String::new();
    let mut cached_artwork: Option<Artwork> = None;
    let mut cached_background: Option<MusicBackground> = None;
    loop {
        let sessions = manager.GetSessions().map_err(|error| error.to_string())?;
        let current_source = manager
            .GetCurrentSession()
            .ok()
            .and_then(|session| session.SourceAppUserModelId().ok())
            .map(|source| source.to_string());
        let mut candidates = Vec::new();
        let mut session_values: Vec<GlobalSystemMediaTransportControlsSession> = Vec::new();
        for index in 0..sessions.Size().map_err(|error| error.to_string())? {
            let session = sessions.GetAt(index).map_err(|error| error.to_string())?;
            let source = session
                .SourceAppUserModelId()
                .map_err(|error| error.to_string())?
                .to_string();
            let status = session
                .GetPlaybackInfo()
                .and_then(|info| info.PlaybackStatus())
                .unwrap_or(Status::Stopped);
            let state = if status == Status::Playing {
                CandidateState::Playing
            } else if status == Status::Paused {
                CandidateState::Paused
            } else {
                CandidateState::Stopped
            };
            candidates.push(Candidate {
                source: source.clone(),
                state,
                is_previous: source == previous_source,
                is_current: current_source.as_deref() == Some(source.as_str()),
                index: index as usize,
            });
            session_values.push(session);
        }

        let selected = choose_candidate(&candidates);
        let Some(selected_index) = selected else {
            if !previous_source.is_empty() {
                generation = generation.saturating_add(1);
                previous_source.clear();
                track_key.clear();
                cached_artwork = None;
                cached_background = None;
            }
            feed.take_commands();
            publish_snapshot(app, feed, MusicSnapshot::empty(generation));
            feed.wait(Duration::from_secs(1));
            continue;
        };
        let session = &session_values[selected_index];
        let source = candidates[selected_index].source.clone();
        if source != previous_source {
            generation = generation.saturating_add(1);
            previous_source = source.clone();
            track_key.clear();
            cached_artwork = None;
            cached_background = None;
        }
        for command in feed.take_commands() {
            if command.generation == generation {
                execute_command(session, command);
            }
        }
        let snapshot = sample_session(
            session,
            &source,
            generation,
            &mut track_key,
            &mut cached_artwork,
            &mut cached_background,
        )?;
        publish_snapshot(app, feed, snapshot);
        feed.wait(Duration::from_secs(1));
    }
}

#[cfg(windows)]
fn execute_command(
    session: &windows::Media::Control::GlobalSystemMediaTransportControlsSession,
    command: MusicCommand,
) {
    let Ok(info) = session.GetPlaybackInfo() else {
        return;
    };
    let Ok(controls) = info.Controls() else {
        return;
    };
    let status = info.PlaybackStatus().ok();
    let operation = match command.action {
        MusicAction::Toggle if controls.IsPlayPauseToggleEnabled().unwrap_or(false) => {
            session.TryTogglePlayPauseAsync()
        }
        MusicAction::Toggle
            if status
                == Some(windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                && controls.IsPauseEnabled().unwrap_or(false) =>
        {
            session.TryPauseAsync()
        }
        MusicAction::Toggle if controls.IsPlayEnabled().unwrap_or(false) => session.TryPlayAsync(),
        MusicAction::Previous if controls.IsPreviousEnabled().unwrap_or(false) => session.TrySkipPreviousAsync(),
        MusicAction::Next if controls.IsNextEnabled().unwrap_or(false) => session.TrySkipNextAsync(),
        _ => return,
    };
    if let Ok(operation) = operation {
        let _ = operation.get();
    }
}

#[cfg(windows)]
fn sample_session(
    session: &windows::Media::Control::GlobalSystemMediaTransportControlsSession,
    source: &str,
    generation: u64,
    track_key: &mut String,
    cached_artwork: &mut Option<Artwork>,
    cached_background: &mut Option<MusicBackground>,
) -> Result<MusicSnapshot, String> {
    use windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status;

    let playback = session
        .GetPlaybackInfo()
        .map_err(|error| error.to_string())?;
    let status = playback
        .PlaybackStatus()
        .map_err(|error| error.to_string())?;
    let state = if status == Status::Playing {
        PlaybackState::Playing
    } else if status == Status::Paused {
        PlaybackState::Paused
    } else {
        PlaybackState::Stopped
    };
    let controls = playback.Controls().map_err(|error| error.to_string())?;
    let timeline = session.GetTimelineProperties().ok();
    let timeline_start_ms = timeline
        .as_ref()
        .and_then(|value| value.StartTime().ok())
        .map(|value| ticks_to_ms(value.Duration))
        .unwrap_or(0);
    let absolute_position_ms = timeline
        .as_ref()
        .and_then(|value| value.Position().ok())
        .map(|value| ticks_to_ms(value.Duration))
        .unwrap_or(0);
    let timeline_end_ms = timeline
        .as_ref()
        .and_then(|value| value.EndTime().ok())
        .map(|value| ticks_to_ms(value.Duration))
        .unwrap_or(0);
    let position_ms = absolute_position_ms.saturating_sub(timeline_start_ms);
    let duration_ms = timeline_end_ms.saturating_sub(timeline_start_ms);
    let properties = session
        .TryGetMediaPropertiesAsync()
        .and_then(|operation| operation.get())
        .map_err(|error| error.to_string())?;
    let title = truncate_utf8(&properties.Title().unwrap_or_default().to_string(), 96);
    let mut artist = properties.Artist().unwrap_or_default().to_string();
    if artist.is_empty() {
        artist = properties.AlbumArtist().unwrap_or_default().to_string();
    }
    let artist = truncate_utf8(&artist, 64);
    let album = truncate_utf8(&properties.AlbumTitle().unwrap_or_default().to_string(), 64);
    let next_track_key = format!("{source}\u{1f}{title}\u{1f}{artist}\u{1f}{album}");
    if *track_key != next_track_key {
        *track_key = next_track_key;
        let images = read_artwork(&properties).ok().flatten();
        *cached_artwork = images.as_ref().map(|images| images.0.clone());
        *cached_background = images.and_then(|images| images.1);
    }
    let artwork = cached_artwork.clone();
    let background = cached_background.clone();
    Ok(MusicSnapshot {
        kind: "music",
        version: MUSIC_VERSION,
        timestamp: chrono::Utc::now().timestamp().max(1) as u64,
        generation,
        state,
        title,
        artist,
        album,
        application: friendly_application(source),
        position_ms,
        duration_ms,
        can_play_pause: controls.IsPlayPauseToggleEnabled().unwrap_or(false)
            || (state == PlaybackState::Playing && controls.IsPauseEnabled().unwrap_or(false))
            || (state != PlaybackState::Playing && controls.IsPlayEnabled().unwrap_or(false)),
        can_previous: controls.IsPreviousEnabled().unwrap_or(false),
        can_next: controls.IsNextEnabled().unwrap_or(false),
        art_id: artwork
            .as_ref()
            .map(|art| art.id.clone())
            .unwrap_or_default(),
        background_id: background
            .as_ref()
            .map(|image| image.id.clone())
            .unwrap_or_default(),
        artwork,
        background,
    })
}

#[cfg(windows)]
fn read_artwork(
    properties: &windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties,
) -> Result<Option<(Artwork, Option<MusicBackground>)>, String> {
    use image::codecs::jpeg::JpegEncoder;
    use image::imageops::FilterType;
    use std::io::Cursor;
    use windows::Storage::Streams::DataReader;

    let reference = match properties.Thumbnail() {
        Ok(reference) => reference,
        Err(_) => return Ok(None),
    };
    let stream = reference
        .OpenReadAsync()
        .and_then(|operation| operation.get())
        .map_err(|error| error.to_string())?;
    let size = stream.Size().map_err(|error| error.to_string())?;
    if size == 0 || size > 8 * 1024 * 1024 {
        return Ok(None);
    }
    let input = stream
        .GetInputStreamAt(0)
        .map_err(|error| error.to_string())?;
    let reader = DataReader::CreateDataReader(&input).map_err(|error| error.to_string())?;
    let loaded = reader
        .LoadAsync(size as u32)
        .and_then(|operation| operation.get())
        .map_err(|error| error.to_string())?;
    if loaded == 0 || loaded > size as u32 {
        return Ok(None);
    }
    let mut source = vec![0u8; loaded as usize];
    reader
        .ReadBytes(&mut source)
        .map_err(|error| error.to_string())?;
    let (width, height) = image::ImageReader::new(Cursor::new(&source))
        .with_guessed_format()
        .map_err(|error| error.to_string())?
        .into_dimensions()
        .map_err(|error| error.to_string())?;
    if width == 0 || height == 0 || u64::from(width) * u64::from(height) > 40_000_000 {
        return Ok(None);
    }
    let decoded = image::ImageReader::new(Cursor::new(source))
        .with_guessed_format()
        .map_err(|error| error.to_string())?
        .decode()
        .map_err(|error| error.to_string())?;
    let resized = decoded.resize_to_fill(96, 96, FilterType::Triangle);
    let mut encoded = Vec::new();
    for quality in [70, 60, 50, 40] {
        encoded.clear();
        JpegEncoder::new_with_quality(&mut encoded, quality)
            .encode_image(&resized)
            .map_err(|error| error.to_string())?;
        if encoded.len() <= MUSIC_ART_MAX_BYTES {
            break;
        }
    }
    if encoded.is_empty() || encoded.len() > MUSIC_ART_MAX_BYTES {
        return Ok(None);
    }
    let digest = Sha256::digest(&encoded);
    let id = digest[..8]
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect::<String>();
    let artwork = Artwork {
        id: id.clone(),
        crc32: crc32(&encoded),
        bytes: Arc::new(encoded),
    };

    // Crop to the panel aspect ratio before resizing; this fills the display
    // without distorting the cover. Blurring is intentionally host-side.
    let softened = decoded
        .resize_to_fill(320, 240, FilterType::CatmullRom)
        .blur(14.0);
    let mut background_bytes = Vec::new();
    for quality in [55, 45, 35, 25] {
        background_bytes.clear();
        JpegEncoder::new_with_quality(&mut background_bytes, quality)
            .encode_image(&softened)
            .map_err(|error| error.to_string())?;
        if background_bytes.len() <= MUSIC_BACKGROUND_MAX_BYTES {
            break;
        }
    }
    let background =
        if background_bytes.is_empty() || background_bytes.len() > MUSIC_BACKGROUND_MAX_BYTES {
            None
        } else {
            Some(MusicBackground {
                id,
                crc32: crc32(&background_bytes),
                bytes: Arc::new(background_bytes),
            })
        };
    Ok(Some((artwork, background)))
}

#[cfg(windows)]
fn ticks_to_ms(ticks: i64) -> u32 {
    if ticks <= 0 {
        0
    } else {
        (ticks / 10_000).clamp(0, i64::from(u32::MAX)) as u32
    }
}

#[cfg(windows)]
fn friendly_application(source: &str) -> String {
    let leaf = source
        .rsplit(['\\', '/', '!'])
        .next()
        .unwrap_or(source)
        .trim_end_matches(".exe");
    truncate_utf8(if leaf.is_empty() { source } else { leaf }, 64)
}

fn publish_snapshot(app: &AppHandle, feed: &MusicFeed, snapshot: MusicSnapshot) {
    if feed.publish(snapshot) {
        let _ = app.state::<AppState>().serial.send(Job::MusicReady);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn candidate(source: &str, state: CandidateState, index: usize) -> Candidate {
        Candidate {
            source: source.into(),
            state,
            is_previous: false,
            is_current: false,
            index,
        }
    }

    #[test]
    fn playing_session_beats_paused_browser() {
        let values = vec![
            candidate("browser", CandidateState::Paused, 0),
            candidate("feishin", CandidateState::Playing, 1),
        ];
        assert_eq!(choose_candidate(&values), Some(1));
    }

    #[test]
    fn previous_paused_session_is_sticky_and_ties_are_deterministic() {
        let mut values = vec![
            candidate("z-player", CandidateState::Paused, 0),
            candidate("a-player", CandidateState::Paused, 1),
        ];
        values[0].is_previous = true;
        assert_eq!(choose_candidate(&values), Some(0));
        values[0].is_previous = false;
        assert_eq!(choose_candidate(&values), Some(1));
    }

    #[test]
    fn stopped_sessions_produce_the_empty_state() {
        let values = vec![candidate("stale-player", CandidateState::Stopped, 0)];
        assert_eq!(choose_candidate(&values), None);
    }

    #[test]
    fn utf8_truncation_keeps_code_points_and_payload_is_bounded() {
        let value = "🎵".repeat(30);
        let truncated = truncate_utf8(&value, 97);
        assert!(truncated.len() <= 97);
        assert!(truncated.is_char_boundary(truncated.len()));
        let mut snapshot = MusicSnapshot::empty(4);
        snapshot.title = truncated;
        snapshot.artist = "a".repeat(64);
        snapshot.album = "b".repeat(64);
        snapshot.application = "c".repeat(64);
        assert!(snapshot.payload(1).unwrap().len() < MUSIC_MAX_PAYLOAD_BYTES);
    }

    #[test]
    fn artwork_chunks_are_bounded_and_round_trip() {
        let bytes = (0..MUSIC_ART_MAX_BYTES)
            .map(|value| (value % 251) as u8)
            .collect::<Vec<_>>();
        let art = Artwork {
            id: "0123456789abcdef".into(),
            crc32: crc32(&bytes),
            bytes: Arc::new(bytes.clone()),
        };
        let mut rebuilt = Vec::new();
        for index in 0..bytes.len().div_ceil(MUSIC_ART_CHUNK_BYTES) {
            let payload = art_chunk_payload(&art, index as i64 + 1, index).unwrap();
            assert!(payload.len() < 4095);
            let value: serde_json::Value = serde_json::from_str(&payload).unwrap();
            rebuilt.extend(
                base64::engine::general_purpose::STANDARD
                    .decode(value["data"].as_str().unwrap())
                    .unwrap(),
            );
        }
        assert_eq!(rebuilt, bytes);
        assert_eq!(crc32(&rebuilt), art.crc32);
    }

    #[test]
    fn background_chunks_are_bounded_and_tied_to_artwork() {
        let background = MusicBackground {
            id: "track-art".into(),
            bytes: Arc::new(vec![0x55; MUSIC_BACKGROUND_CHUNK_BYTES + 37]),
            crc32: 99,
        };
        let first = background_chunk_payload(&background, 4, 0).unwrap();
        let value: serde_json::Value = serde_json::from_str(&first).unwrap();
        assert_eq!(value["type"], "music_background");
        assert_eq!(value["id"], "track-art");
        assert_eq!(value["width"], 320);
        assert_eq!(value["height"], 240);
        assert_eq!(value["chunks"], 2);
        assert!(first.len() < 4095);
        assert!(background_chunk_payload(&background, 5, 2).is_err());
    }

    #[test]
    fn feed_coalesces_and_bounds_commands() {
        let feed = MusicFeed::default();
        assert!(feed.publish(MusicSnapshot::empty(1)));
        assert!(!feed.publish(MusicSnapshot::empty(2)));
        assert_eq!(feed.take().unwrap().generation, 2);
        for generation in 0..32 {
            feed.command(MusicCommand {
                generation,
                action: MusicAction::Next,
            });
        }
        let commands = feed.take_commands();
        assert_eq!(commands.len(), 16);
        assert_eq!(commands[0].generation, 16);
    }
}
