$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$desktopPath = Join-Path $repo 'companion\Sources\SettingsWindow+EchoDeskDesktop.swift'
$settingsPath = Join-Path $repo 'companion\Sources\SettingsWindow.swift'
$mainPath = Join-Path $repo 'companion\Sources\main.swift'

foreach ($path in @($desktopPath, $settingsPath, $mainPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "PC Dashboard validation input is missing: $path" }
}

$desktop = Get-Content -LiteralPath $desktopPath -Raw
$settings = Get-Content -LiteralPath $settingsPath -Raw
$main = Get-Content -LiteralPath $mainPath -Raw

function Assert-Contains([string] $Text, [string] $Needle) {
    if (-not $Text.Contains($Needle)) { throw "PC Dashboard sync contract is missing: $Needle" }
}

# Regression for the desktop-only failure where changing several metric slots
# launches overlapping get-state/set-config transactions. Firmware correctly
# rejects the stale base revision; the client must serialize one write, retain
# only the newest desired snapshot, and retry one stale revision after refresh.
foreach ($stateContract in @(
    'echoDeskPcDashboardPendingMetrics',
    'echoDeskPcDashboardInFlightMetrics',
    'echoDeskPcDashboardApplyInFlight',
    'echoDeskPcDashboardExpectedPort',
    'echoDeskPcDashboardExpectedMAC',
    'echoDeskPcDashboardRetryAfterRefresh',
    'echoDeskPcDashboardDidRetryStaleRevision'
)) { Assert-Contains $settings $stateContract }

foreach ($syncContract in @(
    'queueEchoDeskPcDashboardApply(metrics)',
    'flushPendingEchoDeskPcDashboardApplyIfReady()',
    'monitor.serialPort.connectedPort == echoDeskPcDashboardExpectedPort',
    'DeviceRegistry.shared.currentProfile()?.mac == echoDeskPcDashboardExpectedMAC',
    'echoDeskPcDashboardPendingMetrics ?? echoDeskPcDashboardInFlightMetrics',
    'errorCode == "stale_revision"',
    'echoDeskPcDashboardDidRetryStaleRevision = true',
    'requestEchoDeskPcDashboardState()',
    'if self.echoDeskPcDashboardPendingMetrics == nil'
)) { Assert-Contains $desktop $syncContract }

foreach ($reverseSyncContract in @(
    'var onEchoDeskState: (([String: Any]) -> Void)?',
    'case "echodesk_state":',
    'self?.onEchoDeskState?(json)',
    'var onEchoDeskState: (() -> Void)?',
    'serialPort.onEchoDeskState = { [weak self] _ in self?.onEchoDeskState?() }',
    'settingsController?.handleUnsolicitedEchoDeskState()'
)) { Assert-Contains $main $reverseSyncContract }

foreach ($refreshContract in @(
    'func handleUnsolicitedEchoDeskState()',
    'echoDeskPcDashboardPendingMetrics == nil',
    'echoDeskPcDashboardInFlightMetrics == nil',
    'echoDeskPcDashboardStatus?.stringValue = "EchoDesk changed this layout. Refreshing…"'
)) { Assert-Contains $desktop $refreshContract }

$applyStart = $desktop.IndexOf('@objc private func echoDeskApplyPcDashboard()')
$sendStart = $desktop.IndexOf('private func sendEchoDeskPcDashboardConfig', $applyStart)
if ($applyStart -lt 0 -or $sendStart -le $applyStart) {
    throw 'PC Dashboard apply/send implementation could not be isolated.'
}
$apply = $desktop.Substring($applyStart, $sendStart - $applyStart)
if ($apply.Contains('requestEchoDeskPcDashboardState {')) {
    throw 'Each PC Dashboard edit still launches an independent preflight transaction.'
}

Write-Host 'EchoDesk macOS PC Dashboard synchronization validation passed.'
