//! aimonitor-flash: gemergte Firmware-Images (Bootloader + Partitionen + App
//! ab Offset 0) auf den ESP32 schreiben. Nutzt espflash als Bibliothek,
//! Phase-0-Spike vom 10. September 2026: Release-Image bei 460800 Baud in
//! rund 26 s, danach Hard-Reset.
//!
//! Synchron und blockierend; die App ruft [`flash_image`] in einem
//! Worker-Thread auf und gibt vorher den Serial-Port frei (Spec 6.4).

use espflash::connection::{Connection, ResetAfterOperation, ResetBeforeOperation};
use espflash::flasher::{FlashSize, Flasher};
use espflash::target::{Chip, ProgressCallbacks};
use serde::{Deserialize, Serialize};
use serialport::{FlowControl, SerialPortType, UsbPortInfo};
use sha2::{Digest, Sha256};
use std::time::Duration;

/// Versioned, data-only firmware catalogue shipped with EchoDesk Desktop.
/// Downloaded images are never executed and every image is pinned by SHA-256.
#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct FirmwareManifest {
    pub schema_version: u32,
    pub generated_at: String,
    pub releases: Vec<FirmwareRelease>,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct FirmwareRelease {
    pub firmware_version: String,
    pub hardware_model: String,
    pub hardware_id: String,
    pub supported_chip: String,
    pub image_type: String,
    pub flash_address: u32,
    pub flash_mode: String,
    pub flash_frequency: String,
    pub flash_size: String,
    pub image_file: String,
    pub download_url: Option<String>,
    pub sha256: String,
    pub size: u64,
    pub release_notes: String,
    pub minimum_desktop_version: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum ManifestError {
    #[error("manifest JSON is invalid")]
    Json,
    #[error("unsupported manifest schema")]
    Schema,
    #[error("manifest contains no releases")]
    Empty,
    #[error("release metadata is invalid")]
    Release,
    #[error("firmware source must be a bundled relative path or HTTPS URL")]
    Source,
}

/// Parse and structurally validate the release catalogue. Hardware allowlisting
/// is deliberately left to the device-specific manager, not to UI input.
pub fn parse_firmware_manifest(bytes: &[u8]) -> Result<FirmwareManifest, ManifestError> {
    let manifest: FirmwareManifest =
        serde_json::from_slice(bytes).map_err(|_| ManifestError::Json)?;
    if manifest.schema_version != 1 {
        return Err(ManifestError::Schema);
    }
    if manifest.releases.is_empty() || manifest.releases.len() > 64 {
        return Err(ManifestError::Empty);
    }
    for release in &manifest.releases {
        let digest_ok = release.sha256.len() == 64
            && release.sha256.bytes().all(|byte| byte.is_ascii_hexdigit());
        let relative_file = std::path::Path::new(&release.image_file);
        let safe_relative_file = !release.image_file.is_empty()
            && relative_file.is_relative()
            && !relative_file
                .components()
                .any(|part| matches!(part, std::path::Component::ParentDir));
        if release.firmware_version.trim().is_empty()
            || release.hardware_model.trim().is_empty()
            || release.hardware_id.trim().is_empty()
            || release.minimum_desktop_version.trim().is_empty()
            || release.size == 0
            || !digest_ok
            || !safe_relative_file
        {
            return Err(ManifestError::Release);
        }
        if release
            .download_url
            .as_deref()
            .is_some_and(|url| !url.starts_with("https://"))
        {
            return Err(ManifestError::Source);
        }
    }
    Ok(manifest)
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    digest.iter().map(|byte| format!("{byte:02X}")).collect()
}

pub fn verify_sha256(bytes: &[u8], expected: &str) -> bool {
    sha256_hex(bytes).eq_ignore_ascii_case(expected.trim())
}

/// Baudrate für den Flash-Vorgang wie die Mac-App (main.swift:64).
pub const FLASH_BAUD: u32 = 460_800;
/// Gemergte Images beginnen beim Bootloader.
pub const FLASH_ADDRESS: u32 = 0x0;

/// Zielchip eines gemergten Images (Layout aus scripts/build_firmware.sh).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum TargetChip {
    /// CYD: ESP32 mit 4 MB, Bootloader an 0x1000.
    Esp32,
    /// Guition 4848S040: ESP32-S3 mit 16 MB, Bootloader an 0x0.
    Esp32S3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DeviceProbe {
    pub chip: TargetChip,
    pub flash_size_bytes: Option<u64>,
}

fn flash_size_bytes(size: FlashSize) -> u64 {
    match size {
        FlashSize::_256Kb => 256 * 1024,
        FlashSize::_512Kb => 512 * 1024,
        FlashSize::_1Mb => 1024 * 1024,
        FlashSize::_2Mb => 2 * 1024 * 1024,
        FlashSize::_4Mb => 4 * 1024 * 1024,
        FlashSize::_8Mb => 8 * 1024 * 1024,
        FlashSize::_16Mb => 16 * 1024 * 1024,
        FlashSize::_32Mb => 32 * 1024 * 1024,
        FlashSize::_64Mb => 64 * 1024 * 1024,
        FlashSize::_128Mb => 128 * 1024 * 1024,
        FlashSize::_256Mb => 256 * 1024 * 1024,
        _ => 0,
    }
}

impl TargetChip {
    pub fn max_image_bytes(self) -> u64 {
        match self {
            TargetChip::Esp32 => 0x400000,
            TargetChip::Esp32S3 => 0x1000000,
        }
    }

    fn bootloader_offset(self) -> usize {
        match self {
            TargetChip::Esp32 => 0x1000,
            TargetChip::Esp32S3 => 0x0,
        }
    }

    /// `chip_id` im Image-Header (Byte 12–13).
    fn image_chip_id(self) -> u16 {
        match self {
            TargetChip::Esp32 => 0,
            TargetChip::Esp32S3 => 9,
        }
    }

    fn espflash(self) -> Chip {
        match self {
            TargetChip::Esp32 => Chip::Esp32,
            TargetChip::Esp32S3 => Chip::Esp32s3,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImageValidationError {
    Size,
    Layout,
}

/// Prüft Größe, Bootloader, Partitionstabelle und App sowie die Chip-Kennung
/// im Bootloader-Header — so landet kein Image auf dem falschen Board.
pub fn validate_merged_image(image: &[u8], chip: TargetChip) -> Result<(), ImageValidationError> {
    if image.len() < 0x11000 || image.len() as u64 > chip.max_image_bytes() {
        return Err(ImageValidationError::Size);
    }
    let boot = chip.bootloader_offset();
    let chip_id = u16::from_le_bytes([image[boot + 12], image[boot + 13]]);
    if image[boot] != 0xE9
        || chip_id != chip.image_chip_id()
        || image[0x8000..0x8002] != [0xAA, 0x50]
        || image[0x10000] != 0xE9
    {
        return Err(ImageValidationError::Layout);
    }
    Ok(())
}

#[derive(Debug, thiserror::Error)]
pub enum FlashError {
    #[error("Port konnte nicht geöffnet werden: {0}")]
    Open(String),
    #[error("Keine Verbindung zum ESP32-Bootloader: {0}")]
    Connect(String),
    #[error("Schreiben fehlgeschlagen: {0}")]
    Write(String),
    #[error("Leeres Firmware-Image")]
    EmptyImage,
}

/// Fortschritt, wie ihn die Oberfläche anzeigt (main.swift: FirmwareFlashPhase).
#[derive(Debug, Clone, PartialEq, serde::Serialize)]
#[serde(tag = "phase", rename_all = "camelCase")]
pub enum FlashEvent {
    Connecting,
    Connected { chip: String },
    Erasing,
    Writing { written: usize, total: usize },
    Verifying,
    Rebooting,
    Done,
}

impl FlashEvent {
    /// Prozent innerhalb der Schreibphase, sonst `None`.
    pub fn percent(&self) -> Option<u32> {
        match self {
            FlashEvent::Writing { written, total } if *total > 0 => Some(
                ((*written as f64 / *total as f64) * 100.0)
                    .round()
                    .min(100.0) as u32,
            ),
            _ => None,
        }
    }
}

struct Progress<'a> {
    total: usize,
    on_event: &'a mut dyn FnMut(FlashEvent),
}

impl ProgressCallbacks for Progress<'_> {
    fn init(&mut self, _addr: u32, len: usize) {
        self.total = len;
        (self.on_event)(FlashEvent::Erasing);
        (self.on_event)(FlashEvent::Writing {
            written: 0,
            total: len,
        });
    }

    fn update(&mut self, current: usize) {
        (self.on_event)(FlashEvent::Writing {
            written: current.min(self.total),
            total: self.total,
        });
    }

    fn verifying(&mut self) {
        (self.on_event)(FlashEvent::Verifying);
    }

    fn finish(&mut self, _skipped: bool) {
        (self.on_event)(FlashEvent::Writing {
            written: self.total,
            total: self.total,
        });
    }
}

fn usb_info_for(port_name: &str) -> UsbPortInfo {
    serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .find(|p| p.port_name == port_name)
        .and_then(|p| match p.port_type {
            SerialPortType::UsbPort(info) => Some(info),
            _ => None,
        })
        .unwrap_or(UsbPortInfo {
            vid: 0,
            pid: 0,
            serial_number: None,
            manufacturer: None,
            product: None,
        })
}

/// Chip am Port auslesen und danach neu starten. Trennt das S3-Board von den
/// CYDs; ILI9341 und ST7789 unterscheidet es nicht, beide CYDs tragen
/// denselben ESP32. `Ok(None)` bei einem anderen Espressif-Chip. Der Port muss
/// frei sein.
pub fn detect_chip(port_name: &str) -> Result<Option<TargetChip>, FlashError> {
    Ok(probe_device(port_name)?.map(|probe| probe.chip))
}

/// Identify the Espressif chip and physical SPI flash capacity without
/// writing flash. The bootloader connection is hard-reset before returning.
pub fn probe_device(port_name: &str) -> Result<Option<DeviceProbe>, FlashError> {
    let serial = serialport::new(port_name, 115_200)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_secs(3))
        .open_native()
        .map_err(|e| FlashError::Open(e.to_string()))?;

    let connection = Connection::new(
        serial,
        usb_info_for(port_name),
        ResetAfterOperation::HardReset,
        ResetBeforeOperation::DefaultReset,
        115_200,
    );

    // Ohne Stub und ohne Chip-Vorgabe: espflash liest nur die Kennung.
    let mut flasher = Flasher::connect(connection, false, false, false, None, None)
        .map_err(|e| FlashError::Connect(e.to_string()))?;
    let chip = flasher.chip();
    let size = flasher
        .flash_detect()
        .ok()
        .flatten()
        .map(flash_size_bytes)
        .filter(|size| *size > 0);
    flasher
        .connection()
        .reset_after(false, chip)
        .map_err(|e| FlashError::Connect(e.to_string()))?;
    Ok(target_for(chip).map(|chip| DeviceProbe {
        chip,
        flash_size_bytes: size,
    }))
}

fn target_for(chip: Chip) -> Option<TargetChip> {
    match chip {
        Chip::Esp32 => Some(TargetChip::Esp32),
        Chip::Esp32s3 => Some(TargetChip::Esp32S3),
        _ => None,
    }
}

/// Image ab Offset 0 schreiben, verifizieren, Hard-Reset. Der Port muss
/// frei sein; der Aufrufer trennt vorher die normale Verbindung.
pub fn flash_image(
    port_name: &str,
    image: &[u8],
    baud: u32,
    chip: TargetChip,
    on_event: &mut dyn FnMut(FlashEvent),
) -> Result<(), FlashError> {
    if image.is_empty() {
        return Err(FlashError::EmptyImage);
    }
    on_event(FlashEvent::Connecting);

    let serial = serialport::new(port_name, 115_200)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_secs(3))
        .open_native()
        .map_err(|e| FlashError::Open(e.to_string()))?;

    let connection = Connection::new(
        serial,
        usb_info_for(port_name),
        ResetAfterOperation::HardReset,
        ResetBeforeOperation::DefaultReset,
        115_200,
    );

    let mut flasher = Flasher::connect(
        connection,
        true,
        true,
        false,
        Some(chip.espflash()),
        Some(baud),
    )
    .map_err(|e| FlashError::Connect(e.to_string()))?;
    let chip = flasher.chip();
    on_event(FlashEvent::Connected {
        chip: format!("{chip:?}"),
    });

    let mut progress = Progress {
        total: image.len(),
        on_event,
    };
    flasher
        .write_bin_to_flash(FLASH_ADDRESS, image, &mut progress)
        .map_err(|e| FlashError::Write(e.to_string()))?;

    on_event(FlashEvent::Rebooting);
    flasher
        .connection()
        .reset_after(true, chip)
        .map_err(|e| FlashError::Write(e.to_string()))?;
    drop(flasher);
    on_event(FlashEvent::Done);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_supported_chips_are_targets() {
        assert_eq!(target_for(Chip::Esp32), Some(TargetChip::Esp32));
        assert_eq!(target_for(Chip::Esp32s3), Some(TargetChip::Esp32S3));
        assert_eq!(target_for(Chip::Esp32c3), None);
    }

    #[test]
    fn flash_sizes_are_reported_in_bytes() {
        assert_eq!(flash_size_bytes(FlashSize::_4Mb), 4 * 1024 * 1024);
        assert_eq!(flash_size_bytes(FlashSize::_16Mb), 16 * 1024 * 1024);
    }

    #[test]
    fn percent_only_while_writing() {
        assert_eq!(
            FlashEvent::Writing {
                written: 250,
                total: 1000
            }
            .percent(),
            Some(25)
        );
        assert_eq!(
            FlashEvent::Writing {
                written: 0,
                total: 0
            }
            .percent(),
            None
        );
        assert_eq!(FlashEvent::Verifying.percent(), None);
    }

    #[test]
    fn empty_image_is_rejected_before_touching_the_port() {
        let mut events = Vec::new();
        let r = flash_image(
            "/dev/does-not-exist",
            &[],
            FLASH_BAUD,
            TargetChip::Esp32,
            &mut |e| events.push(e),
        );
        assert!(matches!(r, Err(FlashError::EmptyImage)));
        assert!(events.is_empty());
    }

    #[test]
    fn merged_image_validation_distinguishes_app_image() {
        let mut image = vec![0xff; 0x11000];
        image[0x1000] = 0xe9;
        image[0x100c..0x100e].copy_from_slice(&[0, 0]);
        image[0x8000..0x8002].copy_from_slice(&[0xaa, 0x50]);
        image[0x10000] = 0xe9;
        assert!(validate_merged_image(&image, TargetChip::Esp32).is_ok());
        // Dasselbe Image passt nicht zum S3: Bootloader liegt dort an 0x0.
        assert_eq!(
            validate_merged_image(&image, TargetChip::Esp32S3),
            Err(ImageValidationError::Layout)
        );
        image[0x1000] = 0xff;
        assert_eq!(
            validate_merged_image(&image, TargetChip::Esp32),
            Err(ImageValidationError::Layout)
        );
        assert_eq!(
            validate_merged_image(&vec![0xe9; 0x10000], TargetChip::Esp32),
            Err(ImageValidationError::Size)
        );
        assert_eq!(
            validate_merged_image(
                &vec![0xe9; TargetChip::Esp32.max_image_bytes() as usize + 1],
                TargetChip::Esp32
            ),
            Err(ImageValidationError::Size)
        );
    }

    #[test]
    fn parses_and_verifies_versioned_manifest() {
        let bytes = br#"{
          "schemaVersion":1,
          "generatedAt":"2026-10-31T00:00:00Z",
          "releases":[{
            "firmwareVersion":"2.28.0","hardwareModel":"EchoDesk Mini",
            "hardwareId":"freenove-fnk0114a-2p4","supportedChip":"esp32",
            "imageType":"merged","flashAddress":0,"flashMode":"dio",
            "flashFrequency":"40m","flashSize":"4MB",
            "imageFile":"echodesk-mini/2.28.0/echodesk-os.bin","downloadUrl":null,
            "sha256":"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
            "size":3,"releaseNotes":"First consumer release.",
            "minimumDesktopVersion":"0.2.0"
          }]
        }"#;
        let manifest = parse_firmware_manifest(bytes).unwrap();
        assert_eq!(manifest.releases[0].flash_address, 0);
        assert!(verify_sha256(b"abc", &manifest.releases[0].sha256));
        assert!(!verify_sha256(b"abd", &manifest.releases[0].sha256));
    }

    #[test]
    fn manifest_rejects_traversal_non_https_and_unknown_fields() {
        let template = |file: &str, url: &str, extra: &str| {
            format!(
                r#"{{
          "schemaVersion":1,"generatedAt":"x","releases":[{{
            "firmwareVersion":"1","hardwareModel":"M","hardwareId":"h",
            "supportedChip":"esp32","imageType":"merged","flashAddress":0,
            "flashMode":"dio","flashFrequency":"40m","flashSize":"4MB",
            "imageFile":"{file}","downloadUrl":{url},
            "sha256":"{}","size":1,"releaseNotes":"x",
            "minimumDesktopVersion":"0.2.0"{extra}}}]}}"#,
                "A".repeat(64)
            )
        };
        assert_eq!(
            parse_firmware_manifest(template("../evil.bin", "null", "").as_bytes()),
            Err(ManifestError::Release)
        );
        assert_eq!(
            parse_firmware_manifest(template("x.bin", "\"http://example/x\"", "").as_bytes()),
            Err(ManifestError::Source)
        );
        assert_eq!(
            parse_firmware_manifest(template("x.bin", "null", ",\"address\":7").as_bytes()),
            Err(ManifestError::Json)
        );
    }

    #[test]
    fn s3_image_needs_bootloader_at_zero_with_s3_chip_id() {
        let mut image = vec![0xff; 0x11000];
        image[0] = 0xe9;
        image[12..14].copy_from_slice(&9u16.to_le_bytes());
        image[0x8000..0x8002].copy_from_slice(&[0xaa, 0x50]);
        image[0x10000] = 0xe9;
        assert!(validate_merged_image(&image, TargetChip::Esp32S3).is_ok());
        // ESP32-Kennung im Header: falsches Board.
        image[12..14].copy_from_slice(&0u16.to_le_bytes());
        assert_eq!(
            validate_merged_image(&image, TargetChip::Esp32S3),
            Err(ImageValidationError::Layout)
        );
        // 16 MB sind für den S3 erlaubt, für den ESP32 nicht.
        assert_eq!(
            validate_merged_image(&vec![0xe9; 0x400001], TargetChip::Esp32),
            Err(ImageValidationError::Size)
        );
    }
}
