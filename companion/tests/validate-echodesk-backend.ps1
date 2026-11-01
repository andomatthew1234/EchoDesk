$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$firmware = Join-Path $repo 'desktop\firmware\echodesk-mini\2.31.0\echodesk-os.bin'
$manifestPath = Join-Path $repo 'desktop\firmware\firmware-manifest.json'
$wifiSource = Join-Path $repo 'companion\Sources\EchoDeskWifiSetup.swift'
$bundleSource = Join-Path $repo 'companion\Sources\EchoDeskFirmwareBundle.swift'
$updatesSource = Join-Path $repo 'companion\Sources\EchoDeskFirmwareUpdates.swift'
$mainSource = Join-Path $repo 'companion\Sources\main.swift'
$buildScript = Join-Path $repo 'companion\build.sh'

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

Assert-True (Test-Path -LiteralPath $firmware) 'EchoDesk OS 2.31.0 image is missing.'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$release = @($manifest.releases) | Where-Object firmwareVersion -eq '2.31.0' | Select-Object -First 1
Assert-True ($null -ne $release) 'Firmware manifest has no EchoDesk OS 2.31.0 release.'

$bytes = [IO.File]::ReadAllBytes($firmware)
$digest = (Get-FileHash -LiteralPath $firmware -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-True ($bytes.Length -eq 1516400) "Unexpected firmware size: $($bytes.Length)."
Assert-True ($digest -eq '77dd25b6bb32cd059807135a9db9577b7f7d39748f50bc7256c528bd45577175') 'Firmware SHA-256 mismatch.'
Assert-True ($release.size -eq $bytes.Length -and $release.sha256.ToLowerInvariant() -eq $digest) 'Firmware manifest does not match the image.'
Assert-True ($release.supportedChip -eq 'esp32' -and $release.imageType -eq 'merged' -and $release.flashAddress -eq 0) 'Firmware manifest has an unsafe flash target/layout.'
Assert-True ($bytes[0x1000] -eq 0xE9 -and $bytes[0x100C] -eq 0 -and $bytes[0x100D] -eq 0) 'Merged image is not for classic ESP32.'
Assert-True ($bytes[0x8000] -eq 0xAA -and $bytes[0x8001] -eq 0x50 -and $bytes[0x10000] -eq 0xE9) 'Merged image partition/app layout is invalid.'

$wifi = Get-Content -LiteralPath $wifiSource -Raw
foreach ($required in @(
    '39_320', '39_321', '192.168.4.1', 'Freenove FNK0114A_2P4',
    'SecRandomCopyBytes', 'kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly',
    'echodesk_discover', 'echodesk_provision', 'echodesk_identify'
)) {
    Assert-True ($wifi.Contains($required)) "Wi-Fi backend contract is missing '$required'."
}
Assert-True (-not $wifi.Contains('print(')) 'Wi-Fi backend must not print credentials or pairing tokens.'

$bundle = Get-Content -LiteralPath $bundleSource -Raw
Assert-True ($bundle.Contains('SHA256.hash') -and $bundle.Contains('installValidatedCopy')) 'Firmware bundle is not validated before use.'

$updates = Get-Content -LiteralPath $updatesSource -Raw
foreach ($required in @(
    'Curve25519.Signing.PublicKey', 'isValidSignature',
    'mmy3V6WTCrARRl9JmI7m1D5sFUv87CzJp0dCKJgHPOc=',
    'fw-v', 'fw-beta-v', 'firmware-update-manifest.json.sig',
    'freenove-fnk0114a-2p4', 'validateCachedImage', 'trustedAssetURL',
    'enforceRollbackProtection'
)) {
    Assert-True ($updates.Contains($required)) "Trusted macOS firmware updater is missing '$required'."
}
Assert-True ($updates.Contains('/andomatthew1234/EchoDesk/releases/download/')) 'Remote firmware assets are not confined to the EchoDesk repository and tag.'
Assert-True ($updates.Contains('EchoDeskFirmwareBundle.validateLayout')) 'Remote firmware is not layout-checked before caching.'

$main = Get-Content -LiteralPath $mainSource -Raw
foreach ($prefix in @('cu.usbserial-', 'cu.wchusbserial', 'cu.usbmodem')) {
    Assert-True ($main.Contains($prefix)) "macOS serial enumeration is missing '$prefix'."
}
Assert-True ($main.Contains('let kDisplayVariantDefault = kDisplayVariantST7789')) 'EchoDesk build is not pinned to its ST7789 panel.'
Assert-True ($main.Contains('try? EchoDeskFirmwareBundle.validate(image)')) 'Cached EchoDesk OS is not revalidated immediately before flashing.'

$build = Get-Content -LiteralPath $buildScript -Raw
foreach ($required in @(
    '-D ECHODESK_DESKTOP', 'EchoDesk-Info.plist', '0.2.8',
    'EchoDesk-Desktop-v${APP_VERSION}-macos-universal',
    'EchoDeskWifiSetup.swift', 'EchoDeskFirmwareBundle.swift', 'EchoDeskFirmwareUpdates.swift',
    'SettingsWindow+EchoDeskDesktop.swift', 'SettingsWindow+EchoDeskWifiSetup.swift'
)) {
    Assert-True ($build.Contains($required)) "macOS build integration is missing '$required'."
}

Write-Host 'EchoDesk macOS backend validation passed.'
