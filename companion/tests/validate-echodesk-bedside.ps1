$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bedsidePath = Join-Path $repo 'companion\Sources\SettingsWindow+EchoDeskBedside.swift'
$desktopPath = Join-Path $repo 'companion\Sources\SettingsWindow+EchoDeskDesktop.swift'
$settingsPath = Join-Path $repo 'companion\Sources\SettingsWindow.swift'
$mainPath = Join-Path $repo 'companion\Sources\main.swift'
$buildPath = Join-Path $repo 'companion\build.sh'

foreach ($path in @($bedsidePath, $desktopPath, $settingsPath, $mainPath, $buildPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Bedside validation input is missing: $path" }
}

$bedside = Get-Content -LiteralPath $bedsidePath -Raw
$desktop = Get-Content -LiteralPath $desktopPath -Raw
$settings = Get-Content -LiteralPath $settingsPath -Raw
$main = Get-Content -LiteralPath $mainPath -Raw
$build = Get-Content -LiteralPath $buildPath -Raw

function Assert-Contains([string] $Text, [string] $Needle) {
    if (-not $Text.Contains($Needle)) { throw "EchoDesk Bedside contract is missing: $Needle" }
}

foreach ($copy in @(
    'Bedside Mode', 'Designed for power-only use after setup.', 'Open on EchoDesk',
    'Clock', 'Widgets', 'Manage alarms', 'Auto-brightness and display',
    'Weather — Coming soon', 'Night Shift', 'Dark and Light appearance',
    'Use brightness schedule', 'Manual brightness', 'Use Night Shift schedule',
    'Use appearance schedule', 'Add alarm', 'Add time', 'Save display settings'
)) { Assert-Contains $bedside $copy }

foreach ($schema in @(
    '"cmd": "echodesk_bedside_get_state", "version": 1, "requestId": request',
    'acceptedTypes: ["echodesk_bedside_state", "echodesk_error"]',
    '"cmd": "echodesk_bedside_set_config", "version": 1, "requestId": request',
    '"baseRevision": echoDeskBedsideRevision, "persist": true, "config": config.json',
    'json?["command"] as? String == "echodesk_bedside_set_config"',
    'self.requestEchoDeskBedsideState()',
    '"clock": ["format": clockFormat, "showSeconds": clockShowSeconds, "style": clockStyle]',
    '"widgets": ["nextAlarm": nextAlarmWidget, "weather": weatherWidget]',
    '"alarms": alarms.map(\.json)',
    '"nightShift": ["scheduleEnabled": nightScheduleEnabled',
    '"theme": ["scheduleEnabled": themeScheduleEnabled'
)) { Assert-Contains $bedside $schema }

foreach ($guard in @(
    '(1...8).contains(id)', 'label.utf8.count <= 24', 'Set(parsedAlarms.map(\.id)).count == parsedAlarms.count',
    '(1...8).contains(parsedSegments.count)', '(1...100).contains(manualBrightness)',
    '(0...1439).contains(value)', 'Set(next.segments.map(\.startMinute)).count == next.segments.count',
    'bedsideBoundedUTF8(row.label.stringValue, bytes: 24)',
    'bedsideStateReady', 'echoDeskBedsideStatePort == port'
)) { Assert-Contains $bedside $guard }

# Required default brightness schedule: 00:00 1%, 05:00 40%, 07:00 70%,
# 08:00 100%, 18:00 60%, 19:00 40%, 20:30 30%, 21:30 1%.
foreach ($segment in @(
    '.init(startMinute: 0, brightness: 1)',
    '.init(startMinute: 300, brightness: 40)',
    '.init(startMinute: 420, brightness: 70)',
    '.init(startMinute: 480, brightness: 100)',
    '.init(startMinute: 1080, brightness: 60)',
    '.init(startMinute: 1140, brightness: 40)',
    '.init(startMinute: 1230, brightness: 30)',
    '.init(startMinute: 1290, brightness: 1)'
)) { Assert-Contains $bedside $segment }

foreach ($integration in @(
    'case overview, deviceControl, customisation, bedside, setup, recovery',
    'buildEchoDeskBedsidePage()', 'if page == .bedside { requestEchoDeskBedsideState() }',
    '("bedside", "Bedside Mode", "bed.double.fill")', '("Bedside Mode", "bedside")'
)) { Assert-Contains $desktop $integration }
Assert-Contains $settings 'var echoDeskBedsideConfig = EchoDeskBedsideConfig.defaults'
Assert-Contains $settings 'var echoDeskBedsideStatePort: String?'
Assert-Contains $main 'deviceEchoDeskBedsideProtocol = json["echoDeskBedsideProtocol"] as? Int'
Assert-Contains $main 'deviceEchoDeskBedsideProtocol = nil'
Assert-Contains $build 'Sources/SettingsWindow+EchoDeskBedside.swift'

if ($bedside.Contains('UserDefaults')) {
    throw 'Bedside settings must be device-authoritative, not persisted locally.'
}
if (-not $bedside.Contains('echoDeskBedsideWeatherWidget?.isEnabled = false') -or
    -not $bedside.Contains('next.weatherWidget = false')) {
    throw 'Weather must remain visible as disabled Coming Soon UI and cannot be persisted as enabled.'
}

$sendStart = $bedside.IndexOf('private func sendEchoDeskBedsideConfig')
$sendEnd = $bedside.IndexOf('@objc private func echoDeskBedsideOpenOnDevice', $sendStart)
if ($sendStart -lt 0 -or $sendEnd -le $sendStart) { throw 'Bedside set-config implementation could not be isolated.' }
$send = $bedside.Substring($sendStart, $sendEnd - $sendStart)
if ($send.IndexOf('self.echoDeskBedsideConfig = config') -ge 0) {
    throw 'Bedside config is made authoritative before the device ACK.'
}
$ackIndex = $send.IndexOf('guard ok else')
$refreshIndex = $send.LastIndexOf('self.requestEchoDeskBedsideState()')
if ($ackIndex -lt 0 -or $refreshIndex -le $ackIndex) {
    throw 'Bedside config does not wait for an ACK and refresh authoritative state afterward.'
}

# AppKit does not derive a useful intrinsic height when a custom NSBox has its
# contentView replaced with an unconstrained stack. That collapses each card in
# the page stack and lets its controls draw over the preview and neighbouring
# cards. Keep the Bedside layout on the same explicit edge-to-edge constraint
# contract as the rest of EchoDesk Desktop.
$stackStart = $bedside.IndexOf('private func bedsideStack')
$stackEnd = $bedside.IndexOf('private func bedsideCard', $stackStart)
$cardEnd = $bedside.IndexOf('private func bedsideLabel', $stackEnd)
if ($stackStart -lt 0 -or $stackEnd -le $stackStart -or $cardEnd -le $stackEnd) {
    throw 'Bedside native layout helpers could not be isolated.'
}
$stackHelper = $bedside.Substring($stackStart, $stackEnd - $stackStart)
$cardHelper = $bedside.Substring($stackEnd, $cardEnd - $stackEnd)
foreach ($needle in @(
    'if view is NSTextField || view is NSStackView',
    'view.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true',
    'let card = EchoDeskSurfaceView(.card)',
    'content.translatesAutoresizingMaskIntoConstraints = false',
    'content.leadingAnchor.constraint(equalTo:',
    'content.trailingAnchor.constraint(equalTo:',
    'content.topAnchor.constraint(equalTo:',
    'content.bottomAnchor.constraint(equalTo:'
)) {
    if (-not (($stackHelper + $cardHelper).Contains($needle))) {
        throw "Bedside layout regression: missing explicit sizing contract: $needle"
    }
}
if ($cardHelper.Contains('card.contentView = content')) {
    throw 'Bedside layout regression: replacing NSBox.contentView collapses card height and overlaps controls.'
}
Assert-Contains $bedside 'NSTextField(wrappingLabelWithString: text)'
if ($bedside.Contains('preview.widthAnchor.constraint(equalToConstant: 416)')) {
    throw 'Bedside layout regression: the preview must compress with the page instead of forcing overflow at 820px.'
}
Assert-Contains $bedside 'preview.heightAnchor.constraint(equalTo: preview.widthAnchor'
Assert-Contains $bedside 'icon.setAccessibilityHidden(true)'
if ($bedside.Contains('max(0.65, min(1.0')) {
    throw 'Bedside preview drawing no longer fits the supported 240-point compact preview width.'
}

Write-Host 'EchoDesk macOS Bedside Mode validation passed.'
