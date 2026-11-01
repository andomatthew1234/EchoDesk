$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourcePath = Join-Path $repo 'companion\Sources\SettingsWindow+EchoDeskDesktop.swift'
$mainPath = Join-Path $repo 'companion\Sources\main.swift'
$buildPath = Join-Path $repo 'companion\build.sh'
$plistPath = Join-Path $repo 'companion\Resources\EchoDesk-Info.plist'
$workflowPath = Join-Path $repo '.github\workflows\mac-app.yml'

function Assert-Contains([string] $Text, [string] $Needle) {
    if (-not $Text.Contains($Needle)) { throw "EchoDesk macOS parity UI is missing: $Needle" }
}

$source = Get-Content -LiteralPath $sourcePath -Raw
$main = Get-Content -LiteralPath $mainPath -Raw
$build = Get-Content -LiteralPath $buildPath -Raw
$workflow = Get-Content -LiteralPath $workflowPath -Raw
$plist = [xml](Get-Content -LiteralPath $plistPath -Raw)

foreach ($copy in @(
    'Overview', 'Device Control', 'Customisation', 'Setup new device',
    'Device recovery & updates',
    'Setup with USB', 'Setup EchoDesk by plugging into your PC.',
    'Setup with WiFi',
    "Setup EchoDesk without plugging into your PC. You'll still need to use USB connection for some utilities.",
    'Get started now', 'Setup EchoDesk without updating firmware.',
    'Check for updates',
    'EchoDesk will find your current device firmware and update it if a newer version is available.',
    'Welcome to EchoDesk!', 'Welcome to the community. Your new EchoDesk is ready to go.',
    'Go to home',
    'Update EchoDesk OS to the latest version', 'Install a fresh version of EchoDesk OS',
    'Repair and reset EchoDesk OS if on older firmware', 'Fully reset EchoDesk without checks',
    'Setup EchoDesk Now'
)) { Assert-Contains $source $copy }

foreach ($stateFeature in @(
    'EchoDeskSetupStep', 'EchoDeskPendingSetupPortV2', 'continueEchoDeskSetupAfterReconnect',
    'echoDeskShowUnknownDevices', 'EchoDeskWiFiSetupManager.shared',
    'performEchoDeskVerifiedRecovery', 'kFirmwareChipESP32',
    'EchoDeskPopupButton', 'Quick settings', '12-hour', '24-hour',
    'echoDeskSetupFlashSucceeded', 'echoDeskRecoveryFlashSucceeded',
    'fw.lastFlashPort == expectedPort', 'monitor?.serialPort.connectedPort == expectedPort',
    'Settings.shared.manualPortPath = port', 'DispatchQueue.main.async',
    'applyDeferredEchoDeskQuickSettingsIfNeeded', 'EchoDeskPendingTheme',
    'EchoDeskPendingWiFiDeviceID', 'echoDeskLastStatePort == monitor?.serialPort.connectedPort',
    'profile.mac.replacingOccurrences(of: ":", with: "").lowercased() == pendingDeviceID',
    'echoDeskResponseMatches', 'json["command"] as? String == command'
)) { Assert-Contains $source $stateFeature }

foreach ($mainSafetyFeature in @(
    'func runFirmwareFlash(completion:',
    'completion?(false, "Firmware update cancelled.")',
    '(payload["requestId"] as? NSNumber)?.uint64Value',
    'json["command"] as? String != expectedCommand',
    'let pendingUSBSetup = defaults.string(forKey: "EchoDeskPendingSetupPortV2") != nil'
)) { Assert-Contains $main $mainSafetyFeature }

foreach ($brokenInterpolation in @(
    'DESKTOP (kAppVersion)', 'EchoDesk OS ($0)',
    'EchoDesk Mini — (port)', 'USB Serial: (port',
    'EchoDesk joined (ssid)', 'Preparing EchoDesk OS for (port)'
)) {
    if ($source.Contains($brokenInterpolation)) {
        throw "The macOS UI still contains a broken Swift interpolation: $brokenInterpolation"
    }
}

if ($source.Contains('Terms & Privacy') -or $source.Contains('IsGuidedTermsStep')) {
    throw 'The macOS 0.2.7 setup must not contain a Terms step.'
}
Assert-Contains $main 'let kAppVersion = "0.2.7"'
Assert-Contains $main 'removeMenuBarStatusItem()'
Assert-Contains $build 'APP_VERSION="0.2.7"'
Assert-Contains $build 'SettingsWindow+EchoDeskDesktop.swift'
Assert-Contains $workflow 'EchoDesk-Desktop-v0.2.7-macos-universal'

$versionNode = $plist.SelectSingleNode("//key[.='CFBundleShortVersionString']/following-sibling::*[1]")
if ($null -eq $versionNode -or $versionNode.InnerText -ne '0.2.7') {
    throw 'EchoDesk Info.plist version is not 0.2.7.'
}

Write-Host 'EchoDesk macOS 0.2.7 parity UI validation passed.'
