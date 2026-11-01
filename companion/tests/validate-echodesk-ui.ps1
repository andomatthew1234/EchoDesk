$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourcePath = Join-Path $repo 'companion\Sources\SettingsWindow+EchoDeskDesktop.swift'
$mainPath = Join-Path $repo 'companion\Sources\main.swift'
$buildPath = Join-Path $repo 'companion\build.sh'
$plistPath = Join-Path $repo 'companion\Resources\EchoDesk-Info.plist'
$workflowPath = Join-Path $repo '.github\workflows\mac-app.yml'
$dmgBackgroundPath = Join-Path $repo 'companion\Resources\DMG\EchoDesk-DMG-Background.png'

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
    'Setup EchoDesk Now',
    'Welcome to the EchoDesk Desktop app', 'Setup your first EchoDesk now?',
    'Continue', 'Yes', 'Not now'
)) { Assert-Contains $source $copy }

foreach ($stateFeature in @(
    'EchoDeskSetupStep', 'EchoDeskPendingSetupPortV2', 'continueEchoDeskSetupAfterReconnect',
    'echoDeskShowUnknownDevices', 'EchoDeskWiFiSetupManager.shared',
    'performEchoDeskVerifiedRecovery', 'kFirmwareChipESP32',
    'EchoDeskPopupButton', 'heart.text.square', 'Quick settings', '12-hour', '24-hour',
    'echoDeskSetupFlashSucceeded', 'echoDeskRecoveryFlashSucceeded',
    'fw.lastFlashPort == expectedPort', 'monitor?.serialPort.connectedPort == expectedPort',
    'Settings.shared.manualPortPath = port', 'DispatchQueue.main.async',
    'applyDeferredEchoDeskQuickSettingsIfNeeded', 'EchoDeskPendingTheme',
    'EchoDeskPendingWiFiDeviceID', 'echoDeskLastStatePort == monitor?.serialPort.connectedPort',
    'profile.mac.replacingOccurrences(of: ":", with: "").lowercased() == pendingDeviceID',
    'echoDeskResponseMatches', 'json["command"] as? String == command',
    'EchoDeskFirstRunOnboardingCompletedV1', 'presentEchoDeskFirstRunOnboardingIfNeeded',
    'completeEchoDeskFirstRunOnboarding(openSetup: true)',
    'completeEchoDeskFirstRunOnboarding(openSetup: false)',
    'UserDefaults.standard.set(true, forKey: echoDeskFirstRunOnboardingKey)'
)) { Assert-Contains $source $stateFeature }

$onboardingStart = $source.IndexOf('private func presentEchoDeskFirstRunOnboardingIfNeeded')
$onboardingEnd = $source.IndexOf('private func buildEchoDeskSidebar', $onboardingStart)
if ($onboardingStart -lt 0 -or $onboardingEnd -le $onboardingStart) {
    throw 'The macOS first-run onboarding implementation could not be isolated.'
}
$onboarding = $source.Substring($onboardingStart, $onboardingEnd - $onboardingStart)
if ($onboarding.Contains('monitor') -or $onboarding.Contains('Host')) {
    throw 'First-run onboarding must not depend on the Host or a connected device.'
}
foreach ($resumeGuard in @(
    'defaults.string(forKey: "EchoDeskPendingSetupPortV2") != nil',
    'defaults.integer(forKey: "EchoDeskGuidedSetupStepV2") != EchoDeskSetupStep.connect.rawValue',
    'guard !pendingUSBSetup'
)) {
    if (-not $onboarding.Contains($resumeGuard)) {
        throw "First-run onboarding can overwrite a pending guided setup: missing $resumeGuard"
    }
}
if ($source.IndexOf('Welcome to the EchoDesk Desktop app') -gt $source.IndexOf('Setup your first EchoDesk now?')) {
    throw 'First-run onboarding screens are not in the required order.'
}
if ($main.Contains('EchoDeskDesktopIntroducedV027')) {
    throw 'The obsolete automatic Setup-page first-launch gate is still active.'
}

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

if (-not (Test-Path -LiteralPath $dmgBackgroundPath)) {
    throw 'EchoDesk DMG background asset is missing.'
}
$png = [IO.File]::ReadAllBytes($dmgBackgroundPath)
if ($png.Length -lt 24 -or $png[0] -ne 0x89 -or $png[1] -ne 0x50 -or
    $png[2] -ne 0x4E -or $png[3] -ne 0x47) {
    throw 'EchoDesk DMG background is not a valid PNG.'
}
$pngWidth = (([int]$png[16]) -shl 24) -bor (([int]$png[17]) -shl 16) -bor (([int]$png[18]) -shl 8) -bor ([int]$png[19])
$pngHeight = (([int]$png[20]) -shl 24) -bor (([int]$png[21]) -shl 16) -bor (([int]$png[22]) -shl 8) -bor ([int]$png[23])
if ($pngWidth -ne 1536 -or $pngHeight -ne 1024) {
    throw "EchoDesk DMG background must be 1536x1024, found ${pngWidth}x${pngHeight}."
}
foreach ($dmgContract in @(
    'if [ "$PRODUCT_MODE" = "echodesk" ]',
    'EchoDesk-DMG-Background.png', 'hdiutil create -ov -size',
    '/usr/bin/hdiutil attach "$DMG_RW_PATH" -readwrite', '/usr/bin/osascript <<OSA',
    'set toolbar visible of container window to false',
    'set statusbar visible of container window to false',
    'set pathbar visible of container window to false',
    'set sidebar width of container window to 0',
    'set bounds of container window to {100, 100, 868, 612}',
    'set icon size of viewOptions to 128',
    'set background picture of viewOptions to file ".background:EchoDesk-DMG-Background.png"',
    'set position of item "$APP_NAME.app" to {168, 265}',
    'set position of item "Applications" to {600, 265}',
    'with timeout of 30 seconds',
    'for DMG_FINDER_ATTEMPT in 1 2 3 4',
    '/usr/bin/hdiutil info | grep -Fq -- "$DMG_MOUNT_DIR"',
    'if configure_echodesk_dmg_finder',
    'if [ "$DMG_FINDER_CONFIGURED" != "1" ]',
    '/usr/bin/hdiutil convert "$DMG_RW_PATH" -format UDZO',
    'ERROR: Finder could not configure the EchoDesk DMG layout',
    '# Preserve the original AI Monitor packaging path.'
)) { Assert-Contains $build $dmgContract }

$versionNode = $plist.SelectSingleNode("//key[.='CFBundleShortVersionString']/following-sibling::*[1]")
if ($null -eq $versionNode -or $versionNode.InnerText -ne '0.2.7') {
    throw 'EchoDesk Info.plist version is not 0.2.7.'
}

Write-Host 'EchoDesk macOS 0.2.7 parity UI validation passed.'
