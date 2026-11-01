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
    'Continue', 'Yes', 'Not now', 'My device', 'Notification Stack',
    'Not available on macOS'
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
    'UserDefaults.standard.set(true, forKey: echoDeskFirstRunOnboardingKey)',
    '("myDevice", "My device", "desktopcomputer")',
    'buttons.forEach { $0.isEnabled = ready && supportedApps.contains(app) }',
    'showEchoDeskClockUtility(requestedMode:',
    '"cmd": "echodesk_clock_get_state", "version": 1, "requestId": request',
    'sendEchoDeskCommand("echodesk_clock_control"',
    '"durationSeconds": minutes * 60',
    '"focusMinutes": focus', '"breakMinutes": pause', '"rounds": rounds',
    'UNUserNotificationCenter.current().getDeliveredNotifications'
)) { Assert-Contains $source $stateFeature }

foreach ($clockBridge in @(
    'case "echodesk_clock_config_request":',
    'self?.onClockConfigRequest?(mode)',
    'showEchoDeskClockUtility(requestedMode: mode)',
    'deviceEchoDeskClockProtocol = json["echoDeskClockProtocol"] as? Int'
)) { Assert-Contains $main $clockBridge }

# Polished desktop contracts: clock configuration is a compact, single-mode
# native flow; notification setup only persists after the device confirms it
# and is restored on reconnect; page cards share one content width.
foreach ($polishContract in @(
    'private func presentEchoDeskClockModeChooser()',
    'private func buildEchoDeskClockConfiguration(mode: String) -> NSView',
    'echoDeskClockSelectedMode = mode',
    'echoDeskClockFinishConfiguration',
    'window.styleMask = [.titled, .closable, .fullSizeContentView]',
    'private func presentEchoDeskNotificationSetup()',
    'echoDeskNotificationSetupWindow',
    'echoDeskNotificationSyncedPort',
    'UserDefaults.standard.set(true, forKey: "EchoDeskNotificationStackEnabled")',
    'syncEchoDeskNotificationPreferenceIfNeeded',
    'view.widthAnchor.constraint(equalTo: stack.widthAnchor)',
    'NSImage.SymbolConfiguration(pointSize: 16, weight: .medium)'
)) { Assert-Contains $source $polishContract }

foreach ($redesignContract in @(
    'final class EchoDeskSidebarButton: NSButton',
    'navigationGroup("General", generalButtons)',
    'navigationGroup("Device Management", managementButtons)',
    'echoDeskSidebarDeviceName',
    'EchoDeskDesign.sidebarWidth',
    'let components = echoDeskAdaptiveColumns([component, notificationCard]',
    'let deviceBasics = echoDeskAdaptiveColumns([quick, general]',
    'popup.action = #selector(echoDeskApplyPcDashboard)',
    'echoDeskPendingSetupQuickTheme = theme',
    'retryPendingEchoDeskSetupQuickSettingsIfReady()',
    'echoDeskLastStatePort == expectedPort',
    'echoDeskLastStateMAC == expectedMAC',
    'echoDeskApplyingSetupQuickSettings = true'
)) { Assert-Contains $source $redesignContract }

if ($source.Contains('NSButton(title: "Apply dashboard"')) {
    throw 'PC Dashboard still exposes a redundant Apply button instead of auto-applying changes.'
}

$overviewStart = $source.IndexOf('private func buildEchoDeskOverviewPage()')
$overviewEnd = $source.IndexOf('private func buildEchoDeskDeviceControlPage()', $overviewStart)
if ($overviewStart -lt 0 -or $overviewEnd -le $overviewStart) {
    throw 'Overview page implementation could not be isolated.'
}
$overviewPage = $source.Substring($overviewStart, $overviewEnd - $overviewStart)
foreach ($forbiddenMacNotificationControl in @('Set up notifications', 'Send test', 'Disable')) {
    if ($overviewPage.Contains($forbiddenMacNotificationControl)) {
        throw "Notification Stack must be non-interactive on macOS: $forbiddenMacNotificationControl"
    }
}
if (-not $overviewPage.Contains('notificationCard.alphaValue = 0.62')) {
    throw 'Unavailable macOS Notification Stack card is not visibly greyed out.'
}

# Regression seam for the previously inert Quick Settings Continue button.
# Continue must retain the user's choices, invalidate any pre-restart revision,
# request a state tied to the current USB identity, and resume from that exact
# state callback instead of requiring a second click.
$quickApplyStart = $source.IndexOf('@objc private func echoDeskApplySetupQuickSettings()')
$quickRetryStart = $source.IndexOf('private func retryPendingEchoDeskSetupQuickSettingsIfReady()', $quickApplyStart)
$deferredStart = $source.IndexOf('private func applyDeferredEchoDeskQuickSettingsIfNeeded()', $quickRetryStart)
if ($quickApplyStart -lt 0 -or $quickRetryStart -le $quickApplyStart -or $deferredStart -le $quickRetryStart) {
    throw 'Guided setup Quick Settings implementation could not be isolated.'
}
$quickApply = $source.Substring($quickApplyStart, $quickRetryStart - $quickApplyStart)
$quickRetry = $source.Substring($quickRetryStart, $deferredStart - $quickRetryStart)
foreach ($needle in @(
    'echoDeskPendingSetupQuickTheme = theme',
    'echoDeskPendingSetupQuickClock = clock',
    'echoDeskPendingSetupQuickPort = port',
    'echoDeskPendingSetupQuickMAC = profile.mac',
    'echoDeskLastState = nil',
    'requestEchoDeskState()'
)) {
    if (-not $quickApply.Contains($needle)) { throw "Quick Settings Continue can become inert: missing $needle" }
}
foreach ($needle in @(
    'monitor.serialPort.connectedPort == expectedPort',
    'DeviceRegistry.shared.currentProfile()?.mac == expectedMAC',
    'echoDeskLastStatePort == expectedPort',
    'echoDeskLastStateMAC == expectedMAC',
    'let revision = echoDeskLastState?["revision"] as? Int',
    'self.completeEchoDeskGuidedSetup()'
)) {
    if (-not $quickRetry.Contains($needle)) { throw "Quick Settings retry is not identity/revision safe: missing $needle" }
}
$stateApply = $source.IndexOf('self.echoDeskLastState = json')
$retryCall = $source.IndexOf('self.retryPendingEchoDeskSetupQuickSettingsIfReady()', $stateApply)
if ($stateApply -lt 0 -or $retryCall -le $stateApply) {
    throw 'A fresh EchoDesk state does not automatically resume Quick Settings Continue.'
}

if ($source.Contains('let content = echoDeskPageStack([heading, detail, stopwatch, countdown, focus])')) {
    throw 'The macOS Clock window still renders every mode at once.'
}
$notificationSetupStart = $source.IndexOf('@objc private func echoDeskSetupNotifications()')
$notificationSetupEnd = $source.IndexOf('@objc private func echoDeskDisableNotifications()', $notificationSetupStart)
if ($notificationSetupStart -lt 0 -or $notificationSetupEnd -le $notificationSetupStart) {
    throw 'Notification setup implementation could not be isolated.'
}
$notificationSetup = $source.Substring($notificationSetupStart, $notificationSetupEnd - $notificationSetupStart)
if ($notificationSetup.IndexOf('UserDefaults.standard.set(true') -lt $notificationSetup.IndexOf('guard ok else')) {
    throw 'Notification setup is persisted before EchoDesk confirms the change.'
}
$notificationDisableStart = $source.IndexOf('@objc private func echoDeskDisableNotifications()')
$notificationDisableEnd = $source.IndexOf('private func syncEchoDeskNotificationPreferenceIfNeeded()', $notificationDisableStart)
$notificationDisable = $source.Substring($notificationDisableStart, $notificationDisableEnd - $notificationDisableStart)
if ($notificationDisable.IndexOf('UserDefaults.standard.set(false') -lt $notificationDisable.IndexOf('if ok')) {
    throw 'Notification disable is persisted before EchoDesk confirms the change.'
}

$dashboardApplyStart = $source.IndexOf('@objc private func echoDeskApplyPcDashboard()')
$dashboardApplyEnd = $source.IndexOf('private func buildEchoDeskSetupPage()', $dashboardApplyStart)
if ($dashboardApplyStart -lt 0 -or $dashboardApplyEnd -le $dashboardApplyStart) {
    throw 'PC Dashboard Apply implementation could not be isolated.'
}
$dashboardApply = $source.Substring($dashboardApplyStart, $dashboardApplyEnd - $dashboardApplyStart)
if (-not $dashboardApply.Contains('requestEchoDeskPcDashboardState { [weak self] ready in') -or
    -not $dashboardApply.Contains('sendEchoDeskPcDashboardConfig(metrics)')) {
    throw 'PC Dashboard Apply does not preflight a fresh device revision before sending its configuration.'
}

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
    'let savedSetupStep = EchoDeskSetupStep(rawValue:',
    'let setupInProgress = savedSetupStep != .connect',
    'guard !setupInProgress'
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
    'let savedSetupStep = EchoDeskSetupStep(rawValue: defaults.integer(forKey: "EchoDeskGuidedSetupStepV2")) ?? .connect',
    'if savedSetupStep != .connect'
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
    throw 'The macOS 0.2.10 setup must not contain a Terms step.'
}
Assert-Contains $main 'let kAppVersion = "0.2.10"'
Assert-Contains $main 'removeMenuBarStatusItem()'
Assert-Contains $main 'var availableUpdateAsset: GitHubAsset?'
Assert-Contains $main '$0.name.hasPrefix("EchoDesk-Desktop-v")'
Assert-Contains $main '$0.name.hasSuffix("-macos-universal.zip")'
Assert-Contains $build 'APP_VERSION="0.2.10"'
Assert-Contains $build 'SettingsWindow+EchoDeskDesktop.swift'
Assert-Contains $workflow 'EchoDesk-Desktop-v0.2.10-macos-universal'

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
    '/usr/bin/hdiutil attach "$DMG_RW_PATH"', 'DMG_MOUNT_DIR#/Volumes/', '/usr/bin/osascript <<OSA',
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

foreach ($layoutNeedle in @(
    'final class EchoDeskAdaptiveStackView: NSStackView',
    'page.isHidden = true',
    'page.setAccessibilityHidden(true)',
    'target.layoutSubtreeIfNeeded()',
    'echoDeskPageTransitionGeneration += 1',
    'button.layer?.backgroundColor = selected',
    'button.bezelColor = .controlAccentColor',
    'label.maximumNumberOfLines = 0',
    'scroll.contentInsets = NSEdgeInsets(top: 0, left: 0, bottom: 22, right: 0)'
)) {
    if (-not $source.Contains($layoutNeedle)) {
        throw "EchoDesk macOS shell polish regression: missing $layoutNeedle"
    }
}
if ($source.Contains('stack.edgeInsets = NSEdgeInsets(top: 0, left: 0, bottom: 22')) {
    throw 'Nested macOS card stacks must not inherit page-level bottom spacing.'
}
if ($source.Contains('echoDeskPageViews.first(where:')) {
    throw 'Page transitions must use the exact previous page, not the first temporarily visible page.'
}

$settingsSource = Get-Content -LiteralPath (Join-Path $repo 'companion\Sources\SettingsWindow.swift') -Raw
if (-not $settingsSource.Contains('echoDeskBedsidePreviewTimer?.invalidate()')) {
    throw 'The Bedside preview timer is not released when the settings window closes.'
}

$versionNode = $plist.SelectSingleNode("//key[.='CFBundleShortVersionString']/following-sibling::*[1]")
if ($null -eq $versionNode -or $versionNode.InnerText -ne '0.2.10') {
    throw 'EchoDesk Info.plist version is not 0.2.10.'
}

Write-Host 'EchoDesk macOS 0.2.10 parity UI validation passed.'
