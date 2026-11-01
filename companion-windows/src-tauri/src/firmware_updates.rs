//! Authenticated EchoDesk OS release discovery.
//!
//! GitHub is only the transport. A release becomes installable after its raw
//! manifest bytes verify against the pinned EchoDesk Ed25519 key and every
//! device, version and asset binding below passes.

use aimonitor_flash::{parse_firmware_manifest, FirmwareRelease};
use base64::Engine;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use serde::Deserialize;

const RELEASES_API: &str =
    "https://api.github.com/repos/andomatthew1234/EchoDesk/releases?per_page=30";
const REPOSITORY_DOWNLOAD_PREFIX: &str =
    "https://github.com/andomatthew1234/EchoDesk/releases/download/";
const MANIFEST_ASSET: &str = "firmware-update-manifest.json";
const SIGNATURE_ASSET: &str = "firmware-update-manifest.json.sig";
const USER_AGENT: &str = "EchoDesk-Desktop-Firmware-Updater";
const MAX_RELEASES_BYTES: u64 = 512 * 1024;
const MAX_MANIFEST_BYTES: u64 = 128 * 1024;
const MAX_SIGNATURE_BYTES: u64 = 1024;

pub const PINNED_PUBLIC_KEY_BASE64: &str = "mmy3V6WTCrARRl9JmI7m1D5sFUv87CzJp0dCKJgHPOc=";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiscoveryError {
    /// GitHub was unavailable. The caller may use its verified bundled image.
    Unavailable,
    /// GitHub answered, but the release failed the EchoDesk trust contract.
    Untrusted,
}

#[derive(Debug, Deserialize)]
struct GitHubAsset {
    name: String,
    browser_download_url: String,
}

#[derive(Debug, Deserialize)]
struct GitHubRelease {
    tag_name: String,
    draft: bool,
    prerelease: bool,
    assets: Vec<GitHubAsset>,
}

fn get_limited(url: &str, limit: u64) -> Result<Vec<u8>, DiscoveryError> {
    let mut response = ureq::get(url)
        .header("Accept", "application/vnd.github+json")
        .header("User-Agent", USER_AGENT)
        .call()
        .map_err(|_| DiscoveryError::Unavailable)?;
    response
        .body_mut()
        .with_config()
        .limit(limit)
        .read_to_vec()
        .map_err(|_| DiscoveryError::Unavailable)
}

fn parse_stable_version(tag: &str) -> Option<&str> {
    let version = tag.strip_prefix("fw-v")?;
    let mut parts = version.split('.');
    let valid = (0..3).all(|_| {
        parts.next().is_some_and(|part| {
            !part.is_empty()
                && part.bytes().all(|byte| byte.is_ascii_digit())
                && (part == "0" || !part.starts_with('0'))
                && part.parse::<u32>().is_ok()
        })
    });
    (valid && parts.next().is_none()).then_some(version)
}

fn signature_bytes(bytes: &[u8]) -> Result<[u8; 64], DiscoveryError> {
    let decoded;
    let bytes = if bytes.len() == 64 {
        bytes
    } else {
        decoded = base64::engine::general_purpose::STANDARD
            .decode(String::from_utf8_lossy(bytes).trim())
            .map_err(|_| DiscoveryError::Untrusted)?;
        decoded.as_slice()
    };
    bytes.try_into().map_err(|_| DiscoveryError::Untrusted)
}

fn verify_with_key(
    manifest: &[u8],
    signature: &[u8],
    key_bytes: &[u8],
) -> Result<(), DiscoveryError> {
    let key = VerifyingKey::from_bytes(
        &key_bytes
            .try_into()
            .map_err(|_| DiscoveryError::Untrusted)?,
    )
    .map_err(|_| DiscoveryError::Untrusted)?;
    let signature = Signature::from_bytes(&signature_bytes(signature)?);
    key.verify(manifest, &signature)
        .map_err(|_| DiscoveryError::Untrusted)
}

fn verify_manifest(manifest: &[u8], signature: &[u8]) -> Result<(), DiscoveryError> {
    let key_bytes = base64::engine::general_purpose::STANDARD
        .decode(PINNED_PUBLIC_KEY_BASE64)
        .map_err(|_| DiscoveryError::Untrusted)?;
    verify_with_key(manifest, signature, &key_bytes)
}

fn asset<'a>(release: &'a GitHubRelease, name: &str) -> Result<&'a GitHubAsset, DiscoveryError> {
    release
        .assets
        .iter()
        .find(|asset| asset.name == name)
        .ok_or(DiscoveryError::Untrusted)
}

fn expected_asset_url(tag: &str, asset_name: &str) -> String {
    format!("{REPOSITORY_DOWNLOAD_PREFIX}{tag}/{asset_name}")
}

fn validate_asset_url(tag: &str, asset: &GitHubAsset) -> Result<(), DiscoveryError> {
    (asset.browser_download_url == expected_asset_url(tag, &asset.name))
        .then_some(())
        .ok_or(DiscoveryError::Untrusted)
}

fn bind_manifest(
    release: &GitHubRelease,
    bytes: &[u8],
    expected_model: &str,
    expected_hardware_id: &str,
) -> Result<FirmwareRelease, DiscoveryError> {
    let tag_version = parse_stable_version(&release.tag_name).ok_or(DiscoveryError::Untrusted)?;
    let manifest = parse_firmware_manifest(bytes).map_err(|_| DiscoveryError::Untrusted)?;
    let candidates: Vec<_> = manifest
        .releases
        .into_iter()
        .filter(|candidate| {
            candidate.firmware_version == tag_version
                && candidate.hardware_model == expected_model
                && candidate.hardware_id == expected_hardware_id
                && candidate.supported_chip == "esp32"
                && candidate.image_type == "merged"
                && candidate.flash_address == 0
                && candidate.flash_mode.eq_ignore_ascii_case("dio")
                && candidate.flash_frequency.eq_ignore_ascii_case("40m")
                && candidate.flash_size.eq_ignore_ascii_case("4MB")
        })
        .collect();
    if candidates.len() != 1 {
        return Err(DiscoveryError::Untrusted);
    }
    let candidate = candidates.into_iter().next().unwrap();
    let image_asset = release
        .assets
        .iter()
        .find(|asset| {
            candidate.download_url.as_deref() == Some(asset.browser_download_url.as_str())
        })
        .ok_or(DiscoveryError::Untrusted)?;
    validate_asset_url(&release.tag_name, image_asset)?;
    Ok(candidate)
}

fn release_order(left: &GitHubRelease, right: &GitHubRelease) -> std::cmp::Ordering {
    let left = parse_stable_version(&left.tag_name).unwrap_or("0.0.0");
    let right = parse_stable_version(&right.tag_name).unwrap_or("0.0.0");
    aimonitor_core::semver::compare(left, right)
}

/// Returns the latest authenticated stable EchoDesk Mini release. `Ok(None)`
/// means the repository does not yet contain a firmware release.
pub fn latest_stable(
    expected_model: &str,
    expected_hardware_id: &str,
) -> Result<Option<FirmwareRelease>, DiscoveryError> {
    let body = get_limited(RELEASES_API, MAX_RELEASES_BYTES)?;
    let mut releases: Vec<GitHubRelease> =
        serde_json::from_slice(&body).map_err(|_| DiscoveryError::Untrusted)?;
    releases.retain(|release| {
        !release.draft && !release.prerelease && parse_stable_version(&release.tag_name).is_some()
    });
    releases.sort_by(release_order);
    let Some(release) = releases.last() else {
        return Ok(None);
    };
    let manifest_asset = asset(release, MANIFEST_ASSET)?;
    let signature_asset = asset(release, SIGNATURE_ASSET)?;
    validate_asset_url(&release.tag_name, manifest_asset)?;
    validate_asset_url(&release.tag_name, signature_asset)?;
    let manifest = get_limited(&manifest_asset.browser_download_url, MAX_MANIFEST_BYTES)?;
    let signature = get_limited(&signature_asset.browser_download_url, MAX_SIGNATURE_BYTES)?;
    verify_manifest(&manifest, &signature)?;
    bind_manifest(release, &manifest, expected_model, expected_hardware_id).map(Some)
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};

    fn release(tag: &str, image_url: &str) -> GitHubRelease {
        GitHubRelease {
            tag_name: tag.into(),
            draft: false,
            prerelease: false,
            assets: vec![GitHubAsset {
                name: "echodesk-os-freenove-fnk0114a-2p4-v2.33.0.bin".into(),
                browser_download_url: image_url.into(),
            }],
        }
    }

    fn manifest(url: &str) -> Vec<u8> {
        format!(r#"{{"schemaVersion":1,"generatedAt":"2026-11-01T00:00:00Z","releases":[{{"firmwareVersion":"2.33.0","hardwareModel":"EchoDesk Mini","hardwareId":"freenove-fnk0114a-2p4","supportedChip":"esp32","imageType":"merged","flashAddress":0,"flashMode":"dio","flashFrequency":"40m","flashSize":"4MB","imageFile":"echodesk-mini/2.33.0/echodesk-os.bin","downloadUrl":"{url}","sha256":"{}","size":1516400,"releaseNotes":"Test","minimumDesktopVersion":"0.2.9"}}]}}"#, "A".repeat(64)).into_bytes()
    }

    #[test]
    fn stable_tags_are_strict() {
        assert_eq!(parse_stable_version("fw-v2.33.0"), Some("2.33.0"));
        for tag in [
            "v2.33.0",
            "fw-beta-v2.33.0",
            "fw-v2.032.0",
            "fw-v2.32",
            "fw-v2.33.0-rc.1",
        ] {
            assert_eq!(parse_stable_version(tag), None, "{tag}");
        }
    }

    #[test]
    fn manifest_is_bound_to_tag_hardware_and_asset() {
        let tag = "fw-v2.33.0";
        let image_name = "echodesk-os-freenove-fnk0114a-2p4-v2.33.0.bin";
        let url = expected_asset_url(tag, image_name);
        let release = release(tag, &url);
        let candidate = bind_manifest(
            &release,
            &manifest(&url),
            "EchoDesk Mini",
            "freenove-fnk0114a-2p4",
        )
        .unwrap();
        assert_eq!(candidate.firmware_version, "2.33.0");

        let wrong_origin = manifest("https://example.com/firmware.bin");
        assert_eq!(
            bind_manifest(
                &release,
                &wrong_origin,
                "EchoDesk Mini",
                "freenove-fnk0114a-2p4"
            ),
            Err(DiscoveryError::Untrusted)
        );
        assert_eq!(
            bind_manifest(&release, &manifest(&url), "EchoDesk Mini", "other-board"),
            Err(DiscoveryError::Untrusted)
        );
    }

    #[test]
    fn detached_ed25519_verification_rejects_tampering() {
        let signing = SigningKey::from_bytes(&[7u8; 32]);
        let message = b"signed manifest bytes";
        let signature = signing.sign(message).to_bytes();
        let verifying = signing.verifying_key().to_bytes();
        assert_eq!(verify_with_key(message, &signature, &verifying), Ok(()));
        assert_eq!(
            verify_with_key(b"tampered manifest bytes", &signature, &verifying),
            Err(DiscoveryError::Untrusted)
        );
        assert_eq!(
            verify_with_key(message, &[0; 64], &verifying),
            Err(DiscoveryError::Untrusted)
        );
    }
}
