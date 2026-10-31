//! Opt-in, local-only WASAPI loopback analysis for EchoDesk Audio Bars.
//!
//! The worker captures the Windows default render endpoint only while the
//! device explicitly requests it. PCM is never retained or transmitted: a
//! 1024-sample FFT is reduced to sixteen smoothed byte bands in memory.

use crate::serial_service::Job;
use crate::state::AppState;
use rustfft::{num_complex::Complex32, FftPlanner};
use serde::Serialize;
use std::collections::VecDeque;
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Manager};

pub const VISUALIZER_VERSION: u8 = 1;
pub const VISUALIZER_BAND_COUNT: usize = 16;
const FFT_SIZE: usize = 1024;
const UPDATE_INTERVAL: Duration = Duration::from_millis(50);

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct VisualizerSnapshot {
    #[serde(rename = "type")]
    kind: &'static str,
    version: u8,
    pub available: bool,
    pub enabled: bool,
    pub sequence: u32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bands: Option<[u8; VISUALIZER_BAND_COUNT]>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}

impl VisualizerSnapshot {
    fn available(sequence: u32, bands: [u8; VISUALIZER_BAND_COUNT]) -> Self {
        Self {
            kind: "music_visualizer",
            version: VISUALIZER_VERSION,
            available: true,
            enabled: true,
            sequence,
            bands: Some(bands),
            reason: None,
        }
    }

    fn unavailable(sequence: u32, reason: impl Into<String>) -> Self {
        let mut reason = reason.into();
        if reason.len() > 96 {
            let mut end = 96;
            while end > 0 && !reason.is_char_boundary(end) {
                end -= 1;
            }
            reason.truncate(end);
        }
        Self {
            kind: "music_visualizer",
            version: VISUALIZER_VERSION,
            available: false,
            enabled: true,
            sequence,
            bands: None,
            reason: Some(reason),
        }
    }

    pub fn payload(&self, frame_id: i64) -> Result<String, String> {
        let mut value = serde_json::to_value(self).map_err(|error| error.to_string())?;
        value["frameId"] = serde_json::json!(frame_id);
        let payload = value.to_string();
        if payload.len() >= 1024 {
            return Err(format!("visualizer payload is {} bytes", payload.len()));
        }
        Ok(payload)
    }
}

#[derive(Debug, Default)]
struct FeedState {
    enabled: bool,
    latest: Option<VisualizerSnapshot>,
    notification_pending: bool,
}

#[derive(Debug, Clone, Default)]
pub struct VisualizerFeed {
    inner: Arc<(Mutex<FeedState>, Condvar)>,
}

impl VisualizerFeed {
    pub fn set_enabled(&self, enabled: bool) {
        let (lock, wake) = &*self.inner;
        let mut state = lock.lock().unwrap();
        if state.enabled == enabled {
            return;
        }
        state.enabled = enabled;
        if !enabled {
            state.latest = None;
            state.notification_pending = false;
        }
        wake.notify_all();
    }

    fn enabled(&self) -> bool {
        self.inner.0.lock().unwrap().enabled
    }

    fn wait_until_enabled(&self) {
        let (lock, wake) = &*self.inner;
        let mut state = lock.lock().unwrap();
        while !state.enabled {
            state = wake.wait(state).unwrap();
        }
    }

    fn wait_retry(&self, duration: Duration) {
        let (lock, wake) = &*self.inner;
        let state = lock.lock().unwrap();
        drop(wake.wait_timeout(state, duration));
    }

    fn publish(&self, snapshot: VisualizerSnapshot) -> bool {
        let (lock, _) = &*self.inner;
        let mut state = lock.lock().unwrap();
        if !state.enabled {
            return false;
        }
        state.latest = Some(snapshot);
        if state.notification_pending {
            false
        } else {
            state.notification_pending = true;
            true
        }
    }

    pub fn take(&self) -> Option<VisualizerSnapshot> {
        let (lock, _) = &*self.inner;
        let mut state = lock.lock().unwrap();
        let latest = state.latest.take();
        state.notification_pending = false;
        latest
    }
}

pub fn start(app: AppHandle) {
    #[cfg(windows)]
    std::thread::Builder::new()
        .name("aimonitor-wasapi-visualizer".into())
        .spawn(move || run_windows(app))
        .expect("WASAPI visualizer worker");

    #[cfg(not(windows))]
    let _ = app;
}

#[cfg(windows)]
struct ComApartment(bool);

#[cfg(windows)]
impl Drop for ComApartment {
    fn drop(&mut self) {
        if self.0 {
            unsafe { windows::Win32::System::Com::CoUninitialize() };
        }
    }
}

fn publish(app: &AppHandle, feed: &VisualizerFeed, snapshot: VisualizerSnapshot) {
    if feed.publish(snapshot) {
        let _ = app
            .state::<AppState>()
            .serial
            .send(Job::AudioVisualizerReady);
    }
}

#[cfg(windows)]
fn run_windows(app: AppHandle) {
    use windows::Win32::System::Com::{CoInitializeEx, COINIT_MULTITHREADED};

    let feed = app.state::<AppState>().audio_visualizer.clone();
    let mut sequence = 0u32;
    // Exactly one COM apartment belongs to this long-lived worker. Capture
    // sessions can be torn down and recreated without reinitialising COM.
    let com_error = unsafe { CoInitializeEx(None, COINIT_MULTITHREADED) }
        .ok()
        .err()
        .map(|error| format!("COM initialization failed: {error}"));
    let _com_apartment = ComApartment(com_error.is_none());
    loop {
        feed.wait_until_enabled();
        if let Some(error) = &com_error {
            sequence = sequence.wrapping_add(1);
            publish(
                &app,
                &feed,
                VisualizerSnapshot::unavailable(sequence, error.clone()),
            );
            feed.wait_retry(Duration::from_secs(5));
            continue;
        }
        sequence = sequence.wrapping_add(1);
        publish(
            &app,
            &feed,
            VisualizerSnapshot::unavailable(sequence, "initializing"),
        );
        if let Err(error) = capture_loop(&app, &feed, &mut sequence) {
            sequence = sequence.wrapping_add(1);
            publish(
                &app,
                &feed,
                VisualizerSnapshot::unavailable(sequence, error),
            );
            feed.wait_retry(Duration::from_secs(2));
        }
    }
}

#[derive(Debug, Clone, Copy)]
enum SampleEncoding {
    Float32,
    Pcm16,
    Pcm24,
    Pcm32,
}

#[cfg(windows)]
fn capture_loop(app: &AppHandle, feed: &VisualizerFeed, sequence: &mut u32) -> Result<(), String> {
    use windows::Win32::Media::Audio::{
        eMultimedia, eRender, IAudioCaptureClient, IAudioClient, IMMDeviceEnumerator,
        MMDeviceEnumerator, AUDCLNT_BUFFERFLAGS_SILENT, AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK, WAVEFORMATEX,
    };
    use windows::Win32::System::Com::{CoCreateInstance, CoTaskMemFree, CLSCTX_ALL};

    unsafe {
        let enumerator: IMMDeviceEnumerator =
            CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)
                .map_err(|error| format!("audio device enumeration failed: {error}"))?;
        let device = enumerator
            .GetDefaultAudioEndpoint(eRender, eMultimedia)
            .map_err(|error| format!("default output unavailable: {error}"))?;
        let client: IAudioClient = device
            .Activate(CLSCTX_ALL, None)
            .map_err(|error| format!("output activation failed: {error}"))?;
        let format_ptr = client
            .GetMixFormat()
            .map_err(|error| format!("output format unavailable: {error}"))?;
        if format_ptr.is_null() {
            return Err("output format unavailable".into());
        }
        let format: WAVEFORMATEX = format_ptr.read_unaligned();
        let encoding = match sample_encoding(format_ptr, &format) {
            Ok(encoding) => encoding,
            Err(error) => {
                CoTaskMemFree(Some(format_ptr.cast()));
                return Err(error);
            }
        };
        let channels = usize::from(format.nChannels);
        let sample_rate = format.nSamplesPerSec;
        let block_align = usize::from(format.nBlockAlign);
        if channels == 0 || sample_rate < 8_000 || block_align == 0 {
            CoTaskMemFree(Some(format_ptr.cast()));
            return Err("unsupported output format".into());
        }
        let initialized = client.Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            1_000_000,
            0,
            format_ptr,
            None,
        );
        CoTaskMemFree(Some(format_ptr.cast()));
        initialized.map_err(|error| format!("loopback initialization failed: {error}"))?;
        let capture: IAudioCaptureClient = client
            .GetService()
            .map_err(|error| format!("loopback capture unavailable: {error}"))?;
        client
            .Start()
            .map_err(|error| format!("loopback start failed: {error}"))?;
        crate::serial_service::log(app, "Audio Bars: local WASAPI loopback active");

        let mut samples = VecDeque::with_capacity(FFT_SIZE * 2);
        let mut smoother = BandAnalyzer::new(sample_rate);
        let mut next_update = Instant::now();
        let mut frames_since_update = 0usize;
        while feed.enabled() {
            loop {
                let packet_frames = capture
                    .GetNextPacketSize()
                    .map_err(|error| format!("loopback packet failed: {error}"))?;
                if packet_frames == 0 {
                    break;
                }
                let mut data = std::ptr::null_mut();
                let mut frames = 0u32;
                let mut flags = 0u32;
                capture
                    .GetBuffer(&mut data, &mut frames, &mut flags, None, None)
                    .map_err(|error| format!("loopback read failed: {error}"))?;
                if flags & (AUDCLNT_BUFFERFLAGS_SILENT.0 as u32) != 0 || data.is_null() {
                    for _ in 0..frames {
                        push_sample(&mut samples, 0.0);
                    }
                } else {
                    let bytes = std::slice::from_raw_parts(data, frames as usize * block_align);
                    append_mono_samples(
                        bytes,
                        frames as usize,
                        channels,
                        block_align,
                        encoding,
                        &mut samples,
                    );
                }
                frames_since_update = frames_since_update.saturating_add(frames as usize);
                capture
                    .ReleaseBuffer(frames)
                    .map_err(|error| format!("loopback release failed: {error}"))?;
            }

            let now = Instant::now();
            if now >= next_update {
                next_update = now + UPDATE_INTERVAL;
                if frames_since_update == 0 {
                    // Some drivers emit no silent packets. Do not leave the
                    // previous FFT window frozen; let the real levels decay.
                    samples.clear();
                    samples.resize(FFT_SIZE, 0.0);
                }
                let bands = smoother.analyze(&samples);
                frames_since_update = 0;
                *sequence = (*sequence).wrapping_add(1);
                publish(app, feed, VisualizerSnapshot::available(*sequence, bands));
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        let _ = client.Stop();
    }
    Ok(())
}

#[cfg(windows)]
unsafe fn sample_encoding(
    format_ptr: *const windows::Win32::Media::Audio::WAVEFORMATEX,
    format: &windows::Win32::Media::Audio::WAVEFORMATEX,
) -> Result<SampleEncoding, String> {
    use windows::Win32::Media::Audio::WAVEFORMATEXTENSIBLE;
    use windows::Win32::Media::Multimedia::{
        KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_IEEE_FLOAT,
    };
    const WAVE_FORMAT_EXTENSIBLE: u16 = 0xfffe;
    let format_tag = format.wFormatTag;
    let bits_per_sample = format.wBitsPerSample;
    let extra_size = format.cbSize;
    let is_float = if u32::from(format_tag) == WAVE_FORMAT_IEEE_FLOAT {
        true
    } else if format_tag == WAVE_FORMAT_EXTENSIBLE {
        if usize::from(extra_size) + std::mem::size_of_val(format)
            < std::mem::size_of::<WAVEFORMATEXTENSIBLE>()
        {
            return Err("truncated extensible output format".into());
        }
        let extensible = format_ptr.cast::<WAVEFORMATEXTENSIBLE>();
        std::ptr::addr_of!((*extensible).SubFormat).read_unaligned()
            == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
    } else {
        false
    };
    match (is_float, bits_per_sample) {
        (true, 32) => Ok(SampleEncoding::Float32),
        (false, 16) => Ok(SampleEncoding::Pcm16),
        (false, 24) => Ok(SampleEncoding::Pcm24),
        (false, 32) => Ok(SampleEncoding::Pcm32),
        _ => Err(format!(
            "unsupported output sample format tag={} bits={}",
            format_tag, bits_per_sample
        )),
    }
}

fn push_sample(samples: &mut VecDeque<f32>, sample: f32) {
    if samples.len() >= FFT_SIZE * 2 {
        samples.pop_front();
    }
    samples.push_back(sample.clamp(-1.0, 1.0));
}

fn append_mono_samples(
    bytes: &[u8],
    frames: usize,
    channels: usize,
    block_align: usize,
    encoding: SampleEncoding,
    samples: &mut VecDeque<f32>,
) {
    let bytes_per_sample = block_align / channels.max(1);
    if bytes_per_sample == 0 {
        return;
    }
    for frame in 0..frames {
        let frame_start = frame * block_align;
        if frame_start + block_align > bytes.len() {
            break;
        }
        let mut mono = 0.0f32;
        for channel in 0..channels {
            let offset = frame_start + channel * bytes_per_sample;
            mono += decode_sample(&bytes[offset..], encoding);
        }
        push_sample(samples, mono / channels as f32);
    }
}

fn decode_sample(bytes: &[u8], encoding: SampleEncoding) -> f32 {
    match encoding {
        SampleEncoding::Float32 if bytes.len() >= 4 => {
            f32::from_le_bytes(bytes[..4].try_into().unwrap()).clamp(-1.0, 1.0)
        }
        SampleEncoding::Pcm16 if bytes.len() >= 2 => {
            i16::from_le_bytes(bytes[..2].try_into().unwrap()) as f32 / 32768.0
        }
        SampleEncoding::Pcm24 if bytes.len() >= 3 => {
            let raw = i32::from_le_bytes([
                bytes[0],
                bytes[1],
                bytes[2],
                if bytes[2] & 0x80 != 0 { 0xff } else { 0 },
            ]);
            raw as f32 / 8_388_608.0
        }
        SampleEncoding::Pcm32 if bytes.len() >= 4 => {
            i32::from_le_bytes(bytes[..4].try_into().unwrap()) as f32 / 2_147_483_648.0
        }
        _ => 0.0,
    }
}

struct BandAnalyzer {
    fft: Arc<dyn rustfft::Fft<f32>>,
    buffer: Vec<Complex32>,
    sample_rate: u32,
    smoothed: [f32; VISUALIZER_BAND_COUNT],
}

impl BandAnalyzer {
    fn new(sample_rate: u32) -> Self {
        let mut planner = FftPlanner::new();
        Self {
            fft: planner.plan_fft_forward(FFT_SIZE),
            buffer: vec![Complex32::default(); FFT_SIZE],
            sample_rate,
            smoothed: [0.0; VISUALIZER_BAND_COUNT],
        }
    }

    fn analyze(&mut self, samples: &VecDeque<f32>) -> [u8; VISUALIZER_BAND_COUNT] {
        let missing = FFT_SIZE.saturating_sub(samples.len());
        for index in 0..FFT_SIZE {
            let sample = if index < missing {
                0.0
            } else {
                samples[index - missing]
            };
            let window =
                0.5 - 0.5 * (std::f32::consts::TAU * index as f32 / (FFT_SIZE - 1) as f32).cos();
            self.buffer[index] = Complex32::new(sample * window, 0.0);
        }
        self.fft.process(&mut self.buffer);

        let mut output = [0u8; VISUALIZER_BAND_COUNT];
        let nyquist = self.sample_rate as f32 / 2.0;
        let max_frequency = nyquist.min(16_000.0);
        for (band, output_value) in output.iter_mut().enumerate() {
            let fraction_low = band as f32 / VISUALIZER_BAND_COUNT as f32;
            let fraction_high = (band + 1) as f32 / VISUALIZER_BAND_COUNT as f32;
            let low = 45.0 * (max_frequency / 45.0).powf(fraction_low);
            let high = 45.0 * (max_frequency / 45.0).powf(fraction_high);
            let first = ((low * FFT_SIZE as f32 / self.sample_rate as f32) as usize).max(1);
            let last = ((high * FFT_SIZE as f32 / self.sample_rate as f32) as usize)
                .max(first + 1)
                .min(FFT_SIZE / 2);
            let peak = self.buffer[first..last]
                .iter()
                .map(|value| value.norm() * 2.0 / FFT_SIZE as f32)
                .fold(0.0f32, f32::max);
            let db = 20.0 * peak.max(0.000_001).log10();
            let target = ((db + 72.0) / 66.0).clamp(0.0, 1.0);
            let factor = if target > self.smoothed[band] {
                0.68
            } else {
                0.16
            };
            self.smoothed[band] += (target - self.smoothed[band]) * factor;
            *output_value = (self.smoothed[band] * 255.0).round() as u8;
        }
        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decoder_handles_pcm_without_panics() {
        assert!((decode_sample(&[0xff, 0x7f], SampleEncoding::Pcm16) - 0.999_969).abs() < 0.001);
        assert!(decode_sample(&[0, 0, 0x80], SampleEncoding::Pcm24) <= -0.99);
        assert_eq!(decode_sample(&[], SampleEncoding::Float32), 0.0);
    }

    #[test]
    fn fft_bands_are_signal_based_and_bounded() {
        let sample_rate = 48_000;
        let mut samples = VecDeque::new();
        for index in 0..FFT_SIZE {
            push_sample(
                &mut samples,
                (std::f32::consts::TAU * 1_000.0 * index as f32 / sample_rate as f32).sin() * 0.5,
            );
        }
        let mut analyzer = BandAnalyzer::new(sample_rate);
        let bands = analyzer.analyze(&samples);
        assert!(bands.iter().copied().max().unwrap() > 80);
        assert_ne!(bands, [0; VISUALIZER_BAND_COUNT]);
    }

    #[test]
    fn unavailable_payload_never_contains_pcm() {
        let payload = VisualizerSnapshot::unavailable(7, "no output")
            .payload(3)
            .unwrap();
        assert!(payload.contains("\"available\":false"));
        assert!(!payload.contains("bands"));
        assert!(payload.len() < 1024);
    }

    #[test]
    fn feed_coalesces_latest_bands_and_disable_revokes_capture() {
        let feed = VisualizerFeed::default();
        feed.set_enabled(true);
        assert!(feed.publish(VisualizerSnapshot::available(1, [1; VISUALIZER_BAND_COUNT])));
        assert!(!feed.publish(VisualizerSnapshot::available(2, [2; VISUALIZER_BAND_COUNT])));
        assert_eq!(feed.take().unwrap().sequence, 2);
        feed.set_enabled(false);
        assert!(!feed.enabled());
        assert!(feed.take().is_none());
    }
}
